# 제품이 호출하는 계층

**생성 파일이다. 직접 수정하지 마라.** `scripts/reachability.py` 가 빌드마다 다시 쓴다.
손으로 고쳐도 다음 빌드에 덮어써지고, 내용이 현실과 어긋나면 그 전에 도달성 게이트가 빌드를 실패시킨다.

출처는 제품 타깃이 컴파일될 때 컴파일러가 남긴 의존성 출력(MSVC `/sourceDependencies`, GCC/Clang `-MD`)이다.
텍스트로 `#include` 를 훑은 추정치가 아니라 컴파일러가 실제로 연 파일 목록이다.

## 요약

| 항목 | 수 |
|---|---|
| `include/` 헤더 전체 | 121 |
| 제품 도달 | 121 |
| 랩 전용(`docs/LAB_ONLY.md` 등재) | 0 |
| 근거 없는 미도달 | 0 (있으면 빌드가 실패한다) |

제품 진입점별 도달 수:

| 제품 타깃 | 도달 헤더 |
|---|---|
| `mega_defense` | 116 |
| `mega_defense_server` | 91 |

## 계층별 도달 현황

| 계층 | 도달 / 전체 |
|---|---|
| `ai/` | 9 / 9 |
| `algo/` | 6 / 6 |
| `containers/` | 4 / 4 |
| `db/` | 3 / 3 |
| `ecs/` | 1 / 1 |
| `engine/` | 7 / 7 |
| `game/` | 21 / 21 |
| `io/` | 3 / 3 |
| `jobs/` | 1 / 1 |
| `live/` | 7 / 7 |
| `math/` | 12 / 12 |
| `memory/` | 3 / 3 |
| `mp/` | 6 / 6 |
| `net/` | 9 / 9 |
| `physics/` | 6 / 6 |
| `platform/` | 10 / 10 |
| `profiler/` | 1 / 1 |
| `render/` | 4 / 4 |
| `sched/` | 1 / 1 |
| `serialize/` | 1 / 1 |
| `sync/` | 2 / 2 |
| `tools/` | 4 / 4 |

## 제품 도달 헤더

