# 19. 오래된 두 가정

## 들어가며

이 편은 서로 다른 두 곳을 고친다. 공통점은 **오래전에 참이었던 가정이 언젠가 거짓이 됐는데 아무도 알아채지 못했다**는 것이다.

**첫 번째 가정 — 리소스는 프로세스만큼 산다.** 1편의 `DefenseSkin` 은 스프라이트 열 장을 값으로 들었다. 시작할 때 한 번 읽고 그대로 유지하니 그 소유로 충분했다. 12편이 매치 중 리로드를 넣으면서 그 전제가 깨졌다 — 리로드는 열 장을 통째로 덮어쓴다. 옛 픽셀을 가리키던 참조가 있었다면 그것은 이미 사라진 메모리다. 값 소유의 문제는 그 사고가 **일어난다**는 것이 아니라, 그 사고를 **말할 수단조차 없다**는 것이다. 옛 스프라이트에는 이름이 없다.

**두 번째 가정 — 소리에는 위치가 없다.** 1편의 `AudioDevice::Play(Sfx)` 는 미리 합성한 톤을 전음량 모노로 낸다. 화면이 480x480 고정이고 맵 전체가 한눈에 들어오던 시절에는 그것이 맞았다. 14편이 카메라를 넣으면서 화면은 팬·줌하게 됐는데 소리는 그대로였다. 맵 반대편의 발파와 눈앞의 총성이 같은 크기로, 같은 방향에서 난다.

두 헤더가 그 자리를 가져간다.

| 헤더 | 하는 일 |
|---|---|
| `engine/resource.hpp` | 세대(generation) 핸들 + 레퍼런스 카운트 리소스 풀 |
| `engine/audio.hpp` | 거리 감쇠 · equal-power 패닝 · 다중 보이스 합성 |

도달점의 수치.

- 리로드 두 번 뒤 **인덱스가 재사용된 슬롯**을 옛 핸들이 여전히 거절한다(index=1, 세대 1 → 3).
- 참조를 잡고 있으면 옛 픽셀이 살아남는다: 리로드 뒤 살아 있는 리소스 **11개**(새 10 + 붙잡힌 1), 참조를 놓으면 **10개**.
- 시뮬이 한 프레임에 낸 소리 분포: 1개 **17프레임**, 2개 **37**, 3개 **5**, 4개 **2** — **72%의 프레임에서 소리가 겹친다.**
- 좌우 게인: 왼쪽 소스 `0.262/0.083`, 오른쪽 `0.083/0.262`, 정면 `0.245/0.245`. 카메라를 반 바퀴 돌리면 같은 소리가 **반대쪽**에서 난다.
- 보이스 32개 x 9922프레임 합성 **175µs**, 64개를 넣어도 **32개만** 섞인다.

---

