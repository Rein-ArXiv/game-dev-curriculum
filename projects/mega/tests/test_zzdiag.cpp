#include "game/defense_content.hpp"
#include "game/game.hpp"
#include "game/map_gen.hpp"
#include <cstdio>
using namespace mega;
using namespace mega::game;
int main() {
    mapgen::TerrainParams params; params.seed = 4;
    DefenseMap map; mapgen::Generate(params, map);
    Switches sw; sw.map = MapSource::Procgen; sw.mapSeed = 4;
    sw.pathing = PathingKind::FlowField; sw.steering = true; sw.scale = ScaleKind::Large;
    GameWorld world; world.Configure(sw);
    std::vector<WaveDef> waves{{120, 40.0f, 40.0f, 1, 0.03f, EnemyType::Swarm}};
    world.Init(map, waves, 0, 1e9f);
    const DefenseMap::Cell core = map.CoreCell();
    std::printf("core cell %d,%d center %.1f,%.1f cellsize %d\n", core.x, core.y,
        (double)world.CorePos().x, (double)world.CorePos().y, map.CellSize());
    for (int tick = 0; tick < 6000; ++tick) {
        world.Step(1.0f/60.0f);
        if (tick % 1500 == 1499) {
            const EnemyStore& e = world.Enemies();
            int alive = 0; for (size_t i=0;i<e.Size();++i) if (e.Alive(i)) ++alive;
            std::printf("tick %d alive=%d wave=%d rows=%zu\n", tick, alive, world.Wave(), e.Size());
            int shown = 0;
            for (size_t i=0;i<e.Size() && shown<6;++i) {
                if (!e.Alive(i)) continue;
                const BrainRow& b = world.Brains()[i];
                const DefenseMap::Cell c = map.WorldToCell(e.Pos(i));
                std::printf("   row %zu pos %.1f,%.1f cell %d,%d  target %d,%d has=%d state=%d vel %.2f,%.2f walk=%d\n",
                    i, (double)e.Pos(i).x, (double)e.Pos(i).y, c.x, c.y,
                    b.cell.x, b.cell.y, (int)b.hasCell, (int)b.state,
                    (double)b.velocity.x, (double)b.velocity.y,
                    (int)world.Nav().Walkable(c.x, c.y));
                ++shown;
            }
        }
    }
    return 0;
}
