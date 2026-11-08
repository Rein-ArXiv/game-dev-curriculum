## 들어가며

거대한 월드를 다룬다는 것은 파일을 비동기로 읽는다는 뜻만이 아니다. 플레이어 주변의 어떤 셀이 존재해야 하는지 결정하고, 요청된 셀이 로드 중인지 활성 상태인지 구분하며, 이동 중인 엔티티의 소유 셀을 바꾸고, 메모리 압력에서 무엇을 퇴거할지 선택하고, 종료나 크래시 뒤에도 저장 상태를 일관되게 복원해야 한다.

에셋 스트리밍과 월드 스트리밍은 서로 연결되지만 같은 문제가 아니다.

- 에셋 시스템은 texture, mesh, audio 같은 *공유 바이트와 GPU 자원* 의 수명을 관리한다.
- 월드 시스템은 cell, entity, component 같은 *게임 상태와 공간 소유권* 의 수명을 관리한다.

한 월드 셀이 활성화되려면 필요한 에셋이 준비되어야 하지만, 에셋이 준비되었다고 셀의 게임플레이 상태를 즉시 게시해서는 안 된다. 로딩 결과를 검증하고, 메인 수명 경계에서 entity handle을 발급하며, 물리·렌더·AI 인덱스에 원자적으로 등록해야 한다.

이 글은 월드 partition, 셀 상태 기계, 비동기 준비와 commit, 메모리 예산, 안정적 ID, 저장 스키마, 원자적 저장, 장애 주입을 하나의 일관성 모델로 정리한다.

---

## 목차