## 목차
1. [값 소유가 표현하지 못하는 사고](#1-값-소유가-표현하지-못하는-사고)
2. [세대 — 인덱스가 되살아난 뒤에도 거절한다](#2-세대--인덱스가-되살아난-뒤에도-거절한다)
3. [레퍼런스 카운트 — 교체 중에도 살아 있다](#3-레퍼런스-카운트--교체-중에도-살아-있다)
4. [상주와 대기 — 절반만 갈린 스킨을 그리지 않는다](#4-상주와-대기--절반만-갈린-스킨을-그리지-않는다)
5. [소리가 위치를 갖는다](#5-소리가-위치를-갖는다)
6. [카메라가 청취자다](#6-카메라가-청취자다)
7. [waveOut 은 섞지 않는다](#7-waveout-은-섞지-않는다)
8. [계층 — 게인은 카메라 옆에서, 합성은 파형 옆에서](#8-계층--게인은-카메라-옆에서-합성은-파형-옆에서)
9. [equal-power 가 가르쳐 준 것 둘](#9-equal-power-가-가르쳐-준-것-둘)
10. [설계에서 조정한 것](#10-설계에서-조정한-것)
11. [변이 둘](#11-변이-둘)
12. [실행과 관측](#12-실행과-관측)

---

## 1. 값 소유가 표현하지 못하는 사고

12편의 비동기 리로더는 워커에서 파일을 읽고, 다 모인 프레임에 디코드해 반영한다. 반영은 이런 모양이었다.

```cpp
skin.BuildFallbacks();
skin.externalAssets = 0;
for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot) {
    // ...
    if (render::DecodePpmP3(text, decoded)) skin.Apply(slot, std::move(decoded));
}
```

`Apply` 는 배열 원소를 이동 대입한다. 그 순간 옛 `std::vector<uint32_t>` 픽셀 버퍼가 해제된다.

지금 이 코드가 안전한 이유는 **아무도 스프라이트 참조를 프레임 너머로 들고 있지 않기 때문**이다. `DrawGame` 이 `skin.Tower(index)` 를 받아 그 프레임 안에서 다 쓰고 버린다. 그것은 구조가 아니라 **우연**이고, 우연은 다음 편집에서 깨진다.

값 소유의 진짜 문제는 그 사고를 표현할 어휘가 없다는 것이다. "지금 그리고 있는 저 스프라이트"를 가리키는 이름이 `skin.towers[0]` 뿐이고, 그 이름은 리로드 뒤에 **다른 것**을 가리킨다. 옛 것과 새 것을 구분해서 말할 방법이 없으므로, "옛 것을 아직 쓰고 있다"는 문장도 쓸 수 없고 그 상태를 검사할 수도 없다.

`engine/resource.hpp` 가 그 어휘를 준다.

```cpp
struct Handle {
    uint32_t index      = 0xFFFFFFFFu;
    uint32_t generation = 0;
    bool Valid() const { return index != 0xFFFFFFFFu; }
};
```

인덱스와 **세대**. 슬롯이 회수되면 세대가 오르고, 옛 핸들은 인덱스가 같아도 세대가 달라 `IsValid` 에서 걸린다. 이제 "옛 것"에 이름이 있다.

## 2. 세대 — 인덱스가 되살아난 뒤에도 거절한다

세대가 하는 일을 단언하려면 **인덱스가 실제로 재사용된 뒤에** 물어야 한다. 리로드를 한 번만 하면 옛 핸들이 거절되는 이유가 "슬롯이 죽었다"인지 "세대가 다르다"인지 구분되지 않는다.

`tests/test_asset_handles.cpp` 는 리로드를 두 번 한다.

```cpp
const engine::Handle loose = skin.SlotHandle(1);   // 아무도 잡지 않는다
skin.Load(blue);                                    // 1회: loose 의 슬롯이 회수된다
skin.Load(red);                                     // 2회: 그 슬롯이 다시 쓰인다
bool indexReused = false;
uint32_t reusedGeneration = loose.generation;
for (int slot = 0; slot < DefenseSkin::kFileCount; ++slot) {
    const engine::Handle current = skin.SlotHandle(slot);
    if (current.index != loose.index) continue;
    indexReused = true;
    reusedGeneration = current.generation;
}
CHECK(indexReused);                        // 그 자리가 실제로 다시 쓰였다
CHECK(reusedGeneration != loose.generation);
CHECK(!skin.IsValid(loose));               // 그래도 옛 핸들은 거절된다
```

`indexReused` 를 먼저 확인하는 것이 요점이다. 인덱스가 재사용되지 않았다면 그 뒤의 `!IsValid` 는 세대와 무관한 사실이고, 그 테스트는 세대를 검사하지 않은 채 통과한다.

실측:

```
  재사용된 슬롯 index=1  옛 세대=1  새 세대=3
```

세대가 1에서 3으로 갔다 — 두 번의 회수. 그리고 그 자리를 지금 다른 스프라이트가 쓰고 있는데도 `loose` 는 `nullptr` 을 받는다.

**슬롯 인덱스가 어떻게 재사용되는가.** `ResourcePool` 의 `freeList_` 는 LIFO 다. 커밋은 새 것을 먼저 적재하고 옛 것을 나중에 해제하므로, 1회 리로드에서 옛 인덱스들이 자유 목록에 쌓이고 2회 리로드가 그것을 집어 든다. 그 순서가 이 테스트가 성립하는 조건이다.

## 3. 레퍼런스 카운트 — 교체 중에도 살아 있다

핸들만으로는 절반이다. 리로드 중에 옛 스프라이트를 **쓰고 있는** 코드가 있다면 거절이 아니라 생존이 필요하다.

```cpp
engine::Handle Acquire(int slot) {
    const engine::Handle handle = SlotHandle(slot);
    pool_.AddRef(handle);
    return handle;
}
```

슬롯 자체가 참조 하나를 들고 있으므로, `Acquire` 뒤의 참조 수는 2다. 커밋이 옛 핸들을 `Release` 하면 1로 내려가고 슬롯은 회수되지 않는다.

```cpp
const engine::Handle held = skin.Acquire(0);
CHECK_EQ(skin.RefCount(held), 2);
const uint32_t redTower0 = FirstPixel(skin.Tower(0));

skin.Load(blue);
CHECK(FirstPixel(skin.Tower(0)) != redTower0);        // 슬롯은 새 픽셀
CHECK(skin.IsValid(held));                            // 옛 핸들은 유효
CHECK_EQ(FirstPixel(*skin.Resolve(held)), redTower0); // 그리고 옛 픽셀을 본다
CHECK_EQ(skin.RefCount(held), 1);
CHECK_EQ(skin.AliveResources(), 11);                  // 새 10 + 붙잡힌 옛 1
```

마지막 참조를 놓는 순간에만 회수된다.

```cpp
skin.Release(held);
CHECK(!skin.IsValid(held));
CHECK_EQ(skin.AliveResources(), 10);
```

**같은 상황에서 아무도 잡지 않은 슬롯 1은 커밋 순간에 죽는다.** 두 결과가 같은 리로드 안에서 나온다는 것이 레퍼런스 카운트가 실제로 판정에 쓰인다는 증거다.

## 4. 상주와 대기 — 절반만 갈린 스킨을 그리지 않는다

리로드는 열 장을 갈아 끼운다. 그 열 번의 교체가 하나씩 즉시 반영되면, 그 사이 어느 프레임이 그려질 때 **절반은 옛 스킨, 절반은 새 스킨**인 상태가 화면에 나올 수 있다.

비동기 팔에서는 지금 그런 일이 없다 — `Poll` 이 열 장이 다 모인 뒤에만 반영하기 때문이다. 그러나 디코드는 실패할 수 있고(`DecodePpmP3` 가 false 를 돌려주면 그 슬롯만 폴백이 남는다), 실패가 섞이면 반영 도중의 상태가 관측 가능해진다.

그래서 반영을 셋으로 나눴다.

<!-- snippet: include/game/defense_view.hpp@skin-commit -->
```cpp
void BeginReload() { DrainPending(); }

void Stage(int slot, render::Sprite&& sprite) {
    if (slot < 0 || slot >= kFileCount) return;
    DropPending(slot);                       // 같은 슬롯을 두 번 스테이징
    const engine::Handle handle = pool_.Load(sprite);
    Node* node = NodeOf(handle);
    if (!node) { pool_.Release(handle); return; }
    node->handle = handle;
    node->slot = slot;
    pending_.PushBack(node);
}

void CommitReload() {
    while (Node* node = pending_.Front()) {
        pending_.Remove(node);
        const size_t slot = static_cast<size_t>(node->slot);
        if (Node* previous = residentOf_[slot]) {
            resident_.Remove(previous);
            pool_.Release(previous->handle);   // 마지막 참조면 여기서 회수된다
            previous->slot = -1;
        }
        live_[slot] = node->handle;
        residentOf_[slot] = node;
        resident_.PushBack(node);
    }
}
```

`resident_` 와 `pending_` 은 9편의 `IntrusiveList` 다. 노드가 두 훅(`residentHook`·`pendingHook`)을 갖고 두 목록에 번갈아 든다 — 같은 객체가 여러 목록에 동시에 들 수 있다는 것이 intrusive 리스트의 성질이고, 여기가 그 성질이 실제로 필요한 자리다.

노드 배열이 `std::array<Node, kMaxSlots>` 로 고정인 이유는 **주소 안정성**이다. `std::vector` 로 두면 재할당이 훅의 주소를 옮기고 목록이 통째로 끊어진다(9편의 발사체 풀이 같은 이유로 고정 블록 풀을 쓴다). 상한은 `kFileCount * 2 + 2` — 리로드 중에는 옛 열 장과 새 열 장이 동시에 살기 때문이다.

`BeginReload` 가 대기 목록을 **비운다**는 점도 계약이다. 프레임 중간에 리로드가 중단되면(요청은 걸렸는데 파일이 사라졌다든가) 스테이징이 남고, 남은 채로 다음 리로드를 시작하면 그때 커밋되어 두 세대가 섞인 스킨이 된다.

테스트가 그 셋을 각각 확인한다.

```cpp
const uint32_t visible = FirstPixel(skin.Tower(0));
skin.BeginReload();
skin.Stage(0, std::move(staged));
CHECK_EQ(skin.PendingCount(), 1);
CHECK_EQ(FirstPixel(skin.Tower(0)), visible);     // 커밋 전에는 화면이 그대로다
skin.BeginReload();                                // 중단
CHECK_EQ(skin.PendingCount(), 0);
CHECK_EQ(skin.AliveResources(), 10);               // 스테이징이 새지 않았다
```

## 5. 소리가 위치를 갖는다

여기서부터가 두 번째 가정이다.

소리가 방향을 가지려면 먼저 **어디서 났는가**를 시뮬이 말해야 한다. 그런데 시뮬은 카메라를 몰라야 한다 — 14편이 그 경계를 그었고, 그 경계가 "연출이 상태 해시를 흔들 수 없다"의 근거다.

그래서 시뮬은 **위치만** 낸다.

```cpp
enum class SoundKind : uint8_t { Fire, Kill, Blast, Stomp };
struct SoundEvent {
    math::Vec2 at{0, 0};
    SoundKind  kind = SoundKind::Fire;
};
```

`GameWorld` 가 네 자리에서 이것을 쌓는다 — 타워 발사, 적 처치, 발파, 스톰프. 그리고 **상태 해시에 넣지 않는다.** 넣으면 표시 계층이 시뮬을 바꿀 수 있게 되고, 그 순간 9편부터 쌓아 온 결정성 논증이 무너진다. 소리는 상태가 아니라 출력이다.

상한도 둔다.

```cpp
void Emit(SoundKind kind, const math::Vec2& at) {
    if (sounds_.size() >= kMaxSounds) return;
    sounds_.push_back({at, kind});
}
```

`kMaxSounds = 32` 는 믹서의 보이스 상한과 같은 수다. 상한이 필요한 이유는 **소비자가 없는 실행**이 있기 때문이다 — 테스트와 전용 서버는 `Sounds()` 를 읽지 않고, 상한이 없으면 이 벡터가 매치 길이만큼 자란다.

실측: 타워 8개와 무리 웨이브로 2000틱.

```
  시뮬 출력: 발사 74 · 처치 40 (소리 난 프레임 61, 최대 4)
  프레임당 소리 수: 1=17 2=37 3=5 4=2 5=0 6+=0
```

소리가 난 61프레임 중 **44프레임(72%)에서 둘 이상이 겹친다.** 이 분포가 §7의 결정을 정당화한다.

## 6. 카메라가 청취자다

<!-- snippet: include/game/sfx_bus.hpp@listener-from-camera -->
```cpp
inline engine::Listener ListenerFrom(const DefenseCamera& camera) {
    engine::Listener listener;
    if (!camera.Tilted()) {
        const math::Vec2 focus = camera.Focus();
        listener.pos = {focus.x, focus.y, kAudioListenerHeight};
        listener.right = {1.0f, 0.0f, 0.0f};
        return listener;
    }
    listener.pos = camera.Eye();
    // 우측 = 상방 x 전방. 순서를 뒤집으면 좌우가 통째로 바뀐다.
    const math::Vec3 up{0.0f, 0.0f, 1.0f};
    const math::Vec3 right = math::Cross(up, camera.Forward());
    listener.right = math::Length(right) > 1e-4f ? math::Normalize(right)
                                                 : math::Vec3{1.0f, 0.0f, 0.0f};
    return listener;
}
```

`ortho2d` 팔에는 눈 위치라는 것이 없다 — 항등 투영이라 카메라가 어디 있는지가 정의되지 않는다. 그래서 화면 중심 위 260픽셀에 떠 있다고 본다. 그 높이가 있어야 맵 양 끝의 소리가 다른 크기로 들린다(높이가 0이면 화면 중심의 소리만 거리 0이고 나머지는 전부 평면 거리 그대로다 — 그것도 틀린 것은 아니지만, 위에서 내려다보는 시점의 물리적 해석과 맞지 않는다).

`tilted3d` 팔에서는 카메라의 눈과 우측 기저를 그대로 쓴다. `Cross(up, forward)` 의 순서가 부호를 정하므로 뒤집으면 좌우가 통째로 바뀐다.

**그 관계를 가장 직접적으로 단언하는 것은 카메라를 돌리는 것이다.**

```cpp
tilted.Frame(map);                                   // yaw 0
bus.Collect({{{focus.x + 200.0f, focus.y}, SoundKind::Fire}}, tilted, tones);
CHECK(bus.Voices()[0].gainR > bus.Voices()[0].gainL);

tilted.SetYawPitch(180.0f, 58.0f);                   // 반 바퀴
bus.Collect({{{focus.x + 200.0f, focus.y}, SoundKind::Fire}}, tilted, tones);
CHECK(bus.Voices()[0].gainL > bus.Voices()[0].gainR);
```

같은 좌표의 같은 소리가 반대 채널에서 크게 난다. 이 한 줄이 "청취자가 카메라다"를 다른 어떤 단언보다 직접 말한다.

수집 자체는 이렇다.

<!-- snippet: include/game/sfx_bus.hpp@sfx-collect -->
```cpp
void Collect(const std::vector<SoundEvent>& events, const DefenseCamera& camera,
             const platform::SfxTones& tones) {
    voices_.clear();
    if (events.empty()) return;
    const engine::Listener listener = ListenerFrom(camera);
    for (const SoundEvent& event : events) {
        if (voices_.size() >= engine::kMaxVoices) { ++dropped_; continue; }
        SfxVoice voice;
        voice.at = event.at;
        voice.sfx = SfxFor(event.kind);
        engine::Source source;
        source.pos = {event.at.x, event.at.y, 0.0f};
        source.refDist = kAudioRefDistance;
        source.samples = tones.Samples(voice.sfx, source.count);
        engine::SourceGains(listener, source, voice.gainL, voice.gainR);
        voices_.push_back(voice);
    }
}
```

`refDist` 는 `engine::Source` 에 새로 생긴 필드다. 원래 `Attenuation` 의 기준 거리는 1로 고정이었는데, 단위가 픽셀인 게임에서 1이면 **두 칸만 떨어져도 소리가 사라진다**. 도메인이 그 값을 정해 주어야 한다는 것이 이 필드의 존재 이유이고, 이 게임은 `kAudioRefDistance = 90.0f` — 기본 맵의 세 칸이다.

## 7. waveOut 은 섞지 않는다

원래 `AudioDevice::Play` 는 이벤트마다 `WAVEHDR` 하나를 제출했다. 16개짜리 헤더 풀을 돌려 쓰며 fire-and-forget.

**그 코드는 소리를 겹쳐 재생하지 않는다.** `waveOut` 에 버퍼를 여럿 줄 세우면 그것들은 **차례로** 재생된다. 한 프레임에 총 넷이 나가면 총성 넷이 겹치는 대신 줄을 서고, 마지막 것은 0.28초 뒤에 난다.

§5의 분포가 그것이 실제로 일어나는 상황임을 말한다 — 소리가 난 프레임의 72%에서 둘 이상이 겹친다.

그래서 한 프레임의 보이스를 **미리 하나로 합성해** 제출한다.

<!-- snippet: include/game/sfx_bus.hpp@sfx-submit -->
```cpp
void Submit(platform::AudioDevice& device) {
    if (voices_.empty()) return;
    const platform::SfxTones& tones = device.Tones();
    mix_.clear();
    size_t frames = 0;
    for (const SfxVoice& voice : voices_) {
        engine::Voice mixed;
        mixed.samples = tones.Samples(voice.sfx, mixed.count);
        mixed.gainL = voice.gainL;
        mixed.gainR = voice.gainR;
        if (mixed.count > frames) frames = mixed.count;
        mix_.push_back(mixed);
    }
    if (frames == 0) return;
    left_.assign(frames, 0.0f);
    right_.assign(frames, 0.0f);
    engine::MixVoices(mix_.data(), mix_.size(), left_.data(), right_.data(), frames);
    block_.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        block_[i * 2]     = Quantize(left_[i]);
        block_[i * 2 + 1] = Quantize(right_[i]);
    }
    device.PlaySamples(block_.data(), frames);
    ++blocks_;
}
```

합성 본체는 엔진에 있다.

<!-- snippet: include/engine/audio.hpp@mix-voices -->
```cpp
inline size_t MixVoices(const Voice* voices, size_t count, float* outL, float* outR, size_t n) {
    for (size_t i = 0; i < n; ++i) { outL[i] = 0; outR[i] = 0; }
    const size_t mixed = count < kMaxVoices ? count : kMaxVoices;
    for (size_t v = 0; v < mixed; ++v) {
        const Voice& voice = voices[v];
        if (!voice.samples) continue;
        const size_t m = voice.count < n ? voice.count : n;
        for (size_t i = 0; i < m; ++i) {
            outL[i] += voice.samples[i] * voice.gainL;
            outR[i] += voice.samples[i] * voice.gainR;
        }
    }
    return mixed;
}
```

반환값이 **실제로 섞은 보이스 수**인 것은 상한이 조용히 일하지 않게 하기 위해서다. 32개를 넘겨도 32개만 섞이고, 그 사실을 호출자가 볼 수 있어야 한다.

합성 비용:

```
  보이스  1개 x 9922프레임 합성 = 18.0us (실제 섞인 보이스 1)
  보이스  8개 x 9922프레임 합성 = 54.2us (실제 섞인 보이스 8)
  보이스 32개 x 9922프레임 합성 = 175.0us (실제 섞인 보이스 32)
  보이스 64개 x 9922프레임 합성 = 201.8us (실제 섞인 보이스 32)
```

보이스 수에 대략 선형이고, **32에서 끊긴다** — 64개를 넣어도 32개분의 시간만 든다(201.8µs 는 32개의 175.0µs 와 같은 자리수이고, 차이는 64개짜리 배열을 훑는 비용이다). 상한이 없으면 이 수가 계속 자라고, 60Hz 예산 16.7ms 안에서 그것은 곧 프레임 하나다.

가장 긴 톤이 0.45초(9922 프레임)라 최악의 블록이 그것이다. 짧은 소리만 나는 프레임에서는 블록도 짧다.

`Quantize` 는 클리핑한다.

```cpp
static int16_t Quantize(float sample) {
    const float scaled = sample * 8000.0f;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<int16_t>(scaled);
}
```

32보이스가 몰리면 합이 1을 크게 넘는다. 클리핑은 소리를 뭉개지만 감싸는 것(wrap)은 **폭발음**을 낸다 — 최댓값에서 최솟값으로 튀는 파형이 만들어지기 때문이다. 둘 중에는 뭉개는 쪽이 옳다.

## 8. 계층 — 게인은 카메라 옆에서, 합성은 파형 옆에서

`engine/audio.hpp` 의 `MixStereo` 는 위치를 넣으면 파형이 나오는 함수다. 그것을 제품이 그대로 부를 수 없었다. 두 입력이 **다른 층에 있기** 때문이다.

- **게인**은 청취자가 필요하고, 청취자는 카메라다. 카메라는 `game/` 에 있다.
- **파형**은 장치가 합성한 톤이고, 그것은 `platform/` 에 있다.

`platform/` 이 `engine/` 을 인클루드하게 만들면 한 줄로 끝나지만, 그 방향은 이 저장소가 지금까지 쓰지 않은 방향이다 — `platform/*.hpp` 는 자기 폴더 밖의 헤더를 하나도 인클루드하지 않는다.

그래서 장치를 둘로 쪼갰다.

<!-- snippet: include/platform/audio_device.hpp@sfx-tones -->
```cpp
class SfxTones {
public:
    static constexpr int kRate = 22050;

    SfxTones() {
        Synth(Sfx::Hit,    200.0f, 0.07f, /*square*/ true,  0.0f);
        Synth(Sfx::Pickup, 700.0f, 0.10f, false,            900.0f);   // 상승음
        Synth(Sfx::Death,  120.0f, 0.45f, true,            -60.0f);    // 하강·길게
        Synth(Sfx::Win,    523.0f, 0.30f, false,            400.0f);   // 밝게 상승
        Synth(Sfx::Step,   90.0f,  0.03f, true,             0.0f);     // 짧은 클릭
    }

    const float* Samples(Sfx sfx, size_t& count) const {
        if (sfx == Sfx::Count) { count = 0; return nullptr; }
        const std::vector<float>& buffer = buf_[static_cast<int>(sfx)];
        count = buffer.size();
        return buffer.data();
    }
    // ...
};
```

`SfxTones` 는 **`#ifdef _WIN32` 바깥에** 있다. 합성은 순수 계산이고, 장치가 열리지 않았다는 이유로 그 계산이 다른 코드가 되면 헤드리스 검증이 제품과 다른 것을 재게 된다. 파형이 int16 이 아니라 float 인 것도 같은 이유다 — 합성이 float 도메인에서 일어나므로 양자화는 마지막 한 번이면 된다.

장치에 남은 것은 제출뿐이다.

```cpp
void PlaySamples(const int16_t* interleaved, size_t frames);
void Play(Sfx s);   // 위치 없는 소리(승패·배치 확인음)는 중앙·전음량
```

그리고 프레임 루프의 이음매:

<!-- snippet: include/game/defense_app.hpp@frame-audio -->
```cpp
sfx_.Collect(world_.Sounds(), camera_, sound_.Tones());
sfx_.Submit(sound_);
world_.ClearSounds();
```

카메라 레일(`camera_.UpdateRail`) **뒤에** 있다. 레일이 도는 프레임에서 청취자가 한 프레임 뒤처지지 않게 하려는 것이다.

`Collect` 와 `Submit` 이 나뉜 것은 헤드리스 검증 때문이다. 장치가 닫혀 있어도 `Collect` 는 똑같이 돌고, `app.Sfx().Voices()` 로 그 결과를 밖에서 읽을 수 있다. `tests/test_audio_wiring.cpp` 가 제품의 진짜 프레임 루프를 돌리면서 그 값을 검사한다.

```
  제품 프레임: 왼쪽 보이스 1 · 오른쪽 보이스 197 · 어긋남 0 · 블록 179
```

화면 왼쪽 절반의 타워에서 난 소리는 왼쪽이 크고, 오른쪽 절반의 타워는 오른쪽이 크다. 어긋난 것이 0이다.

(왼쪽 1 · 오른쪽 197 의 비대칭은 배치 탓이다. 경로가 오른쪽 타워 옆을 먼저 오래 지나고, 왼쪽 타워는 웨이브 후반에야 사거리에 든다. 검사는 "양쪽 모두 한 번 이상 나올 때까지" 돌므로 그 비대칭이 판정을 흐리지 않는다.)

## 9. equal-power 가 가르쳐 준 것 둘

**(a) "게인 합이 1을 넘지 않는다"는 성립하지 않는 단언이다.**

설계는 검사 항목에 그것을 적었다. 실제로는 equal-power 패닝의 정의가 `left² + right² = 1` 이고, 중앙(`pan = 0`)에서 좌우가 각각 `cos(π/4) = 0.707` 이라 합은 **1.414** 다. 합을 1로 묶으면 그것은 equal-power 가 아니라 linear 패닝이고, linear 패닝은 중앙에서 음량이 3dB 꺼진다.

그래서 단언을 에너지로 바꿨다.

```cpp
for (int step = -20; step <= 20; ++step) {
    const float pan = static_cast<float>(step) / 20.0f;
    PanGains(pan, l, r);
    CHECK(l >= 0.0f && r >= 0.0f);
    CHECK(Near(l * l + r * r, 1.0f, 1e-3f));
    CHECK(l + r <= 1.4143f);
}
```

`l² + r² = 1` 이 패닝 전체에서 유지된다는 것이 곧 "패닝이 음량을 바꾸지 않는다"이고, 그것이 원래 재려던 것이다.

**(b) `pan = +1` 에서 왼쪽 게인이 음수였다.**

위 루프의 첫 줄(`l >= 0.0f && r >= 0.0f`)이 처음 실행됐을 때 실패했다.

```
  FAIL tests\test_engine_audio.cpp:53  CHECK(l >= 0.0f && r >= 0.0f)
```

원인은 상수다.

```cpp
float angle = (pan + 1.0f) * 0.25f * 3.14159265f;   // 0..π/2
left  = std::cos(angle);
```

`3.14159265f` 를 float 로 반올림하면 `3.14159274…` 로 **π 보다 크다**. `pan = +1` 에서 `angle = 1.5707963705…` 이고 이것은 π/2(`1.5707963267…`)를 넘는다. `cos` 가 `-4.4e-8` 을 돌려준다.

크기로는 무시할 수 있다. 부호는 아니다 — 음수 게인은 그 채널의 **위상을 뒤집는다**. 같은 소리가 좌우로 동시에 나면 서로를 지우고, 모노로 다운믹스하는 출력에서는 그 채널이 통째로 사라진다. 4e-8 에서는 들리지 않지만, 이 부호를 허용하는 코드 경로가 존재한다는 사실 자체가 결함이다.

```cpp
if (left < 0.0f) left = 0.0f;
if (right < 0.0f) right = 0.0f;
```

두 줄로 그 경로를 없앴다. **원래 이 함수의 테스트는 `l < 0.01f` 를 단언하고 있었고, 그것은 통과했다.** 부호를 묻지 않는 단언이었기 때문이다.

## 10. 설계에서 조정한 것

**1. `AudioDevice::PlayAt(sfx, gainL, gainR)` 대신 `PlaySamples(스테레오 블록)`.** 설계는 장치에 게인 인자를 더하는 그림이었다. 그러면 보이스 하나당 버퍼 하나가 제출되고, `waveOut` 이 그것들을 줄 세워 차례로 재생한다(§7) — 합성이 일어나지 않는다. 프레임의 보이스를 미리 합성해 넘기는 형태로 바꿨고, 그 대가로 장치가 톤 테이블을 밖에 노출하게 됐다(`Tones()`).

**2. 톤 합성을 `#ifdef _WIN32` 바깥으로.** 게인 계산과 합성이 헤드리스에서 돌아야 하는데, 파형이 Windows 전용 클래스 안에 있으면 그 경로가 통째로 검증 불가가 된다. `SfxTones` 를 분리하면서 파형이 int16 에서 float 로 바뀌었다.

**3. 믹싱을 `platform/` 이 아니라 `game/` 에서 부른다.** `MixVoices` 를 장치가 부르게 하면 `platform/` → `engine/` 인클루드가 생긴다. 그 방향이 규칙으로 금지된 것은 아니지만(계층 검사기는 `games/` 만 본다), 지금까지 `platform/*.hpp` 는 자기 폴더 밖을 인클루드한 적이 없다. 그 성질을 지켰다.

**4. `MixStereo` 를 `MixVoices` 위에 다시 얹었다.** 제품은 두 단계를 계층 경계를 사이에 두고 나눠 부르므로 `MixStereo` 자체는 부르지 않는다. 그렇다고 지우면 "위치를 넣으면 파형이 나온다"를 파형 수준에서 단언하는 테스트가 사라진다. 합산 루프가 두 벌이 되지 않게 `MixStereo` 를 `SourceGains` + `MixVoices` 의 합성으로 다시 썼다.

**5. `Source::refDist` 추가.** 기준 거리 1은 픽셀 단위 게임에서 성립하지 않는다(§6).

**6. `DefenseSkin` 의 배열 접근을 접근자로.** `skin.towers[i]` 가 `skin.Tower(i)` 가 됐다. 핸들 뒤에 있는 것을 배열 첨자로 꺼낼 수는 없기 때문인데, 그 결과 `DrawGame` 과 리로더가 스프라이트의 **수명**을 직접 다루지 않게 됐다.

**7. 리로더가 `BeginReload`/`CommitReload` 를 부른다.** 12편의 반영 루프가 슬롯마다 즉시 덮어쓰던 것을 스테이징 → 커밋으로 바꿨다(§4).

## 11. 변이 둘

**(a) `ResourcePool::Release` 의 세대 증가를 지운다.**

```
> build\bin\Release\test_asset_handles.exe
[test] asset_handles
  FAIL tests\test_asset_handles.cpp:115  CHECK(reusedGeneration != loose.generation)
  FAIL tests\test_asset_handles.cpp:116  CHECK(!skin.IsValid(loose))
  FAIL tests\test_asset_handles.cpp:117  CHECK(skin.Resolve(loose) == nullptr)
  재사용된 슬롯 index=1  옛 세대=0  새 세대=0
[test] asset_handles: 40 checks, 3 failures
```

출력 한 줄이 변이를 그린다 — 옛 세대와 새 세대가 **둘 다 0**이다. 슬롯이 두 번 회수되고 두 번 다시 쓰였는데 세대가 움직이지 않았고, 그래서 옛 핸들이 새 데이터를 가리킨다. 그것이 정확히 dangling 핸들이다.

**(b) `PanGains` 에서 `pan` 을 무시한다**(각도를 중앙에 고정).

```
> build\bin\Release\test_engine_audio.exe
  FAIL tests\test_engine_audio.cpp:44  CHECK(l < 0.01f)
  FAIL tests\test_engine_audio.cpp:96  CHECK(gr > gl)
  FAIL tests\test_engine_audio.cpp:101 CHECK(gl > gr)
  FAIL tests\test_engine_audio.cpp:116 CHECK(gl < gr * 0.02f)
  FAIL tests\test_engine_audio.cpp:119 CHECK(gr < gl * 0.02f)
[test] engine_audio: 199 checks, 8 failures

> build\bin\Release\test_audio_wiring.exe
  FAIL tests\test_audio_wiring.cpp:45  CHECK(bus.Voices()[0].gainL > bus.Voices()[0].gainR)
  FAIL tests\test_audio_wiring.cpp:46  CHECK(bus.Voices()[1].gainR > bus.Voices()[1].gainL)
  FAIL tests\test_audio_wiring.cpp:67  CHECK(bus.Voices()[0].gainR > bus.Voices()[0].gainL)
  FAIL tests\test_audio_wiring.cpp:73  CHECK(bus.Voices()[0].gainL > bus.Voices()[0].gainR)
  FAIL tests\test_audio_wiring.cpp:164 CHECK(left > 0)
  FAIL tests\test_audio_wiring.cpp:165 CHECK(right > 0)
  FAIL tests\test_audio_wiring.cpp:166 CHECK_EQ(mismatched, 0)
[test] audio_wiring: 2021 checks, 7 failures
```

두 층이 함께 죽는 것이 이 변이의 값이다. 단위 검사(`engine_audio`)는 함수가 틀렸다고 말하고, 배선 검사(`audio_wiring`)는 제품이 그 함수를 실제로 부르고 있었다고 말한다. 후자가 없으면 앞의 실패는 "아무도 안 쓰는 함수가 틀렸다"와 구분되지 않는다.

## 12. 실행과 관측

깨끗한 빌드에서.

```
> cmake -S . -B build_verify && cmake --build build_verify --config Release
> ctest -C Release -j 8
100% tests passed out of 84
```

**소유.**

```
> build_verify\bin\Release\test_asset_handles.exe
[test] asset_handles
  재사용된 슬롯 index=1  옛 세대=1  새 세대=3
[test] asset_handles: 40 checks, 0 failures
```

**듣기.**

```
> build_verify\bin\Release\test_audio_wiring.exe
[test] audio_wiring
  ortho2d  좌=0.262/0.083  우=0.083/0.262  정면=0.245/0.245
  tilted3d yaw=180  좌=0.122 우=0.073
  시뮬 출력: 발사 74 · 처치 40 (소리 난 프레임 61, 최대 4)
  프레임당 소리 수: 1=17 2=37 3=5 4=2 5=0 6+=0
  제품 프레임: 왼쪽 보이스 1 · 오른쪽 보이스 197 · 어긋남 0 · 블록 179
  보이스  1개 x 9922프레임 합성 = 18.0us (실제 섞인 보이스 1)
  보이스  8개 x 9922프레임 합성 = 54.2us (실제 섞인 보이스 8)
  보이스 32개 x 9922프레임 합성 = 175.0us (실제 섞인 보이스 32)
  보이스 64개 x 9922프레임 합성 = 201.8us (실제 섞인 보이스 32)
[test] audio_wiring: 2029 checks, 0 failures
```

`좌=0.262/0.083` 을 읽는 법: 왼쪽 200픽셀의 소스가 왼쪽 채널 0.262, 오른쪽 0.083. 좌우 비 3.2배이고, 감쇠 뒤의 절대값이 0.27 인 것은 그 소스가 청취자에서 328픽셀(평면 200 + 높이 260) 떨어져 있기 때문이다.

**엔진 단위.**

```
> build_verify\bin\Release\test_engine_audio.exe
[test] engine_audio: 199 checks, 0 failures
```

**리로드 A/B 는 그대로 통과한다.** 소유를 바꿨지만 12편의 판정 — 동기 팔과 비동기 팔의 **최종 픽셀이 같다** — 은 건드리지 않았다.

```
> build_verify\bin\Release\test_async_reload.exe
[test] async_reload: ... 0 failures
```

**제품.**

```
> build_verify\bin\Release\mega_defense.exe --selftest
[assets] loaded 10/10 external sprites from ...\assets
[content] map=external waves=external (5) balance=script pose=external(5키)
[selftest] result=CLEAR ticks=1377 towers=11 wave=6 packet=175 protocol=OK render=OK assets=10/10 content=external stomps=2
```

`assets=10/10` — 열 장이 전부 외부 파일에서 왔고, 그 열 장이 이제 핸들 뒤에 있다.

---

## 결론

두 가정이 언제 거짓이 됐는지는 분명하다. 리소스 수명 가정은 12편에서, 소리 위치 가정은 14편에서. 두 번 다 그 편의 변경은 옳았고, 옳은 변경이 **다른 곳의 전제를 조용히 무효화**했다.

그 무효화를 아무도 알아채지 못한 이유도 같다. **전제가 코드에 적혀 있지 않았기 때문이다.** `std::array<render::Sprite, 4>` 는 "이 스프라이트는 프로세스만큼 산다"고 말하지 않는다. `Play(Sfx)` 는 "이 소리에는 위치가 없다"고 말하지 않는다. 둘 다 그냥 그렇게 동작할 뿐이었고, 동작이 전제를 말해 주지 않는다.

핸들과 청취자는 그 전제를 **타입으로** 만든다. `engine::Handle` 은 "이것은 언제든 무효가 될 수 있다"를 인터페이스에 적어 두고, `engine::Listener` 는 "소리는 누군가에게 들린다"를 적어 둔다. 그래서 다음에 무언가가 깨질 때는 컴파일러나 테스트가 먼저 말한다.

측정에서 나온 것 셋.

**소리가 난 프레임의 72%에서 소리가 겹친다.** 프레임당 한 블록으로 합성하는 결정의 근거이고, 이 수를 재기 전까지는 "겹치는 일이 얼마나 있겠나"에 답할 수 없었다.

**보이스 상한이 실제로 선형을 끊는다.** 64개를 넣어도 32개분의 시간만 든다.

**`pan = +1` 에서 게인이 음수였다.** 부호를 묻지 않는 단언(`l < 0.01f`)이 통과시키고 있던 결함이고, 에너지 단언을 쓰면서 드러났다.

현재 상태의 한계 넷.

**클라이언트는 소리를 내지 않는다.** `SoundEvent` 는 시뮬이 내는 것이고 클라이언트는 시뮬을 돌리지 않는다. 클라이언트가 들을 수 있는 것은 스냅샷의 변화(타워가 늘었다, 매치가 끝났다)뿐이고, 그것은 지금도 위치 없는 `Play` 로 난다.

**블록 길이가 가장 긴 톤에 맞춰진다.** 짧은 소리 하나와 긴 소리 하나가 같은 프레임에 나면 9922프레임짜리 블록을 만들고, 그중 대부분은 짧은 소리 쪽이 0이다. 보이스별 시작 오프셋을 지원하는 스트리밍 믹서라면 그 낭비가 없다.

**연속 프레임의 블록이 서로 겹칠 수 있다.** 각 프레임이 자기 블록을 제출하므로, 앞 블록이 재생 중일 때 다음 블록이 큐에 들어간다. `waveOut` 은 그 둘을 이어 붙여 재생하므로 소리가 미세하게 밀린다. 진짜 해법은 고정 크기 블록을 연속 스트림으로 내보내는 것이고, 그것은 프레임 루프가 아니라 오디오 스레드의 일이다.

**핸들 노드 배열이 고정 상한이다.** `kFileCount * 2 + 2 = 22`. 리로드가 중첩되면(요청이 끝나기 전에 또 요청) 상한을 넘을 수 있는데, `AssetReloader` 가 `busy_` 로 중첩을 막고 있어 지금은 도달하지 않는다. 그 방어가 리소스 쪽이 아니라 리로더 쪽에 있다는 것이 이 구조의 약한 지점이다.
