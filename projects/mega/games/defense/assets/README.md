# Defense skin

`mega_defense` loads these ASCII PPM (`P3`) sprites at startup. Pure magenta
(`255 0 255`) is transparent. Replace a file while preserving its name; no C++
change or rebuild is required.

- `tower_cannon.ppm`, `tower_gatling.ppm`, `tower_splash.ppm`, `tower_sniper.ppm`
- `enemy_normal.ppm`, `enemy_fast.ppm`, `enemy_tank.ppm`, `enemy_swarm.ppm`, `enemy_boss.ppm`
- `core.ppm`
- `level.txt`: `S` spawn, `P` path, `C` core, `.` buildable, `#` blocked.
- `waves.txt`: `count hp speed reward interval enemy_type` per line.
- `balance.script`: `start_gold`, `core_hp`, `enemy_hp_multiplier`,
  `enemy_speed_multiplier`, and `reward_multiplier`.

The loader accepts at most 256x256 pixels and validates every channel. Missing
or malformed files fall back to built-in procedural sprites. A level is
accepted only when every path tile forms one continuous, non-branching chain
from the single spawn to the single core. Wave files accept 1-100 validated
rows and enemy types `normal`, `fast`, `tank`, `swarm`, or `boss`; malformed
files fall back to the built-in five-wave set as one atomic unit.
The balance script is capped at 64 KiB, runs in the restricted arithmetic DSL,
and is accepted only when every exported value is finite and inside its
documented gameplay and wire-format range. Any parse, execution, or range
failure restores the complete built-in balance configuration.