1. [월드 partition과 공간 정체성](#1-월드-partition과-공간-정체성)
2. [셀 수명 상태 기계](#2-셀-수명-상태-기계)
3. [비동기 준비와 원자적 활성화](#3-비동기-준비와-원자적-활성화)
4. [의존성과 안정적 참조](#4-의존성과-안정적-참조)
5. [메모리 예산과 퇴거 정책](#5-메모리-예산과-퇴거-정책)
6. [경계·원점·이동 엔티티](#6-경계원점이동-엔티티)
7. [저장 스키마와 버전 진화](#7-저장-스키마와-버전-진화)
8. [원자적 저장과 크래시 복구](#8-원자적-저장과-크래시-복구)
9. [권위 상태와 복제 경계](#9-권위-상태와-복제-경계)
10. [검증과 계측](#10-검증과-계측)

---

## 1. 월드 partition과 공간 정체성

월드를 고정 크기 정사각형 셀로 나누면 2차원 위치 $(x,z)$의 셀 좌표는 다음과 같다.

$$
c_x=\left\lfloor\frac{x}{s}\right\rfloor,\qquad
c_z=\left\lfloor\frac{z}{s}\right\rfloor
$$

$s$는 셀 한 변의 길이다. 음수 좌표에서는 C++ 정수 나눗셈이 0 방향으로 절단되므로 단순 cast를 쓰면 안 된다. `floor` 의미를 명시해야 원점 왼쪽과 오른쪽의 셀 경계가 대칭이다.

```cpp
CellCoord WorldToCell(Vec3 position, double cellSize) {
    return {
        (int64_t)std::floor(position.x / cellSize),
        (int64_t)std::floor(position.z / cellSize),
    };
}
```

셀 크기는 성능 다이얼이다.

| 작은 셀 | 큰 셀 |
|---|---|
| 관심 영역이 정밀함 | 요청 수와 메타데이터가 적음 |
| 더 자주 경계를 넘음 | 한 셀 로드 spike가 큼 |
| 의존성과 seam이 많음 | 불필요한 객체까지 상주 |
| 병렬 작업을 잘게 나눌 수 있음 | 퇴거 단위가 거침 |

셀 ID는 좌표에서 결정적으로 만들어야 저장과 네트워크에서 재현할 수 있다. 프로세스 실행 중의 vector index를 영속 ID로 쓰면 재정렬·삭제 뒤 다른 셀을 가리킨다.

```cpp
struct CellId {
    int32_t layer;
    int64_t x;
    int64_t z;
};
```

`layer`는 실내·지하·인스턴스 공간처럼 같은 평면 좌표가 다른 월드를 나타내는 경우를 구분한다. hash collision은 ID 동일성이 아니므로 hash 값만 저장하지 말고 원래 좌표와 layer를 비교한다.

### 관심 집합

카메라 또는 플레이어 위치 $p$와 반경 $r$로 원형 관심 집합을 만들 수 있다.

$$
W=\{c\mid d(\operatorname{bounds}(c),p)\le r\}
$$

실제 시스템은 한 개 반경보다 여러 ring을 사용한다.

- simulation ring: AI와 물리를 완전히 실행
- presentation ring: 저주기 animation과 렌더 proxy 유지
- preload ring: 파일 읽기와 역직렬화만 수행
- retention ring: 방금 벗어난 셀을 잠시 유지해 thrashing 방지

ring마다 상태와 품질이 다르므로 “loaded” boolean 하나로 표현하지 않는다.

## 2. 셀 수명 상태 기계

셀의 대표 상태는 다음과 같다.

```cpp
enum class CellState : uint8_t {
    Unloaded,
    Requested,
    Reading,
    Decoding,
    Prepared,
    Activating,
    Active,
    Deactivating,
    Evictable,
    Failed,
};
```

상태 전이는 원인과 guard를 갖는다.

| 현재 | 사건 | 조건 | 결과 |
|---|---|---|---|
| Unloaded | request | 예산 예약 성공 | Requested |
| Requested | I/O start | 취소되지 않음 | Reading |
| Reading | bytes complete | checksum·길이 유효 | Decoding |
| Decoding | prepare complete | 의존성 준비 | Prepared |
| Prepared | commit point | 월드 generation 일치 | Activating |
| Activating | index 등록 완료 | 불변식 통과 | Active |
| Active | 관심 집합 이탈 | pin 없음 | Deactivating |
| Deactivating | snapshot drain | 외부 참조 없음 | Evictable |
| Evictable | budget pressure | dirty 저장 완료 | Unloaded |

모든 상태에서 취소가 같은 의미는 아니다. `Requested`는 큐에서 제거할 수 있지만 OS I/O가 이미 진행 중이면 실제 읽기는 끝날 수 있다. 이 경우 결과를 게시하지 않는 논리적 취소가 필요하다.

```cpp
struct CellRequest {
    CellId id;
    uint64_t worldGeneration;
    uint64_t requestGeneration;
    Priority priority;
    CancellationToken cancellation;
};
```

완료 callback은 `worldGeneration`과 `requestGeneration`을 확인한다. 플레이어가 빠르게 왕복해 같은 셀을 다시 요청했더라도 오래된 요청 결과가 새 요청을 덮어쓰면 안 된다.

### 상태 전이 불변식

- `Active` 셀만 gameplay query와 physics broad phase에 노출한다.
- `Prepared` 데이터는 worker 소유이며 live entity handle을 갖지 않는다.
- dirty 셀은 저장 성공 또는 명시적 폐기 정책 없이 `Unloaded`가 되지 않는다.
- 실패 셀은 무한 즉시 재시도하지 않고 backoff와 오류 원인을 기록한다.
- 한 `CellId`에 동시에 하나의 활성 generation만 존재한다.

## 3. 비동기 준비와 원자적 활성화

월드 셀 로딩은 세 구간으로 나눈다.

1. **I/O 구간**: 파일 범위를 읽고 압축 블록을 얻는다.
2. **준비 구간**: 역직렬화, 스키마 검증, 에셋 ID 수집, CPU 구조 생성.
3. **활성화 구간**: live handle 발급, component storage 삽입, 물리·렌더·AI 등록.

앞의 두 구간은 worker에서 실행할 수 있다. 활성화는 live world의 단일 writer 단계에서 commit해야 한다.

```cpp
struct PreparedCell {
    CellId id;
    uint64_t worldGeneration;    // 준비된 월드 epoch
    uint64_t requestGeneration;  // 어느 요청의 결과인가
    std::vector<EntityRecord> entities;
    std::vector<AssetHandle> pinnedAssets;
    CellBounds bounds;
    size_t residentBytes;
};
```

`PreparedCell`에는 live pointer를 넣지 않는다. component 안의 참조는 영속 entity ID나 local reference index로 유지하고 commit 중 실제 handle로 resolve한다.

```cpp
ActivationResult World::Activate(PreparedCell&& prepared) {
    if (prepared.worldGeneration != generation_)
        return ActivationResult::Stale;                            // 월드 전환
    if (prepared.requestGeneration != activeRequestGeneration_[prepared.id])
        return ActivationResult::Stale;                            // 재요청으로 대체
    if (!Validate(prepared))
        return ActivationResult::Invalid;

    ActivationJournal journal;
    journal.created.reserve(prepared.entities.size());             // 부작용 전에 용량 확보
    for (const EntityRecord& record : prepared.entities) {
        EntityHandle handle = entities_.Create(record.persistentId);   // strong guarantee
        journal.created.push_back(handle);                         // 재할당 없음
        if (!AttachComponents(handle, record, journal)) {
            journal.Rollback(*this);
            return ActivationResult::Failed;
        }
    }
    RegisterCellIndexes(prepared.id, journal.created);
    PublishCell(prepared.id, std::move(prepared.pinnedAssets), std::move(journal.created));
    return ActivationResult::Committed;
}
```

`Validate()`는 live pointer가 없는지, entity record의 persistent ID와 스키마 버전이 요청과 맞는지, `CellBounds`와 `residentBytes`가 상한 안인지를 검사한다. `worldGeneration`은 월드 전환 epoch이고 `requestGeneration`은 같은 월드에서 같은 셀을 다시 요청할 때마다 증가하는 토큰이므로, 완료 callback뿐 아니라 commit 직전에도 현재 활성 요청과 다시 비교한다.

중간 실패에서 일부 entity만 남지 않게 activation journal로 rollback한다. 스니펫은 부작용 전에 journal 용량을 확보하므로 `push_back`이 재할당으로 실패하지 않고, `entities_.Create`는 실패 시 아무것도 만들지 않는 strong guarantee를, `Commit`·`Destroy`는 실패하지 않는 no-fail 계약을 갖는다. 더 단순한 방법은 필요한 용량을 사전 예약하고 모든 검증을 먼저 끝내 commit 구간이 실패하지 않도록 만드는 것이다.

`PreparedCell::pinnedAssets`는 성공 commit에서 `PublishCell`로 이동해 활성 셀이 소유한다. 실패·stale 반환 시에는 `prepared`의 소유권이 호출자에게 남아 pin을 회수하며, entity component가 pin을 따로 소유하는 경우에도 그 획득을 commit 안에서 명시한다.

### 프레임 예산 안의 commit

Prepared 셀이 한꺼번에 수만 entity를 만들면 worker에서 준비했어도 메인 commit이 spike를 만든다. activation 비용을 budgeted batch로 나눈다.

$$
N_{\text{activate/frame}}
=\min\left(N_{\text{remaining}},
\left\lfloor\frac{B_{\text{activation}}}{\bar C_{\text{entity}}}\right\rfloor\right)
$$

$\bar C_{\text{entity}}$는 계획값이므로 이 식만으로 hard frame budget을 보장할 수 없다. attachment 비용 분산과 고정 등록비, 할당 spike를 고려해 측정된 deadline으로 batch를 중단하거나 보수적 상한을 사용한다. staging 중 live query 차단과 구획별 원자적 subcell commit 원칙은 그대로 유지한다.

부분 활성화 중인 셀은 외부 query에 보이지 않게 하거나, 구획별로 일관된 subcell commit을 정의해야 한다. “절반 생성된 Active 셀”은 피한다.

## 4. 의존성과 안정적 참조

한 셀은 mesh, material, collision shape, nav tile, script class 같은 의존성을 가진다. 의존성은 강한 것과 약한 것으로 나눈다.

- 강한 의존성: 없으면 셀 불변식이 깨짐. 활성화 전에 준비되어야 함.
- 약한 의존성: placeholder나 낮은 품질로 대체 가능.

에셋 handle과 entity handle을 혼동하지 않는다. 에셋은 여러 셀이 공유하고 content hash나 asset ID로 식별한다. entity는 월드 generation과 수명을 가진다.

### 셀 경계 참조

셀 A의 문이 셀 B의 스위치를 가리킬 수 있다. B가 unloaded이면 raw handle을 저장할 수 없다.

```cpp
struct PersistentEntityRef {
    WorldId world;
    EntityGuid entity;
};
```

resolve 결과는 세 상태를 구분한다.

```cpp
enum class ResolveStatus {
    Available,
    KnownButUnloaded,
    Missing,
};
```

`KnownButUnloaded`를 `Missing`으로 취급하면 셀을 벗어날 때 quest 관계가 끊긴다. 반대로 참조 하나 때문에 먼 셀을 항상 pin하면 메모리 예산이 무너진다. 참조 의미에 따라 proxy, deferred command, pin 중 하나를 고른다.

### 고아 command

unloaded entity를 대상으로 한 command는 다음 정책 중 하나를 명시한다.

- drop: 시각 효과처럼 일시적이고 비권위적인 명령
- queue by persistent ID: quest flag, mailbox
- force load: 드물고 강한 일관성이 필요한 작업
- apply to compact record: full entity 없이 저장 가능한 상태 변경

무제한 deferred queue는 메모리 누수와 오래된 명령 재생을 만든다. TTL, deduplication key, 최대 용량이 필요하다.

## 5. 메모리 예산과 퇴거 정책

월드 스트리밍은 거리 정렬 문제가 아니라 admission control 문제다. 셀을 읽기 전에 예상 resident 비용을 예약하지 않으면 여러 완료가 동시에 도착해 예산을 초과한다.

```cpp
struct WorldBudget {
    size_t activeEntityBytes;
    size_t preparedBytes;
    size_t physicsBytes;
    size_t navigationBytes;
    size_t renderProxyBytes;
    size_t pendingIoBytes;
};
```

총 예산만 두면 한 계층이 전부 소비할 수 있다. category별 soft limit와 전체 hard limit를 함께 사용한다.

### 비용 추정과 실제 비용

파일 크기는 resident 비용이 아니다. 압축 1MB 셀이 역직렬화 후 8MB component와 20MB GPU resource를 요구할 수 있다. content build 단계에서 예상 비용 메타데이터를 만들고 런타임 실제값과 비교한다.

$$
E_{\text{error}}=
\frac{|M_{\text{actual}}-M_{\text{estimated}}|}
{\max(M_{\text{actual}},1)}
$$

추정 오차가 큰 asset type을 계측해 메타데이터 모델을 보정한다. 예약값은 추정치이므로 실제 decode·GPU 메모리가 더 크면 hard limit가 깨질 수 있다. 추가 예약이 실패하면 prepare를 중단하거나 축소하고, bounded decoding과 peak staging, 공유 에셋의 중복 집계, retired 메모리를 Part05의 예산 모델과 연결한다.

### 퇴거 점수

단순 LRU보다 거리, 마지막 사용, 재로드 비용, dirty 상태, pin을 함께 고려할 수 있다.

$$
\operatorname{score}(c)=
w_d D(c)+w_t T(c)-w_r R(c)-w_p P(c)-w_s S(c)
$$

- $D$: 관심 지점에서 거리
- $T$: 마지막 사용 이후 시간
- $R$: 재로드 비용
- $P$: pin 중요도
- $S$: 저장해야 할 dirty 비용

점수가 높을수록 퇴거하기 쉽다고 정의할 수 있다. 가중치는 장르와 이동 속도에 종속된다. 다만 pin과 미저장 dirty는 유한한 음의 가중치로 표현할 수 없는데, 매우 큰 거리·나이에서는 금지 후보가 최고 점수를 받을 수 있기 때문이다. 따라서 먼저 `eligible = (mandatory pin 없음) AND (clean OR durable save 완료)`로 후보를 필터링한 뒤 그 안에서 score를 적용한다. 적격 후보가 없으면 hard limit에서도 무리하게 퇴거하지 않고 admission을 거절하거나 품질을 낮춘다.

### 히스테리시스

로드 반경과 언로드 반경을 같게 두면 경계에서 왕복할 때 thrashing이 발생한다.

$$
r_{\text{unload}}>r_{\text{load}}
$$

retention time도 함께 사용한다. 다만 메모리 hard limit에서는 히스테리시스를 무시할 수 있어야 한다.

## 6. 경계·원점·이동 엔티티

셀 경계는 구현 세부가 아니라 gameplay 오류가 드러나는 곳이다.

### 중복과 틈

AABB 경계를 양쪽 모두 닫힌 구간으로 정의하면 경계 객체가 두 셀에 들어간다. 각 축을 반열린 구간으로 정의한다.

$$
[x_0,x_1)\times[z_0,z_1)
$$

넓은 객체는 소유 셀 하나와 여러 spatial proxy를 가질 수 있다. 객체 자체를 셀마다 복제하면 상태가 갈라진다.

### 이동 엔티티

플레이어, 투사체, 차량은 셀을 넘는다. source 셀에서 제거하고 destination 셀에 추가하는 작업은 한 commit 안에서 처리한다.

```cpp
struct CellMigration {
    EntityHandle entity;
    CellId from;
    CellId to;
};
```

destination 셀이 unloaded이면 선택이 필요하다.

- 이동을 제한한다.
- destination을 긴급 요청하고 경계 proxy에서 대기한다.
- 이동 엔티티를 셀 독립 global layer가 소유한다.

플레이어처럼 항상 존재해야 하는 객체는 global layer가 자연스럽다. 정적 장식물까지 global로 올리면 partition의 이점이 사라진다.

런타임에서 source 제거와 destination 추가를 한 tick commit 안에서 처리해도, 두 셀 파일을 각각 독립 저장하면 크래시 후 객체가 양쪽에 존재하거나 양쪽에서 사라질 수 있다. migration을 공통 journal/manifest sequence 트랜잭션으로 기록하거나 영속 entity 소유권을 별도 레코드로 관리하고, 두 dirty 셀의 checkpoint 세대를 묶는다.

### floating origin

큰 좌표에서 32비트 float 간격은 커진다. float의 유효 가수는 약 24비트이므로 값 $x$ 근처의 간격은 대략 다음 규모다.

$$
\operatorname{ulp}(x)\approx 2^{\lfloor\log_2|x|\rfloor-23}
$$

월드 좌표는 64비트 정수 셀 좌표와 셀 내부 float 위치로 나누고, 렌더·물리 원점을 플레이어 근처로 이동할 수 있다.

```cpp
struct WorldPosition {
    CellId cell;
    Vec3 local;
};
```

원점 이동은 모든 transform에 임의로 delta를 더하는 작업이 아니다. physics broad phase, particles, trails, camera history, temporal rendering history에 discontinuity를 알려야 한다.

## 7. 저장 스키마와 버전 진화

저장 파일은 메모리 dump가 아니다. padding, pointer, container layout, endian, compiler ABI는 영속 계약이 될 수 없다.

저장 레코드는 안정적 필드 ID와 명시적 숫자 표현을 사용한다.

```cpp
struct SaveHeader {
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t endianMarker;
    uint64_t buildSchemaHash;
    uint64_t worldId;
    uint64_t sequence;
};
```

### ID 계층

| ID | 수명 |
|---|---|
| runtime handle | 한 실행·한 generation |
| persistent entity GUID | 저장과 로드 사이 |
| asset ID/content hash | 빌드와 패치 사이 |
| player/account ID | 서비스 권위 저장소 |

runtime handle을 그대로 저장하지 않는다. 로드 시 persistent ID에서 새 handle로 매핑한다.

### 스키마 진화

필드 추가는 기본값으로 처리할 수 있지만 의미 변경은 migration이 필요하다.

```cpp
SaveRecord MigrateV3ToV4(const SaveRecordV3& old) {
    SaveRecord current;
    current.health = Clamp(old.health, 0, old.maxHealth);
    current.stamina = DefaultStaminaFor(old.classId);
    current.inventory = ConvertLegacyInventory(old.items);
    return current;
}
```

migration은 연쇄 적용하거나 지원 가능한 최소 버전에서 현재 버전으로 직접 변환한다. 어떤 방식을 택하든 golden save fixture를 보존해 실제 과거 파일을 테스트해야 한다.

알 수 없는 필드를 버릴지 보존할지도 정책이다. 모드나 확장 데이터가 왕복해야 한다면 unknown field를 opaque blob으로 유지할 수 있지만 메모리와 보안 검증 비용이 든다.

### 결정적 저장

동일 상태가 동일 바이트를 만들어야 diff, checksum, 재현이 쉬워진다.

- map key와 entity를 안정적 순서로 정렬
- NaN과 음의 0 정규화
- endian 고정
- 압축 전 canonical serialization
- timestamp 같은 비결정 메타데이터를 payload hash에서 분리

## 8. 원자적 저장과 크래시 복구

저장 중 프로세스가 죽어도 이전 정상 save와 새 정상 save 중 하나를 선택할 수 있어야 한다. 기존 파일을 제자리에서 덮어쓰면 중간 상태만 남을 수 있다.

일반적인 copy-on-write 경로는 다음과 같다.

1. 새 내용을 임시 파일에 쓴다.
2. 파일 내용을 flush하고 필요한 내구성 경계를 요청한다.
3. 전체 길이와 checksum을 다시 검증한다.
4. 임시 파일을 최종 이름으로 원자적 교체한다.
5. 디렉터리 항목의 내구성이 필요하면 디렉터리도 동기화한다.

파일 시스템과 플랫폼마다 rename·replace·flush 보장이 다르므로 API 이름만 보고 전원 손실 원자성을 가정하지 않는다.

```text
slot_A.save  sequence=40 valid
slot_B.tmp   sequence=41 writing
manifest     active=A
```

시작 시 모든 후보의 header, 길이, checksum을 검증하고 가장 높은 *유효한* sequence를 선택한다. manifest만 믿으면 manifest가 먼저 갱신되고 payload가 손상된 경우 복구할 수 없다. 유효 후보는 여기에 더해 world ID와 schema 버전이 현재와 맞고, 인증이 필요한 경우 인증을 통과하며, sequence가 단조 증가하는 경우로 한정한다. checksum만 맞는 다른 world나 지원하지 않는 save는 선택하지 않는다. 완전한 tmp가 manifest 게시 전에 존재할 수 있으므로, 그 tmp를 후보로 삼을 때의 commit/ack 시점과 `NewCommittedState`의 의미도 정한다.

파일 교체 자체가 원자적이어도 저장 대상 상태가 논리적으로 일관되지 않으면 소용이 없다. live world를 순회하며 서로 다른 tick의 필드를 읽으면 파일이 완전히 쓰이고 checksum이 맞아도 불가능한 혼합 상태가 저장된다. tick commit에서 immutable save snapshot을 만들거나 MVCC/copy-on-write epoch를 고정하고, 저널 복구의 replay 시작 sequence와 snapshot cut, log 절단 순서를 함께 기록한다.

### 저널과 스냅샷

변경이 잦은 월드에서는 전체 save를 매번 쓰지 않고 snapshot과 journal을 결합한다.

```text
snapshot(sequence=100)
log: 101 quest update
log: 102 inventory transaction
log: 103 cell state delta
```

복구는 snapshot을 읽고 checksum이 유효한 log record의 연속 prefix만 적용한다. log가 무한히 길어지지 않게 새 snapshot을 만들고 성공적으로 교체한 뒤 오래된 log를 절단한다.

### 게임 경제 상태

로컬 save의 원자적 교체와 서버 경제 transaction은 같은 문제가 아니다. 서버 권위 화폐는 DB의 transaction, idempotency key, 감사 log가 필요하다. 로컬 파일 저장만으로 중복 지급과 다중 장치 충돌을 해결할 수 없다.

### 보안 경계

길이, count, offset은 모두 검증한다.

$$
0\le\text{offset}\le\text{fileSize},\qquad
\text{count}\times\text{elementSize}\le\text{remaining}
$$

곱셈 overflow를 피하려면 `count <= remaining / elementSize` 형태로 검사한다. 이 나눗셈은 `elementSize > 0`일 때만 유효하므로 상수 크기의 양수 전제를 먼저 둔다. 또한 범위가 유효한 압축 입력도 과도한 출력·중첩·객체 수를 요구할 수 있으므로 `decodedBytes`, `entityCount`, `recursionDepth` 상한을 함께 둔다. checksum은 우발적 손상 검출이며 변조 방지가 필요하면 인증된 MAC이나 서명을 사용한다.

## 9. 권위 상태와 복제 경계

멀티플레이 월드에서 “클라이언트 셀이 unloaded”와 “서버 entity가 존재하지 않음”은 다르다. 서버가 권위 상태를 소유하고 클라이언트는 관심 영역에 필요한 presentation·prediction 상태만 materialize할 수 있다.

셀 ID는 interest management의 공간 bucket으로 사용할 수 있지만 저장 partition과 네트워크 partition을 반드시 같게 할 필요는 없다.

- 저장은 제작·패치 단위가 중요하다.
- simulation은 물리·AI 상호작용 범위가 중요하다.
- 네트워크는 플레이어별 relevancy와 대역폭이 중요하다.

하나의 grid를 모든 문제에 강제하면 셀 크기 요구가 충돌한다.

### 진입과 이탈

클라이언트가 관심 영역에 들어올 때 서버는 spawn snapshot과 기준 sequence를 보낸다. 그 뒤 delta가 기준보다 먼저 도착할 수 있으므로 baseline이 준비될 때까지 보류하거나 폐기 규칙을 둔다.

관심 영역에서 이탈했다고 persistent entity가 파괴된 것은 아니다. 클라이언트 proxy만 제거하고 서버 ID와 despawn reason을 구분한다.

```cpp
enum class DespawnReason {
    Destroyed,
    OutOfInterest,
    WorldTransition,
};
```

이 구분이 없으면 다시 관심 영역에 들어온 같은 entity를 새 객체로 오해하거나 파괴 효과를 잘못 재생한다.

## 10. 검증과 계측

월드 스트리밍은 정상 경로보다 경계와 취소에서 깨진다.

### 상태 기계 테스트

- 허용되지 않은 전이를 모두 거부한다.
- 오래된 request generation의 완료를 게시하지 않는다.
- Prepared 실패가 live registry에 흔적을 남기지 않는다.
- dirty 셀은 저장 없이 퇴거되지 않는다.
- 동일 `CellId`의 두 generation이 동시에 Active가 되지 않는다.

### 이동 테스트

- 음수 좌표의 셀 변환
- 경계 정확히 위의 반열린 구간
- 한 프레임에 여러 셀을 통과하는 빠른 객체
- unloaded destination 진입
- floating-origin 이동 중 physics·camera history reset

### 장애 주입

I/O를 모든 바이트 경계에서 자르고, 역직렬화 단계별 실패를 주입하며, activation 도중 allocation 실패를 만든다. 저장은 임시 파일 쓰기, flush, rename, manifest 갱신의 각 지점에서 중단한다.

검증할 핵심 속성은 다음과 같다.

$$
\text{Recover}(F_k)\in
\{\text{LastCommittedState},\text{NewCommittedState}\}
$$

$F_k$는 저장 절차의 임의 중단 지점이다. 복구 결과가 둘 사이의 부분 상태가 되어서는 안 된다.

### 계측 채널

| 채널 | 진단 질문 |
|---|---|
| requested/reading/prepared/active 셀 수 | 병목 단계가 어디인가 |
| request-to-active P50/P95/P99 | 이동 속도를 따라갈 수 있는가 |
| activation ms/frame | 메인 스레드 spike가 있는가 |
| estimated/actual resident bytes | 예산 추정이 맞는가 |
| eviction·reload rate | thrashing이 있는가 |
| stale completion count | 취소와 왕복이 얼마나 잦은가 |
| dirty bytes·save latency | 퇴거가 저장에 막히는가 |
| unresolved persistent refs | 셀 경계 관계가 깨지는가 |
| fallback proxy duration | 낮은 품질 상태가 얼마나 보이는가 |

평균 load time만 보면 빠른 이동 중 발생하는 긴 꼬리를 놓친다. 플레이어 속도 $v$, preload 거리 $d$, P99 준비 시간 $L_{99}$에 대해 다음 관계를 설계 목표로 삼는다.

$$
\frac{d}{v}>L_{99}+M
$$

$M$은 activation, scheduling jitter, I/O 변동을 위한 여유다. 이 관계는 주어진 속도와 지연 분포에서의 설계 목표이며, 만족해도 1% 꼬리·teleport·속도 변동에서 실패할 수 있고 어겨도 placeholder나 이동 제한으로 stutter를 피할 수 있다. 따라서 실패 빈도, fallback duration, 연속 구간의 상관을 함께 측정한다.

---

## 결론

월드 스트리밍은 거리 안의 파일을 읽는 기능이 아니라 공간 정체성, 셀 수명 상태 기계, 비동기 준비, 원자적 활성화, 메모리 admission, 안정적 참조, 저장 복구를 묶은 일관성 문제다. 에셋 바이트의 준비와 live 월드 상태의 게시를 분리해야 worker 완료가 권위 상태를 임의 시점에 바꾸지 않는다.

좋은 스트리밍 시스템은 빠른 정상 로드뿐 아니라 오래된 완료, 경계 왕복, 예산 초과, 부분 activation, 저장 중단에서도 불변식을 유지한다. 이를 위해 상태 전이와 commit 지점을 코드로 표현하고, 장애 주입과 긴 꼬리 계측으로 검증해야 한다.

## 더 읽을거리

- Jason Gregory, *Game Engine Architecture*, 3rd ed. — 월드 chunk, 런타임 객체, 엔진 수명 구조
- Christer Ericson, *Real-Time Collision Detection* — 공간 분할, AABB, 계층 구조
- Martin Kleppmann, *Designing Data-Intensive Applications* — 로그, 스냅샷, 복구, 식별자
- SQLite, [Atomic Commit In SQLite](https://www.sqlite.org/atomiccommit.html) — 파일 시스템 flush·journal·원자적 commit 모델
- POSIX.1, `rename`, `fsync` 명세 — 파일과 디렉터리 갱신의 플랫폼 계약
- Thanumalayan Sankaranarayana Pillai et al., “All File Systems Are Not Created Equal” (OSDI 2014) — 파일 시스템별 crash consistency 차이