| 헤더 | `mega_defense` | `mega_defense_server` |
|---|---|---|
| `ai/bt/builder.hpp` | O | O |
| `ai/bt/node.hpp` | O | O |
| `ai/bt/tick.hpp` | O | O |
| `ai/fsm/state_machine.hpp` | O | O |
| `ai/pathfinding/astar.hpp` | O | O |
| `ai/pathfinding/flow_field.hpp` | O | O |
| `ai/pathfinding/grid_map.hpp` | O | O |
| `ai/perception/perception.hpp` | O | O |
| `ai/steering/steering.hpp` | O | O |
| `algo/aho_corasick.hpp` | - | O |
| `algo/articulation.hpp` | O | O |
| `algo/dsu.hpp` | O | O |
| `algo/huffman.hpp` | O | - |
| `algo/quickselect.hpp` | O | O |
| `algo/topo_sort.hpp` | O | O |
| `containers/hash_map.h` | O | O |
| `containers/intrusive_list.h` | O | O |
| `containers/ring_buffer.h` | O | O |
| `containers/soa_vector.h` | O | O |
| `db/kv_store.hpp` | O | - |
| `db/records.hpp` | O | - |
| `db/repository.hpp` | O | - |
| `ecs/world.h` | O | O |
| `engine/anim.hpp` | O | O |
| `engine/audio.hpp` | O | - |
| `engine/ik.hpp` | O | O |
| `engine/input.hpp` | O | - |
| `engine/particles.hpp` | O | - |
| `engine/resource.hpp` | O | - |
| `engine/skeleton.hpp` | O | O |
| `game/allocators.hpp` | O | O |
| `game/asset_reload.hpp` | O | - |
| `game/boss_rig.hpp` | O | O |
| `game/camera.hpp` | O | - |
| `game/debris.hpp` | O | O |
| `game/defense_app.hpp` | O | - |
| `game/defense_content.hpp` | O | O |
| `game/defense_map.hpp` | O | O |
| `game/defense_progression.hpp` | O | - |
| `game/defense_protocol.hpp` | O | O |
| `game/defense_server.hpp` | - | O |
| `game/defense_view.hpp` | O | - |
| `game/enemy_brain.hpp` | O | O |
| `game/entity_store.hpp` | O | O |
| `game/game.hpp` | O | O |
| `game/map_gen.hpp` | O | O |
| `game/nav.hpp` | O | O |
| `game/replay.hpp` | O | - |
| `game/sfx_bus.hpp` | O | - |
| `game/switches.hpp` | O | O |
| `game/terrain.hpp` | O | O |
| `io/async_io.h` | O | - |
| `io/file.h` | O | O |
| `io/read_file.hpp` | O | O |
| `jobs/scheduler.h` | O | O |
| `live/admission_token.hpp` | - | O |
| `live/analytics.hpp` | O | - |
| `live/economy.hpp` | O | - |
| `live/event_schedule.hpp` | O | O |
| `live/feature_flag.hpp` | O | O |
| `live/patch.hpp` | O | O |
| `live/unlock_graph.hpp` | O | O |
| `math/interp.hpp` | O | O |
| `math/mat.hpp` | O | O |
| `math/noise.hpp` | O | O |
| `math/quat.hpp` | O | O |
| `math/scalar.hpp` | O | O |
| `math/sdf.hpp` | O | O |
| `math/spatial/aabb.hpp` | O | O |
| `math/spatial/bvh.hpp` | O | O |
| `math/spatial/quadtree.hpp` | O | O |
| `math/spatial/uniform_grid.hpp` | O | O |
| `math/transform.hpp` | O | O |
| `math/vec.hpp` | O | O |
| `memory/arena.h` | O | O |
| `memory/pool.h` | O | O |
| `memory/slab.h` | O | O |
| `mp/det_sim.hpp` | O | O |
| `mp/golden.hpp` | O | - |
| `mp/input.hpp` | O | O |
| `mp/lockstep.hpp` | O | - |
| `mp/matchmaking.hpp` | O | O |
| `mp/rollback.hpp` | O | O |
| `net/packet.hpp` | O | O |
| `net/reliable_channel.hpp` | O | O |
| `net/rtt.hpp` | O | - |
| `net/sequence.hpp` | O | O |
| `net/session.hpp` | O | O |
| `net/snapshot.hpp` | O | O |
| `net/transport.hpp` | O | O |
| `net/udp_server.hpp` | - | O |
| `net/udp_transport.hpp` | O | - |
| `physics/integrate.hpp` | O | O |
| `physics/narrow_phase/collide.hpp` | O | O |
| `physics/rigid_body.hpp` | O | O |
| `physics/shape.hpp` | O | O |
| `physics/solver/sequential_impulse.hpp` | O | O |
| `physics/world.hpp` | O | O |
| `platform/audio_device.hpp` | O | - |
| `platform/byteorder.hpp` | O | O |
| `platform/capability.hpp` | O | O |
| `platform/console.hpp` | O | O |
| `platform/detect.hpp` | O | O |
| `platform/paths.hpp` | O | O |
| `platform/process.hpp` | - | O |
| `platform/vfs.hpp` | O | - |
| `platform/vk_present.hpp` | O | - |
| `platform/window.hpp` | O | - |
| `profiler/trace.h` | O | O |
| `render/framebuffer.hpp` | O | - |
| `render/raster2d.hpp` | O | - |
| `render/sprite.hpp` | O | - |
| `render/text.hpp` | O | - |
| `sched/affinity.h` | O | O |
| `serialize/archive.h` | O | O |
| `sync/futex.h` | O | O |
| `sync/lock.h` | O | O |
| `tools/asset_pipeline.hpp` | O | O |
| `tools/level_editor.hpp` | O | O |
| `tools/procgen.hpp` | O | O |
| `tools/script.hpp` | O | O |

## 랩 전용 헤더 (제품 미도달, 등재됨)

전체 목록과 각 항목의 사유는 [`LAB_ONLY.md`](LAB_ONLY.md) 에 있다. 여기서는 도입 단계별 집계만 둔다.

| 도입 단계 | 헤더 수 | 헤더 |
|---|---|---|

