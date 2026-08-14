# 제품이 호출하는 계층

**생성 파일이다. 직접 수정하지 마라.** `scripts/reachability.py` 가 빌드마다 다시 쓴다.
손으로 고쳐도 다음 빌드에 덮어써지고, 내용이 현실과 어긋나면 그 전에 도달성 게이트가 빌드를 실패시킨다.

출처는 제품 타깃이 컴파일될 때 컴파일러가 남긴 의존성 출력(MSVC `/sourceDependencies`, GCC/Clang `-MD`)이다.
텍스트로 `#include` 를 훑은 추정치가 아니라 컴파일러가 실제로 연 파일 목록이다.

## 요약

| 항목 | 수 |
|---|---|
| `include/` 헤더 전체 | 99 |
| 제품 도달 | 41 |
| 랩 전용(`docs/LAB_ONLY.md` 등재) | 58 |
| 근거 없는 미도달 | 0 (있으면 빌드가 실패한다) |

제품 진입점별 도달 수:

| 제품 타깃 | 도달 헤더 |
|---|---|
| `mega_defense` | 37 |
| `mega_defense_server` | 23 |

## 계층별 도달 현황

| 계층 | 도달 / 전체 |
|---|---|
| `ai/` | 0 / 9 |
| `containers/` | 0 / 4 |
| `db/` | 3 / 3 |
| `ecs/` | 0 / 1 |
| `engine/` | 2 / 7 |
| `game/` | 7 / 7 |
| `io/` | 2 / 3 |
| `jobs/` | 0 / 1 |
| `live/` | 2 / 6 |
| `math/` | 3 / 12 |
| `memory/` | 0 / 3 |
| `mp/` | 0 / 5 |
| `net/` | 7 / 9 |
| `physics/` | 3 / 6 |
| `platform/` | 6 / 10 |
| `profiler/` | 0 / 1 |
| `render/` | 4 / 4 |
| `sched/` | 0 / 1 |
| `serialize/` | 1 / 1 |
| `sync/` | 0 / 2 |
| `tools/` | 1 / 4 |

## 제품 도달 헤더

| 헤더 | `mega_defense` | `mega_defense_server` |
|---|---|---|
| `db/kv_store.hpp` | O | - |
| `db/records.hpp` | O | - |
| `db/repository.hpp` | O | - |
| `engine/input.hpp` | O | - |
| `engine/particles.hpp` | O | - |
| `game/defense_app.hpp` | O | - |
| `game/defense_content.hpp` | O | O |
| `game/defense_progression.hpp` | O | - |
| `game/defense_protocol.hpp` | O | O |
| `game/defense_server.hpp` | - | O |
| `game/defense_view.hpp` | O | - |
| `game/game.hpp` | O | O |
| `io/file.h` | O | O |
| `io/read_file.hpp` | O | O |
| `live/admission_token.hpp` | - | O |
| `live/analytics.hpp` | O | - |
| `math/scalar.hpp` | O | O |
| `math/spatial/aabb.hpp` | O | O |
| `math/vec.hpp` | O | O |
| `net/reliable_channel.hpp` | O | O |
| `net/sequence.hpp` | O | O |
| `net/session.hpp` | O | O |
| `net/snapshot.hpp` | O | O |
| `net/transport.hpp` | O | O |
| `net/udp_server.hpp` | - | O |
| `net/udp_transport.hpp` | O | - |
| `physics/narrow_phase/collide.hpp` | O | O |
| `physics/rigid_body.hpp` | O | O |
| `physics/shape.hpp` | O | O |
| `platform/audio_device.hpp` | O | - |
| `platform/paths.hpp` | O | O |
| `platform/process.hpp` | - | O |
| `platform/vfs.hpp` | O | - |
| `platform/vk_present.hpp` | O | - |
| `platform/window.hpp` | O | - |
| `render/framebuffer.hpp` | O | - |
| `render/raster2d.hpp` | O | - |
| `render/sprite.hpp` | O | - |
| `render/text.hpp` | O | - |
| `serialize/archive.h` | O | O |
| `tools/script.hpp` | O | O |

## 랩 전용 헤더 (제품 미도달, 등재됨)

전체 목록과 각 항목의 사유는 [`LAB_ONLY.md`](LAB_ONLY.md) 에 있다. 여기서는 도입 단계별 집계만 둔다.

| 도입 단계 | 헤더 수 | 헤더 |
|---|---|---|
| P1 | 16 | `containers/intrusive_list.h` `engine/resource.hpp` `io/async_io.h` `jobs/scheduler.h` `math/mat.hpp` `math/noise.hpp` `math/quat.hpp` `math/sdf.hpp` `math/transform.hpp` `memory/arena.h` `memory/pool.h` `memory/slab.h` `profiler/trace.h` `sched/affinity.h` `sync/futex.h` `sync/lock.h` |
| P2 | 10 | `ai/fsm/state_machine.hpp` `ai/steering/steering.hpp` `engine/ik.hpp` `engine/skeleton.hpp` `math/interp.hpp` `math/spatial/bvh.hpp` `math/spatial/uniform_grid.hpp` `physics/integrate.hpp` `physics/solver/sequential_impulse.hpp` `physics/world.hpp` |
| P4 | 10 | `ai/bt/builder.hpp` `ai/bt/node.hpp` `ai/bt/tick.hpp` `ai/pathfinding/astar.hpp` `ai/pathfinding/flow_field.hpp` `ai/pathfinding/grid_map.hpp` `ai/perception/perception.hpp` `containers/hash_map.h` `containers/soa_vector.h` `ecs/world.h` |
| P5 | 7 | `containers/ring_buffer.h` `mp/det_sim.hpp` `mp/input.hpp` `mp/lockstep.hpp` `mp/rollback.hpp` `net/packet.hpp` `net/rtt.hpp` |
| P7 | 5 | `live/economy.hpp` `live/event_schedule.hpp` `live/feature_flag.hpp` `live/patch.hpp` `mp/matchmaking.hpp` |
| P8 | 5 | `platform/byteorder.hpp` `platform/capability.hpp` `platform/console.hpp` `platform/detect.hpp` `tools/asset_pipeline.hpp` |
| P9 | 1 | `engine/audio.hpp` |
| 없음 | 4 | `engine/anim.hpp` `math/spatial/quadtree.hpp` `tools/level_editor.hpp` `tools/procgen.hpp` |

