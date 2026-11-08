## 들어가며

> **핵심 질문**: "패킷이 커널을 통과하는 여정"


네트워크 패킷이 NIC(Network Interface Card)에 도착한 순간부터 애플리케이션의 소켓까지, 그리고 반대 방향으로 돌아가는 과정은 리눅스 커널에서 가장 복잡한 서브시스템 중 하나다. 인터럽트 처리, 프로토콜 스택 순회, 방화벽 규칙 적용, 흐름 제어까지 수십 개의 계층을 거친다.

이 글은 패킷의 생명주기를 따라가며 커널 네트워크 스택의 내부를 살펴본다. OSI/TCP IP 모델 매핑, sk_buff 구조, 수신/송신 경로, Netfilter, TCP 상태 머신, 흐름 제어와 혼잡 제어, NAPI/GRO 같은 고성능 기법까지 다룬다.


---

## 목차
1. [OSI vs TCP/IP 모델](#1-osi-vs-tcpip-모델)
2. [sk_buff: 네트워크의 bio](#2-sk_buff-네트워크의-bio)
3. [수신 경로: NIC에서 소켓까지](#3-수신-경로-nic에서-소켓까지)
4. [송신 경로: 소켓에서 NIC까지](#4-송신-경로-소켓에서-nic까지)
5. [Netfilter와 iptables](#5-netfilter와-iptables)
6. [TCP 상태 머신](#6-tcp-상태-머신)
7. [소켓 버퍼와 흐름 제어](#7-소켓-버퍼와-흐름-제어)
8. [NAPI와 폴링](#8-napi와-폴링)
9. [실전 디버깅](#9-실전-디버깅)


---

## 1. OSI vs TCP/IP 모델

### 이론: OSI 7계층

OSI(Open Systems Interconnection) 모델은 네트워크 통신을 7개 계층으로 분리한다:

```text
7. Application  - HTTP, FTP, DNS
6. Presentation - SSL/TLS, 인코딩
5. Session      - 세션 관리
4. Transport    - TCP, UDP
3. Network      - IP, ICMP, 라우팅
2. Data Link    - Ethernet, MAC 주소
1. Physical     - 전기 신호, 케이블
```

**문제점**: 실제 구현과 괴리가 크다. 5/6/7 계층의 경계가 모호하고, 하드웨어 발전과 안 맞음.

### 실제: TCP/IP 4계층

리눅스를 비롯한 실제 시스템은 **TCP/IP 모델**을 따른다:

```text
4. Application  - HTTP, DNS (사용자 공간)
   ------------ syscall 경계 ------------
3. Transport    - TCP/UDP (커널)
2. Internet     - IP, routing
1. Link         - Ethernet, driver
```

**리눅스 커널 구조와 매핑:**
```text
User Space:        socket API
                   ↓
Transport Layer:   net/ipv4/tcp.c, net/ipv4/udp.c
                   ↓
Network Layer:     net/ipv4/ip_input.c, net/ipv4/route.c
                   ↓
Link Layer:        drivers/net/ethernet/...
                   ↓
Hardware:          NIC
```

## 2. sk_buff: 네트워크의 bio

### 문제: 패킷 데이터 관리

패킷은 계층을 거치며:
- **헤더 추가/제거**: TCP 헤더, IP 헤더, Ethernet 헤더
- **데이터 복사**: 성능 저하의 주범
- **메타데이터**: 타임스탬프, 체크섬, 라우팅 정보

**요구사항**: 복사 없이 헤더 조작, 공유 데이터 관리, 효율적 메모리 사용.

### sk_buff 구조

```c
// include/linux/skbuff.h
struct sk_buff {
    /* 1. 포인터 체인 */
    struct sk_buff      *next;
    struct sk_buff      *prev;

    /* 2. 소켓/디바이스 */
    struct sock         *sk;
    struct net_device   *dev;

    /* 3. 타임스탬프 */
    ktime_t             tstamp;

    /* 4. 데이터 포인터 */
    unsigned char       *head;      // 버퍼 시작
    unsigned char       *data;      // 현재 데이터
    unsigned char       *tail;      // 데이터 끝
    unsigned char       *end;       // 버퍼 끝

    /* 5. 길이 정보 */
    unsigned int        len;        // 데이터 길이
    unsigned int        data_len;   // frags 길이

    /* 6. 프로토콜 정보 */
    __be16              protocol;   // ETH_P_IP, ETH_P_IPV6
    __u16               transport_header;
    __u16               network_header;
    __u16               mac_header;

    /* 7. 체크섬 */
    __wsum              csum;
    __u8                ip_summed;

    /* frags[]와 shared metadata는 struct sk_buff의 직접 멤버가 아니다. */
};
```

non-linear payload의 `frags[]`와 GSO metadata는 데이터 버퍼 끝에 놓인 `struct skb_shared_info`에 들어 있다. 커널 코드는 `skb_shinfo(skb)`로 이를 얻는다. 즉 `skb->frags`나 `skb->shinfo`처럼 직접 접근하는 구조가 아니다.

### 제로카피: 헤더 조작

**송신 경로 (헤더 추가):**
```c
// net/ipv4/tcp_output.c
struct sk_buff *skb = alloc_skb(MAX_TCP_HEADER + len, GFP_ATOMIC);

// 공간 예약 (복사 없음)
skb_reserve(skb, MAX_TCP_HEADER);

// 데이터 추가
memcpy(skb_put(skb, len), data, len);

// TCP 헤더 추가 (head 쪽으로 확장)
struct tcphdr *th = (struct tcphdr *)skb_push(skb, sizeof(struct tcphdr));
th->source = htons(sport);
th->dest = htons(dport);

// IP 헤더 추가
struct iphdr *iph = (struct iphdr *)skb_push(skb, sizeof(struct iphdr));
```

**메모리 레이아웃 변화:**
```text
할당 직후:
[head]    [data/tail]                    [end]
|---------|------------------------------|

skb_put(skb, 100):
[head]    [data]      [tail]             [end]
|---------|===========|------------------|
          | 100 bytes |

skb_push(skb, 20):  // TCP header
[head]  [data]        [tail]             [end]
|-------|=============|-------------------|
        |<-20->|<-100->|
        | TCP  | DATA  |
```

**수신 경로 (헤더 제거):**
```c
// net/ipv4/ip_input.c
struct iphdr *iph = ip_hdr(skb);

// IP 헤더 검증 후 제거
skb_pull(skb, iph->ihl * 4);

// 이제 skb->data는 TCP 헤더를 가리킴
```

### 분산 데이터: skb_shared_info

**문제**: 큰 패킷(MTU > 1500)을 연속 메모리에 할당하면 비효율적.

**해결**: 헤더는 연속, 데이터는 페이지 조각(frags)으로 분산.

```c
struct skb_shared_info {
    unsigned short  nr_frags;
    skb_frag_t      frags[MAX_SKB_FRAGS];  // 페이지 조각들
    // ...
};

typedef struct skb_frag_struct {
    struct page *page;
    __u32       page_offset;
    __u32       size;
} skb_frag_t;
```

**예시: 9KB 패킷 (Jumbo Frame)**
```text
sk_buff:
  head -> [IP헤더|TCP헤더] (256 bytes 연속 메모리)

  frags[0] -> page1 + offset 0, size 4096
  frags[1] -> page2 + offset 0, size 4096
  frags[2] -> page3 + offset 0, size 648
```

**장점**:
- DMA 직접 수행 가능 (NIC → 페이지)
- 메모리 할당 실패 확률 감소
- sendfile 같은 제로카피 시스템콜 지원

## 3. 수신 경로: NIC에서 소켓까지

### 전체 흐름

```text
[1] NIC      → DMA를 통해 패킷을 메모리에 기록
              ↓
[2] Hard IRQ → 드라이버 인터럽트 핸들러 (최소 작업만)
              ↓
[3] Soft IRQ → IRQ 복귀(irq_exit) 시점 또는 ksoftirqd가 패킷 처리
              ↓
[4] IP Layer → 라우팅, Netfilter 체크
              ↓
[5] TCP/UDP  → 체크섬, 재조립, 흐름 제어
              ↓
[6] Socket   → 소켓 버퍼에 추가
              ↓
[7] App      → read()/recv() 시스템콜
```

### [1-2] NIC와 드라이버

**DMA 링 버퍼:**
```c
// drivers/net/ethernet/intel/e1000/e1000_main.c
struct e1000_rx_ring {
    void *desc;                  // RX descriptor ring
    dma_addr_t dma;              // DMA 물리 주소
    struct e1000_buffer *buffer_info;
    unsigned int next_to_use;
    unsigned int next_to_clean;
};
```

**인터럽트 핸들러 (Hard IRQ):**
```c
static irqreturn_t e1000_intr(int irq, void *data) {
    struct net_device *netdev = data;
    struct e1000_adapter *adapter = netdev_priv(netdev);

    // 1. 인터럽트 원인 확인
    u32 icr = E1000_READ_REG(&adapter->hw, ICR);
    if (!icr)
        return IRQ_NONE;

    // 2. 추가 인터럽트 비활성화
    E1000_WRITE_REG(&adapter->hw, IMC, ~0);

    // 3. NAPI 스케줄링 (Soft IRQ로 위임)
    napi_schedule(&adapter->napi);

    return IRQ_HANDLED;  // 수십 마이크로초 내 반환
}
```

### [3] NAPI와 Soft IRQ

**문제**: 고속 네트워크에서 패킷당 인터럽트는 CPU를 압도한다.

**NAPI (New API) 해결책:**
- **저속**: 인터럽트 방식 (지연 최소화)
- **고속**: 폴링 방식 (오버헤드 감소)

```c
// net/core/dev.c
static int napi_poll(struct napi_struct *napi, int budget) {
    struct e1000_adapter *adapter = container_of(napi, ...);
    int work_done = 0;

    // 최대 budget(64)개 패킷 처리
    while (work_done < budget) {
        struct sk_buff *skb = e1000_receive_skb(adapter);
        if (!skb)
            break;

        netif_receive_skb(skb);  // 상위 계층으로
        work_done++;
    }

    if (work_done < budget) {
        // 패킷이 적으면 폴링 종료, 인터럽트 재활성화
        napi_complete(napi);
        e1000_irq_enable(adapter);
    }

    return work_done;
}
```

**ksoftirqd**: Soft IRQ 전용 커널 스레드
```bash
$ ps aux | grep ksoftirqd
root  12  0.0  0.0  ksoftirqd/0
root  18  0.0  0.0  ksoftirqd/1
```

### [4] IP 계층

```c
// net/ipv4/ip_input.c
int ip_rcv(struct sk_buff *skb, struct net_device *dev, ...) {
    struct iphdr *iph;

    // 1. 기본 검증
    if (skb->len < sizeof(struct iphdr))
        goto drop;

    iph = ip_hdr(skb);

    // 2. 체크섬
    if (ip_fast_csum((u8 *)iph, iph->ihl))
        goto drop;

    // 3. 길이 검증 (TTL 만료 판정·ICMP Time Exceeded는 수신 경로가 아니라 ip_forward의 몫이다)
    if (skb->len < ntohs(iph->tot_len))
        goto drop;

    // 4. Netfilter (NF_INET_PRE_ROUTING)
    return NF_HOOK(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, dev, NULL,
                   ip_rcv_finish);

drop:
    kfree_skb(skb);
    return NET_RX_DROP;
}
```

**라우팅 결정:**
```c
static int ip_rcv_finish(struct sk_buff *skb) {
    struct iphdr *iph = ip_hdr(skb);

    // FIB (Forwarding Information Base) 조회
    int err = ip_route_input(skb, iph->daddr, iph->saddr, iph->tos, skb->dev);

    if (skb_dst(skb)->input == ip_local_deliver)
        return ip_local_deliver(skb);  // 로컬 전달
    else
        return ip_forward(skb);         // 포워딩
}
```

### [5] TCP 계층

```c
// net/ipv4/tcp_ipv4.c
int tcp_v4_rcv(struct sk_buff *skb) {
    struct tcphdr *th = tcp_hdr(skb);

    // 1. 소켓 찾기 (해시 테이블)
    struct sock *sk = __inet_lookup_skb(&tcp_hashinfo, skb, th->source, th->dest);
    if (!sk)
        goto no_tcp_socket;

    // 2. 체크섬
    if (tcp_v4_checksum_init(skb))
        goto bad_packet;

    // 3. 상태 머신 처리
    if (sk->sk_state == TCP_TIME_WAIT)
        goto do_time_wait;

    if (sk->sk_state == TCP_NEW_SYN_RECV) {
        // SYN-RECEIVED 처리 (별도 helper가 연결 성립을 처리)
        return tcp_check_req(sk, skb);
    }

    // 4. ESTABLISHED 소켓으로 전달: receive queue로 넣는다.
    //
    // 참고: tcp_prequeue()는 과거 receive path에 있던 사용자 컨텍스트
    // 우선 큐다. 현재 커널에서는 제거되었고, 수신 경로는 backlog 큐
    // (sk_add_backlog) 또는 직접 sk_receive_queue로 들어가는 형태로
    // 일원화되었다. 아래 호출은 옛 모델을 보여주는 의사 코드로 본다.
    tcp_queue_rcv(sk, skb);
    return 0;

no_tcp_socket:
    // RST 전송
    tcp_v4_send_reset(NULL, skb);
}
```

### [6-7] 소켓 버퍼와 애플리케이션

```c
// net/ipv4/tcp_input.c
static int tcp_queue_rcv(struct sock *sk, struct sk_buff *skb) {
    // 1. 소켓 수신 버퍼 확인
    if (sk_rmem_alloc_get(sk) + skb->truesize > sk->sk_rcvbuf) {
        // 버퍼 풀 → 드롭
        __kfree_skb(skb);
        return -ENOMEM;
    }

    // 2. 수신 큐에 추가
    skb_queue_tail(&sk->sk_receive_queue, skb);

    // 3. 대기 중인 프로세스 깨우기
    sk->sk_data_ready(sk);

    return 0;
}
```

**애플리케이션 읽기:**
```c
// net/ipv4/tcp.c
int tcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, ...) {
    struct tcp_sock *tp = tcp_sk(sk);
    int copied = 0;
    lock_sock(sk);

    while (len > 0) {
        struct sk_buff *skb = skb_peek(&sk->sk_receive_queue);
        if (!skb) {
            if (sk->sk_err)
                break;
            // 블로킹 대기
            sk_wait_data(sk, &timeo);
            continue;
        }

        // 이미 소비한 offset을 유지한다. copied_seq는 사용자에게
        // 아직 넘기지 않은 첫 byte를 가리킨다.
        u32 offset = tp->copied_seq - TCP_SKB_CB(skb)->seq;
        u32 avail = skb->len - offset;
        int chunk = min_t(unsigned int, avail, len);

        // 사용자 공간으로 복사 (매번 offset 0에서 시작하지 않는다)
        if (skb_copy_datagram_msg(skb, offset, msg, chunk))
            break;
        copied += chunk;
        len -= chunk;
        tp->copied_seq += chunk;  // 실제 소비량만큼 전진

        // 이 skb를 모두 소비했을 때만 제거
        if (offset + chunk == skb->len)
            sk_eat_skb(sk, skb);
    }

    release_sock(sk);
    return copied;  // 읽은 바이트 수
}
```

## 4. 송신 경로: 소켓에서 NIC까지

### 전체 흐름

```text
[1] App      → write()/send() 시스템콜
              ↓
[2] TCP/UDP  → 세그먼트 생성, 혼잡 제어
              ↓
[3] IP Layer → 라우팅, Netfilter, 단편화
              ↓
[4] Qdisc    → Traffic Control (큐잉)
              ↓
[5] Driver   → DMA 설정
              ↓
[6] NIC      → 하드웨어 전송
```

### [1-2] 소켓에서 TCP

```c
// net/ipv4/tcp.c
int tcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t size) {
    struct tcp_sock *tp = tcp_sk(sk);
    int mss = tcp_send_mss(sk, &size, flags);
    int copied = 0;

    while (size > 0) {
        // 1. 송신 버퍼 확인
        if (sk_wmem_alloc_get(sk) >= sk->sk_sndbuf) {
            // 버퍼 풀 → 대기
            sk_wait_event(sk, &timeo, ...);
            continue;
        }

        // 2. sk_buff 할당 (실패하면 지금까지 접수한 양을 반환)
        struct sk_buff *skb = sk_stream_alloc_skb(sk, 0, sk->sk_allocation);
        if (!skb)
            break;

        // 3. 데이터 복사 (실제 복사한 길이를 사용)
        int copy = min_t(int, size, mss);
        if (skb_add_data(skb, &msg->msg_iter, copy) < 0) {
            kfree_skb(skb);  // 큐에 넣기 전 실패: 회수
            break;
        }

        // 4. write queue로 소유권 이전: 이 시점부터 skb는 소켓이 소유한다.
        tcp_add_write_queue_tail(sk, skb);

        // 5. 송신 큐로 push (소유권은 이미 소켓에 있음)
        tcp_push_one(sk, mss);

        copied += copy;
        size -= copy;
    }

    return copied;  // 실제로 접수한(복사한) 바이트 수
}
```

**TCP 세그먼트 전송:**
```c
static int tcp_transmit_skb(struct sock *sk, struct sk_buff *skb) {
    struct tcp_sock *tp = tcp_sk(sk);
    struct tcphdr *th;

    // 1. TCP 헤더 구성
    th = (struct tcphdr *)skb_push(skb, tcp_header_size);
    th->source = inet->inet_sport;
    th->dest = inet->inet_dport;
    th->seq = htonl(tp->snd_nxt);
    th->ack_seq = htonl(tp->rcv_nxt);
    th->window = htons(tcp_select_window(sk));

    // 2. 체크섬
    th->check = tcp_v4_calc_checksum(skb);

    // 3. IP 계층으로
    return ip_queue_xmit(sk, skb);
}
```

### [3] IP 계층

```c
// net/ipv4/ip_output.c
int ip_queue_xmit(struct sock *sk, struct sk_buff *skb) {
    struct rtable *rt;
    struct iphdr *iph;

    // 1. 라우팅 (캐싱된 경로 사용)
    rt = (struct rtable *)__sk_dst_check(sk, 0);
    if (!rt) {
        rt = ip_route_output_ports(...);
        sk_setup_caps(sk, &rt->dst);
    }

    // 2. IP 헤더 추가
    iph = (struct iphdr *)skb_push(skb, sizeof(struct iphdr));
    iph->version = 4;
    iph->ihl = 5;
    iph->ttl = ip4_dst_hoplimit(&rt->dst);
    iph->protocol = sk->sk_protocol;
    iph->saddr = fl4->saddr;
    iph->daddr = fl4->daddr;

    // 3. Netfilter (NF_INET_LOCAL_OUT)
    return NF_HOOK(NFPROTO_IPV4, NF_INET_LOCAL_OUT, skb, NULL,
                   rt->dst.dev, ip_output);
}
```

**단편화 (MTU 초과 시):**
```c
int ip_fragment(struct sk_buff *skb, int (*output)(struct sk_buff *)) {
    unsigned int mtu = dst_mtu(&rt->dst);
    unsigned int hlen = iph->ihl * 4;
    unsigned int left = skb->len - hlen;

    while (left > 0) {
        unsigned int len = min_t(unsigned int, left, mtu - hlen);
        struct sk_buff *skb2 = alloc_skb(len + hlen + LL_RESERVED_SPACE, ...);

        // 헤더 복사 + 데이터 조각 복사
        skb_copy_from_linear_data(skb, skb_network_header(skb2), hlen);
        // ...

        output(skb2);
        left -= len;
    }
}
```

### [4] Qdisc: Traffic Control

**문제**: 여러 애플리케이션의 패킷을 어떤 순서로 전송할 것인가?

**Qdisc (Queueing Discipline)**: 송신 큐 관리자

```c
// include/net/sch_generic.h
struct Qdisc {
    int (*enqueue)(struct sk_buff *skb, struct Qdisc *sch);
    struct sk_buff *(*dequeue)(struct Qdisc *sch);
    unsigned int    queue_len;
    struct net_device *dev;
};
```

**기본 Qdisc는 환경에 따라 다르다**

`net.core.default_qdisc` sysctl로 설정한다. 커널 기본값은 시기에 따라 `pfifo_fast` → `fq_codel`로 옮겨갔고, 멀티 큐 NIC에는 `mq`(하드웨어 큐별 child qdisc), 가상 인터페이스에는 `noqueue`가 자동으로 붙는 경우가 많다. 현재 시스템 기본을 확인하려면 `sysctl net.core.default_qdisc`와 `tc qdisc show`로 점검한다.

아래는 단일 NIC에 옛 기본값이었던 `pfifo_fast`가 붙어 있을 때의 동작이다.

```text
Band 0 (우선순위 높음): [skb]->[skb]->...
Band 1 (중간):          [skb]->...
Band 2 (낮음):          [skb]->[skb]->[skb]->...

→ Band 0부터 순서대로 dequeue
```

**TOS 기반 분류:**
```c
static inline int pfifo_fast_classify(struct sk_buff *skb) {
    // IP TOS 필드에서 우선순위 추출
    return (skb->priority & TC_PRIO_MAX);
}
```

**확인:**
```bash
$ tc qdisc show dev eth0
qdisc pfifo_fast 0: root refcnt 2 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1

# 변경 예시: HTB (Hierarchical Token Bucket)
tc qdisc add dev eth0 root handle 1: htb default 12
tc class add dev eth0 parent 1: classid 1:1 htb rate 100mbit
tc class add dev eth0 parent 1:1 classid 1:10 htb rate 50mbit
tc class add dev eth0 parent 1:1 classid 1:20 htb rate 50mbit
```

### [5-6] 드라이버와 NIC

```c
// drivers/net/ethernet/intel/e1000/e1000_main.c
static netdev_tx_t e1000_xmit_frame(struct sk_buff *skb, struct net_device *netdev) {
    struct e1000_adapter *adapter = netdev_priv(netdev);
    struct e1000_tx_ring *tx_ring = adapter->tx_ring;
    unsigned int i = tx_ring->next_to_use;
    struct e1000_tx_desc *tx_desc;
    struct e1000_buffer *buffer_info;

    // 1. ring 공간 검사: 다음 슬롯이 아직 회수되지 않았으면 큐가 가득 찬 것이다.
    if (tx_ring->next_to_clean == (i + 1) % tx_ring->count)
        return NETDEV_TX_BUSY;

    tx_desc = E1000_TX_DESC(*tx_ring, i);
    buffer_info = &tx_ring->buffer_info[i];

    // 2. DMA 매핑 (linear 영역과 frags를 각각 매핑한다)
    dma_addr_t dma = dma_map_single(&pdev->dev, skb->data, skb->len, DMA_TO_DEVICE);
    if (dma_mapping_error(&pdev->dev, dma)) {
        dev_kfree_skb_any(skb);
        return NETDEV_TX_OK;
    }

    // 3. Descriptor 설정과 per-slot ownership 기록
    buffer_info->skb = skb;
    buffer_info->dma = dma;
    tx_desc->buffer_addr = cpu_to_le64(dma);
    tx_desc->lower.data = cpu_to_le32(skb->len | E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS);

    // 4. ring index를 증가/랩한 뒤 NIC에 통지 (MMIO write)
    tx_ring->next_to_use = (i + 1) % tx_ring->count;
    writel(tx_ring->next_to_use, adapter->hw.hw_addr + E1000_TDT);

    return NETDEV_TX_OK;
}
```

**전송 완료 인터럽트:**
```c
static void e1000_clean_tx_irq(struct e1000_adapter *adapter) {
    struct e1000_tx_ring *tx_ring = adapter->tx_ring;
    unsigned int i = tx_ring->next_to_clean;
    struct e1000_tx_desc *tx_desc = E1000_TX_DESC(*tx_ring, i);
    struct e1000_buffer *buffer_info = &tx_ring->buffer_info[i];

    while (tx_desc->upper.fields.status & E1000_TXD_STAT_DD) {
        // Descriptor Done bit 확인

        // DMA 언매핑: 매핑 때 기록한 per-slot buffer_info를 사용한다
        dma_unmap_single(&adapter->pdev->dev, buffer_info->dma,
                         buffer_info->skb->len, DMA_TO_DEVICE);

        // sk_buff 해제 후 슬롯을 비운다
        dev_kfree_skb_any(buffer_info->skb);
        buffer_info->skb = NULL;
        buffer_info->dma = 0;

        // 다음 descriptor로 전진하고 ring index를 회수한다
        i = (i + 1) % tx_ring->count;
        tx_desc = E1000_TX_DESC(*tx_ring, i);
        buffer_info = &tx_ring->buffer_info[i];
    }
    tx_ring->next_to_clean = i;
}
```

## 5. Netfilter와 iptables

### 훅 포인트

Netfilter는 패킷 경로 중 5개 지점에 **훅(hook)**을 제공한다:

```text
수신 패킷:
  NIC → [PRE_ROUTING] → routing decision →
        ↓ (local)           ↓ (forward)
   [LOCAL_IN]          [FORWARD]
        ↓                   ↓
      App              [POST_ROUTING] → NIC

송신 패킷:
  App → routing decision → [LOCAL_OUT] →
                           ↓
                      [POST_ROUTING] → NIC
```

**코드 구조:**
```c
// include/uapi/linux/netfilter.h
enum nf_inet_hooks {
    NF_INET_PRE_ROUTING,
    NF_INET_LOCAL_IN,
    NF_INET_FORWARD,
    NF_INET_LOCAL_OUT,
    NF_INET_POST_ROUTING,
};
```

### 규칙 등록

```c
// net/netfilter/core.c
int nf_register_net_hook(struct net *net, const struct nf_hook_ops *ops) {
    // 훅 리스트에 추가
    list_add_rcu(&ops->list, &net->nf.hooks[ops->pf][ops->hooknum]);
}

// 훅 실행
static inline int nf_hook(u_int8_t pf, unsigned int hook, struct sk_buff *skb, ...) {
    struct list_head *hook_list = &net->nf.hooks[pf][hook];

    list_for_each_entry(elem, hook_list, list) {
        verdict = elem->hook(elem->priv, skb, state);
        if (verdict != NF_ACCEPT)
            return verdict;  // NF_DROP, NF_STOLEN 등
    }

    return NF_ACCEPT;
}
```

### iptables 테이블

**5개 테이블, 각자 다른 목적:**
```c
1. filter  : 패킷 필터링 (ACCEPT/DROP)
2. nat     : 주소 변환 (SNAT/DNAT)
3. mangle  : 패킷 수정 (TOS, TTL)
4. raw     : 연결 추적 우회
5. security: MAC(SELinux 등) 보안 마크(SECMARK) 규칙
```

**체인(chain) 구조:**
```text
filter 테이블:
  - INPUT   (NF_INET_LOCAL_IN)
  - FORWARD (NF_INET_FORWARD)
  - OUTPUT  (NF_INET_LOCAL_OUT)

nat 테이블:
  - PREROUTING  (NF_INET_PRE_ROUTING)
  - OUTPUT      (NF_INET_LOCAL_OUT)
  - POSTROUTING (NF_INET_POST_ROUTING)
```

**예시 규칙:**
```bash
# 포트 22 차단
iptables -A INPUT -p tcp --dport 22 -j DROP

# 내부 커널 구조로 변환
struct ipt_entry {
    struct ipt_ip ip;              // 매칭 조건
    unsigned int nfcache;
    __u16 target_offset;
    __u16 next_offset;
    struct xt_counters counters;   // 패킷/바이트 카운터
    // ...
};

struct ipt_ip {
    __be32 src, dst;               // 192.168.1.0/24
    __be32 smsk, dmsk;
    char iniface[IFNAMSIZ];        // eth0
    __u8 proto;                    // IPPROTO_TCP
    // ...
};
```

### 연결 추적 (conntrack)

**목적**: Stateful 방화벽 - 연결 상태 기억.

```c
// include/net/netfilter/nf_conntrack.h
struct nf_conn {
    struct nf_conntrack_tuple_hash {
        struct {
            __be32 src, dst;
            union {
                __be16 all;
                struct { __be16 port; } tcp;
            };
            u_int8_t protonum;
        } tuple;
    } tuplehash[IP_CT_DIR_MAX];  // ORIGINAL, REPLY 방향별 tuple

    unsigned long status;  // IPS_CONFIRMED, IPS_SEEN_REPLY 등
    u32 timeout;
};
```

**상태 기반 규칙:**
```bash
# 기존 연결과 관련된 패킷만 허용
iptables -A INPUT -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
iptables -A INPUT -m conntrack --ctstate NEW -p tcp --dport 80 -j ACCEPT
iptables -A INPUT -j DROP

# /proc/net/nf_conntrack 확인
$ cat /proc/net/nf_conntrack
ipv4  2 tcp  6 431999 ESTABLISHED src=192.168.1.10 dst=93.184.216.34 sport=54321 dport=80 ...
```

## 6. TCP 상태 머신

### 상태 전이도

```text
연결 수립 (3-way handshake):

Client                              Server
CLOSED                              LISTEN
  | SYN (seq=100)                      |
  |-----------------------------------→|
SYN_SENT                           SYN_RCVD
  |                   SYN-ACK (seq=300, ack=101)
  |←-----------------------------------|
  | ACK (ack=301)                      |
  |-----------------------------------→|
ESTABLISHED                        ESTABLISHED

연결 종료 (4-way handshake):

  | FIN (seq=500)                      |
  |-----------------------------------→|
FIN_WAIT_1                        CLOSE_WAIT
  |                   ACK (ack=501)    |
  |←-----------------------------------|
FIN_WAIT_2                             |
  |                   FIN (seq=700)    |
  |←-----------------------------------|
TIME_WAIT                         LAST_ACK
  | ACK (ack=701)                      |
  |-----------------------------------→|
  |                                 CLOSED
  | (2*MSL 대기)
CLOSED
```

### 커널 구현

```c
// net/ipv4/tcp_input.c
int tcp_rcv_state_process(struct sock *sk, struct sk_buff *skb) {
    struct tcp_sock *tp = tcp_sk(sk);
    struct tcphdr *th = tcp_hdr(skb);

    switch (sk->sk_state) {
    case TCP_LISTEN:
        if (th->syn) {
            // SYN 수신 → SYN-RCVD
            return tcp_v4_conn_request(sk, skb);
        }
        break;

    case TCP_SYN_SENT:
        if (th->syn && th->ack) {
            // SYN-ACK 수신 → ESTABLISHED
            tcp_set_state(sk, TCP_ESTABLISHED);
            tcp_send_ack(sk);
        }
        break;

    case TCP_ESTABLISHED:
        tcp_data_queue(sk, skb);  // 데이터 처리
        tcp_ack(sk, skb);         // 수신한 ACK 처리 (ACK 전송은 tcp_send_ack)
        break;

    case TCP_FIN_WAIT_1:
        // ACK flag만으로는 우리 FIN이 확인됐다고 볼 수 없다.
        // cumulative ACK이 우리 FIN sequence까지 포함해(snd_una가 FIN 다음
        // seq에 도달) 전진했는지 확인한다. 오래된 데이터 ACK에도 flag는 있다.
        tcp_ack(sk, skb);
        if (tp->snd_una == tp->write_seq)  // 우리 FIN까지 ACK됨
            tcp_set_state(sk, TCP_FIN_WAIT_2);
        if (th->fin) {  // 상대 FIN 수신
            tcp_send_ack(sk);
            if (tp->snd_una == tp->write_seq)  // 우리 FIN도 확인됨: 동시 종료 완료
                tcp_time_wait(sk, TCP_TIME_WAIT, 0);
            else
                tcp_set_state(sk, TCP_CLOSING);  // 아직 우리 FIN 미확인
        }
        break;

    case TCP_FIN_WAIT_2:
        if (th->fin) {  // 상대 FIN 수신 → ACK 보내고 TIME_WAIT
            tcp_send_ack(sk);
            tcp_time_wait(sk, TCP_TIME_WAIT, 0);
        }
        break;

    // TIME_WAIT 소켓은 inet_timewait_sock으로 바뀌어 이 함수에 오지 않는다.
    // tcp_v4_rcv가 do_time_wait로 분기해 tcp_timewait_state_process가 처리하며,
    // 2*MSL(TCP_TIMEWAIT_LEN, 60초) 뒤 소멸한다.
    }
}
```

### TIME_WAIT 문제

**왜 필요한가?**
- 지연된 패킷이 새 연결에 섞이는 것 방지
- 상대방이 마지막 ACK를 받았는지 확인

**문제점**: 고부하 서버에서 수만 개의 TIME_WAIT 소켓 생성.

```bash
$ netstat -an | grep TIME_WAIT | wc -l
45231  # TIME_WAIT 소켓 수

# 현대 커널의 권장 튜닝 (Linux 4.x 이후)
sysctl -w net.ipv4.tcp_tw_reuse=1      # outgoing 연결에 한해 TIME_WAIT 재사용
sysctl -w net.ipv4.tcp_fin_timeout=30  # FIN-WAIT-2 타임아웃

# 주의: net.ipv4.tcp_tw_recycle은 Linux 4.12에서 제거되었다.
# NAT 뒤의 클라이언트와 호환성 문제(타임스탬프 기반 빠른 재활용이
# 같은 외부 IP에서 들어오는 여러 클라이언트를 구분하지 못함)로 폐기되었으며,
# 4.12 이상의 커널에서 sysctl로 설정해도 효과가 없다.
# tcp_tw_reuse는 처음부터 outgoing(connect) 경로에만 적용되며, 4.18부터 값이 0/1(전역)/2(loopback 한정, 기본값)로 나뉘었다.
```

TIME_WAIT은 마지막 ACK의 도착과 지연·재전송 패킷의 흡수를 보장하는 상태다. `SO_REUSEADDR`는 TIME_WAIT 객체를 즉시 없애는 명령이 아니라 로컬 주소 재바인딩을 허용하는 설정이며, 실제 회수 시점은 `2*MSL`(`TCP_TIMEWAIT_LEN`) 타이머가 결정한다. 문맥을 말할 때도 softirq가 항상 `ksoftirqd`에서 실행된다고 단정하지 않는다. IRQ 복귀 시점의 `irq_exit` 경로에서 먼저 처리되고, 밀린 경우에만 `ksoftirqd`로 넘어간다.

## 7. 소켓 버퍼와 흐름 제어

### 송수신 버퍼

```c
struct sock {
    int             sk_rcvbuf;  // 수신 버퍼 크기
    int             sk_sndbuf;  // 송신 버퍼 크기
    atomic_t        sk_rmem_alloc;  // 현재 수신 사용량
    atomic_t        sk_wmem_alloc;  // 현재 송신 사용량

    struct sk_buff_head sk_receive_queue;
    struct sk_buff_head sk_write_queue;
};
```

**기본값 확인:**
```bash
$ sysctl net.ipv4.tcp_rmem
net.ipv4.tcp_rmem = 4096    131072  6291456
                    (min)   (def)   (max)

$ sysctl net.ipv4.tcp_wmem
net.ipv4.tcp_wmem = 4096    16384   4194304
```

**애플리케이션 설정:**
```c
int sock = socket(AF_INET, SOCK_STREAM, 0);
int bufsize = 256 * 1024;  // 256KB

setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
```

### TCP 흐름 제어: Sliding Window

**수신측 윈도우 광고:**
```c
// net/ipv4/tcp_output.c
static u16 tcp_select_window(struct sock *sk) {
    struct tcp_sock *tp = tcp_sk(sk);
    u32 cur_win = tcp_receive_window(tp);
    u32 free_space = tcp_space(sk);  // 수신 버퍼 여유 공간

    // 버퍼 여유로 논리적(스케일된) 윈도우를 정하되,
    // 이미 광고한 윈도우보다 줄이지는 않는다 (RFC 1122)
    u32 window = max(cur_win, free_space);

    // wire에는 16-bit 필드가 실리므로 negotiated rcv_wscale만큼
    // 오른쪽으로 보낸다. 65535(2^16-1)를 넘는 값은 이 필드에 그대로
    // 담을 수 없으므로 right edge를 유지하도록 65535로 제한한다.
    u32 scaled = window >> tp->rx_opt.rcv_wscale;
    if (scaled > 65535)
        scaled = 65535;
    return (u16)scaled;
}
```

**송신측 윈도우 준수:**
```c
static unsigned int tcp_window_allows(struct tcp_sock *tp, struct sk_buff *skb) {
    u32 win = tp->snd_wnd;  // 상대방이 광고한 윈도우(바이트)
    u32 cwnd = tp->snd_cwnd * tp->mss_cache;  // 혼잡 윈도우(세그먼트 수 → 바이트)
    u32 in_flight = tcp_packets_in_flight(tp) * tp->mss_cache;  // 아직 ACK되지 않은 바이트

    // min(흐름 제어 윈도우, 혼잡 윈도우) - 이미 보낸 양 (언더플로 방지)
    if (min(win, cwnd) <= in_flight)
        return 0;
    return min(win, cwnd) - in_flight;
}
```

### 혼잡 제어: Slow Start & AIMD

**알고리즘 (Reno):**
```text
1. Slow Start: cwnd를 지수적으로 증가
   - ACK 받을 때마다: cwnd += 1
   - cwnd: 1 → 2 → 4 → 8 → 16 ...

2. Congestion Avoidance (ssthresh 도달 후):
   - ACK 받을 때마다: cwnd += 1/cwnd
   - 선형 증가: cwnd → cwnd+1 (RTT당)

3. Fast Retransmit/Recovery:
   - 3 duplicate ACK → 재전송
   - ssthresh = cwnd / 2
   - cwnd = ssthresh
```

**커널 구현:**
```c
// net/ipv4/tcp_cong.c
void tcp_reno_cong_avoid(struct sock *sk, u32 ack, u32 acked) {
    struct tcp_sock *tp = tcp_sk(sk);

    if (tp->snd_cwnd <= tp->snd_ssthresh) {
        // Slow Start
        acked = tcp_slow_start(tp, acked);
        if (!acked)
            return;
    }

    // Congestion Avoidance
    tcp_cong_avoid_ai(tp, tp->snd_cwnd, acked);
}

u32 tcp_slow_start(struct tcp_sock *tp, u32 acked) {
    u32 cwnd = min(tp->snd_cwnd + acked, tp->snd_ssthresh);
    acked -= cwnd - tp->snd_cwnd;
    tp->snd_cwnd = cwnd;
    return acked;
}
```

**최신 알고리즘: BBR (Bottleneck Bandwidth and RTT)**
```bash
# 설정
sysctl -w net.ipv4.tcp_congestion_control=bbr

# 확인
$ cat /proc/sys/net/ipv4/tcp_available_congestion_control
reno cubic bbr

# 연결별 확인
$ ss -tin
State  Recv-Q Send-Q Local:Port  Peer:Port
ESTAB  0      0      10.0.0.1:22 10.0.0.2:54321
         cubic wscale:7,7 rto:204 rtt:3.5/1.5 cwnd:10
```

## 8. NAPI와 폴링

### 전통적 인터럽트 방식의 한계

**문제 시나리오**: 10Gbps NIC, 64 byte 패킷
```text
패킷 수: 10Gbps / (64*8 bits) ≈ 19.5M pps (페이로드만 계산)
        실제 회선에서는 preamble+IFG 포함 84B/frame → 14.88M pps
인터럽트: 천만 단위/sec → CPU 100% 소진 (livelock)
```

**추가 문제:**
- instruction/data cache 오염과 RX descriptor·packet buffer의 cache-line 이동
- hard IRQ와 softirq 진입, interrupt moderation과 다른 작업의 실행 지연. 매 인터럽트가 process context switch나 전체 cache flush를 뜻하지는 않음

### NAPI 설계

**핵심 아이디어**: 적응형 폴링
```c
1. 첫 패킷: 인터럽트로 수신
2. Hard IRQ: 추가 인터럽트 비활성화, Soft IRQ 스케줄
3. Soft IRQ: budget(64) 한도 내에서 폴링
4. 패킷 고갈: 인터럽트 재활성화
```

**드라이버 코드:**
```c
// 초기화
static int my_driver_probe(struct pci_dev *pdev) {
    struct net_device *dev = alloc_etherdev(...);
    struct my_adapter *adapter = netdev_priv(dev);

    // 실제 등록 API와 weight 인자는 kernel version을 확인한다.
    netif_napi_add(dev, &adapter->napi, my_poll);
}

// Hard IRQ (위에서 본 것)
static irqreturn_t my_intr(int irq, void *data) {
    disable_irq_nosync(irq);
    napi_schedule(&adapter->napi);
    return IRQ_HANDLED;
}

// Soft IRQ 폴링
static int my_poll(struct napi_struct *napi, int budget) {
    int work_done = 0;

    while (work_done < budget) {
        struct sk_buff *skb = receive_packet();
        if (!skb)
            break;

        napi_gro_receive(napi, skb);  // GRO (Generic Receive Offload)
        work_done++;
    }

    if (work_done < budget) {
        napi_complete_done(napi, work_done);
        enable_irq(adapter->irq);
    }

    return work_done;
}
```

### GRO: Generic Receive Offload

**목적**: 여러 패킷을 하나로 합쳐 상위 계층 오버헤드 감소.

```c
// 개념 코드: 실제 GRO는 protocol별 callback, flow hash와 flush 조건을 사용한다.
gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb) {
    skb_list_del_init(skb);

    // 1. 같은 플로우의 패킷 찾기
    struct sk_buff *p = napi->gro_list;
    for (; p; p = p->next) {
        if (NAPI_GRO_CB(p)->same_flow) {
            // 2. 병합
            skb_gro_pull(skb, skb_gro_offset(skb));
            skb_shinfo(p)->frag_list = skb;
            return GRO_MERGED;
        }
    }

    // 3. 새 플로우면 리스트에 추가
    NAPI_GRO_CB(skb)->count = 1;
    napi->gro_list = skb;
    return GRO_NORMAL;
}
```

GRO가 합칠 수 있는 수는 flow, header option, checksum, 순서, 최대 크기와 flush 경계에 달려 있다. 패킷 1,000개가 10개로 합쳐진다는 고정 비율도, 그 결과 CPU가 90% 감소한다는 보장도 없다. 비교할 때는 wire packet rate, `rx_gro_packets`·`rx_gro_merged`, 상위 stack 호출 수, CPU time, latency와 drop을 함께 기록한다. 작은 UDP datagram이나 서로 다른 flow처럼 병합할 수 없는 traffic에서는 효과가 작다.

**확인:**
```bash
$ ethtool -k eth0 | grep generic-receive-offload
generic-receive-offload: on

# 통계
$ ethtool -S eth0 | grep gro
rx_gro_packets: 5423891
rx_gro_merged: 48815023  # 병합된 패킷 수
```

### Busy Polling

**극저지연 응용 (HFT 등)**: Soft IRQ 지연도 못 참음.

**해결**: 애플리케이션이 직접 폴링.

```c
// 소켓 옵션 설정 (값의 단위는 마이크로초)
int sock = socket(AF_INET, SOCK_STREAM, 0);
int busy_poll_us = 50;  // 50µs 동안 폴링
setsockopt(sock, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));

// 또는 시스템 전체
sysctl -w net.core.busy_poll=50  # 50us 동안 폴링
```

**커널 코드:**
```c
// net/core/sock.c
int sk_busy_loop(struct sock *sk, int nonblock) {
    unsigned long end_time = busy_loop_end_time();

    do {
        napi_busy_loop(sk->sk_napi_id);  // NIC 직접 폴링

        if (sk_has_rx_data(sk))  // 데이터 도착 확인
            break;
    } while (!time_after(jiffies, end_time));
}
```

## 9. 실전 디버깅

### 사례 1: 패킷 드롭 추적

**증상:**
```bash
$ netstat -s | grep drop
    12456 packets pruned from receive queue  # 수신 큐 드롭
    8923 packets dropped from out-of-order queue
```

**원인 찾기:**
```bash
# 1. 인터페이스 통계
$ ethtool -S eth0 | grep -i drop
rx_dropped: 1245
tx_dropped: 0

# 2. 소켓 버퍼 확인
$ ss -tm
State   Recv-Q  Send-Q  Local:Port   Peer:Port
ESTAB   524288  0       10.0.0.1:22  10.0.0.2:54321
        skmem:(r524288,rb524288,t0,tb87380,...)  # r=사용, rb=최대

# 3. 증가시키기
sysctl -w net.ipv4.tcp_rmem="8192 262144 16777216"
```

### 사례 2: 혼잡 제어 튜닝

**증상**: 고대역폭 네트워크에서 속도 안 남.

```bash
# 1. BDP (Bandwidth-Delay Product) 계산
# 대역폭: 1Gbps, RTT: 100ms
# BDP = 1,000,000,000 bps * 0.1 s / 8 = 12.5 MB

# 2. 버퍼 크기 증가
sysctl -w net.ipv4.tcp_rmem="4096 87380 16777216"  # 16MB
sysctl -w net.ipv4.tcp_wmem="4096 65536 16777216"

# 3. 윈도우 스케일 활성화 (기본값)
sysctl net.ipv4.tcp_window_scaling
# 1

# 4. BBR 사용
sysctl -w net.ipv4.tcp_congestion_control=bbr
```

### 사례 3: TIME_WAIT 폭증

**증상:**
```bash
$ ss -s
Total: 2000 (kernel 2145)
TCP:   50000 (estab 100, closed 49800, orphaned 0, synrecv 0, timewait 49800/0)
```

**해결:**
```bash
# 1. 서버: SO_REUSEADDR로 재바인딩 허용 (TIME_WAIT을 즉시 회수하지는 않음)
int opt = 1;
setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

# 2. 클라이언트: 포트 범위 확대
sysctl -w net.ipv4.ip_local_port_range="10000 65535"

# 3. TIME_WAIT 재사용 (클라이언트만 안전)
sysctl -w net.ipv4.tcp_tw_reuse=1

# 4. Connection Pooling (애플리케이션 레벨)
```


---

## 결론

리눅스 네트워크 스택은 다음 목표를 위해 설계되었다.

1. **성능**: NAPI, GRO, TSO로 수백만 pps 처리
2. **확장성**: RPS/RFS 같은 멀티코어 확장
3. **유연성**: Netfilter, QoS로 다양한 정책 지원
4. **신뢰성**: TCP 흐름/혼잡 제어로 네트워크 적응
5. **제로카피**: sk_buff와 sendfile로 CPU 절약

패킷 하나의 여정을 이해하면 네트워크 성능 문제를 커널 레벨에서 분석하고 튜닝할 수 있다. `/proc/net`, `ss`, `ethtool` 같은 도구는 이 복잡한 시스템을 관찰하는 창이다.

## 더 읽을거리

- Benvenuti, C. (2005). *Understanding Linux Network Internals*. O'Reilly.
- Salim, J., Olsson, R., & Kuznetsov, A. (2001). *Beyond Softnet*. Proceedings of the 5th Annual Linux Showcase and Conference.
- Linux kernel source tree: `net/` directory
 - `net/core/dev.c` - 네트워크 디바이스 코어
 - `net/ipv4/tcp.c` - TCP 프로토콜
 - `net/ipv4/ip_input.c` - IP 수신 경로
 - `net/netfilter/` - Netfilter 구현
- Documentation:
 - `Documentation/networking/` in kernel source
 - `Documentation/networking/scaling.rst` - 멀티코어 확장
- Cardwell, N., Cheng, Y., Gunn, C. S., Yeganeh, S. H., & Jacobson, V. (2016). *BBR: Congestion-Based Congestion Control*. ACM Queue.
- Jacobson, V. (1988). *Congestion Avoidance and Control*. ACM SIGCOMM.
