# SHAMAN Slice 1 Handoff — Phase 1 + Spherical Terrain (UE4.27.2)

Oct 8, 2026 · @Nicholas Heilbron

## Status

Slice 1 is **accepted** in UE4.27.2: all 32 automated tests pass and every Phase 1 acceptance check passed in the editor on 8 Oct 2026. The accepted state is commit `c1d7f28 fix: close Phase 1 acceptance blockers`.

Slice 1 covers two pieces of work that are now one baseline:

- **Phase 1 gameplay:** a deterministic starting world, the Shaman and tribe, TAKA (Blast), combat, death and reincarnation, enemy tribe elimination and a victory state.
- **Spherical terrain foundation:** a planet terrain behind engine-independent interfaces, with Voxel Plugin Free as the rendering/collision backend and spherical movement for units.

Nothing from Phase 2 is part of this baseline. Phase 2 work starts from `c1d7f28` (see the Slice 2 handoff).

## Project and environment

| Item | Value |
| --- | --- |
| Engine | Unreal Engine **4.27.2** installed build (`C:/Program Files/Epic Games/UE_4.27`). UE4 only: no UE5 APIs, no migration. |
| Toolchain | Visual Studio 2019 (MSVC 14.29), Win64 Development Editor |
| Project folder | `C:\Nick\other forms of comp\fun\Shaman\1st Go\Merged 1st steps\Shaman 4.27` |
| Voxel backend | Voxel Plugin Free "Free-Beta-415230fff-2021-06-08", commit `9433a66`, MIT, vendored unmodified in `Plugins/VoxelFree` |
| Git | Local repo with Git LFS for `.uasset`/`.umap`; `Plugins/VoxelFree/** -text` keeps the vendored plugin byte-identical; Binaries, Intermediate, Saved, DerivedDataCache and `.vs` are ignored |
| Patches and notes | Delivered into the project's `Claude outputs` folder |

**Git history (newest first):**

| Commit | What |
| --- | --- |
| `c1d7f28` | fix: close Phase 1 acceptance blockers (accepted baseline) |
| `f869ae0` | Merge terrain/spherical-spike into main |
| `340561a` | terrain: add UE4 spherical terrain foundation |
| `5e62335` | terrain: fix r2.2 (SphericalMovement test steps the movement component) |
| `7557d0c` | terrain: fix UHT name clash (ITerrainModification renamed ITerrainModifier) |
| `09f0bd8` | terrain: spherical terrain spike, patch r2 (also pushed as `origin/terrain/spherical-spike`) |
| `1a7f89c` | baseline: Fix1 before spherical terrain |
| `993fdd0` | Initial Commit (`origin/main`) |

`993fdd0` and `09f0bd8` are known to be on the GitHub remote. Whether `main` (`f869ae0`, `c1d7f28`) has been pushed since was not confirmed; check `git log origin/main` before relying on the remote.

## What was built

**Phase 1 gameplay** (C++ authoritative, data from four DataTables imported from `Content/Data/*.csv`):

| Area | What works |
| --- | --- |
| World generation | `FShamanWorldGenerator` builds the flat start world from seed 1337 (`Valid=1`, 312 markers, 313 actors). Same seed gives the same world (`ShamanRegenerate 1337`). |
| Player start | Shaman, 3 Braves, House, Campfire, Reincarnation Circle |
| World content | Trees, water (pond/sea), food (berry bushes), stone, ore, Wildmen camp |
| Enemy | Enemy settlement with circle, house, Shaman, 3 Braves and 1 Warrior, 7,000 to 11,500 uu away |
| Player | Third-person movement, camera, jump, E interaction (order Braves to follow/hold), melee (RMB/F), rally (R), debug labels (F3) |
| TAKA Blast | Homing projectile: 5 mana, range 10 (500 uu), radius 300, damage 40 with falloff, knockback 1, cooldown 1 s, friendly fire on |
| Convert | Converts Wildmen into Braves (learned with `ShamanLearnSpell Convert`; not a starting spell) |
| Combat and AI | Followers and enemies acquire targets, melee, enemy Shaman casts Blast |
| Tribes | Follower roster per tribe; follower count drives mana regen and rebirth time; deaths remove followers immediately |
| Death and rebirth | Shaman reincarnates at its own circle after a follower-scaled delay |
| Elimination and victory | Enemy Shaman dying with 0 followers destroys its circle and eliminates the tribe; all enemy tribes eliminated = level won (VICTORY banner) |
| Water | Drowning in deep water after a delay |

**Spherical terrain foundation:**

- A planet (radius 20,000 uu) whose height field is the single source of truth (`FPlanetHeightField`): seeded noise, edits (Raise, Lower, Flatten, Paint), protected regions, save as seed plus edit log.
- Gameplay talks only to `UShamanTerrainSubsystem` through `ITerrainWorld`, `ITerrainQuery` and `ITerrainModifier`; Voxel Plugin types stay inside the `ShamanVoxel` module.
- Units walk on the sphere: radial gravity, upright alignment, custom movement modes PlanetWalk and PlanetFall, an analytic ground safety net, walkable slope taken from the terrain (50 degrees).
- A spherical sea with radial water depth, a surface probe actor that re-aligns after edits, and a great-circle path prototype (no obstacle avoidance).

## Architecture

&#91;embedded content: Slice 1 architecture · 4 layers\]

Units and movement query and edit terrain only through the subsystem; Voxel Plugin types never leave the `ShamanVoxel` module, so a different backend (for example a cube-sphere heightmap) can replace it. Spells and tribes do not touch terrain. The flat Phase 1 world is built by `AShamanGameMode` itself (`FShamanWorldGenerator` + a procedural mesh); with no planet active, movement falls back to standard UE4 walking.

| Module / folder | Holds |
| --- | --- |
| `Source/Shaman` | All gameplay, terrain core (`Terrain/`), tests (`Private/Tests`) |
| `Source/ShamanVoxel` | The Voxel Plugin backend only (private dependency on Voxel) |
| `Plugins/VoxelFree` | Vendored Voxel Plugin Free, unmodified |
| `Tools/TerrainHarness` | Off-engine g++ checks of the terrain core |

## Acceptance evidence

**Automated tests: 32 of 32 pass** (Session Frontend, filter `Shaman`).

| Group | Tests |
| --- | --- |
| Spells | ManaCooldownRange, TargetFiltering, FriendlyFire, Convert.\* (11 tests incl. BlastUnchanged) |
| Data | TakaPreserved (reads the production `Spells` table, fails if any Blast value changes) |
| Tribes | PopulationFormulas, FollowerCountOnDeath, Elimination |
| World | Determinism, StartAreaConstraints (40 seeds) |
| Terrain | PlanetCenterRadius, RadialUp, SurfaceQuery, Determinism, Underwater, ModificationRequest, ProtectedRejection, ChangedNotification, NavigationPrototype, SphericalMovement, ShamanDeathRebirth, WalkableSlope |

The off-engine terrain harness (`Tools/TerrainHarness`, g++) passes 20,058 checks; it is a separate verification of the engine-independent terrain core, not a UE4 measurement.

**Editor playtests (PM, 7 to 8 Oct 2026):**

- ShamanPrototype: full start scenario, seed 1337 reproducible, movement, jump, E interaction, Blast cost and cooldown, damage and knockback, combat with the enemy tribe.
- Follower count drops by exactly 1 per death (log: 7, 6, 5, 4).
- Enemy Shaman reborn while it had followers; at 0 followers its circle was destroyed, the tribe eliminated and the VICTORY banner shown.
- Player death and rebirth at the player's own circle, on the flat map and the planet.
- PlanetTest: mountain peaks reachable, dead body stays still, clean respawn, normal movement after respawn.

**UE4 performance (editor PIE, PM's PC):**

| Measure | Value |
| --- | --- |
| Flat world generation | about 10 ms; spawn 47 to 70 ms (313 actors) |
| Planet voxel world creation | 1 to 44 ms; initial meshes and collision 5 to 10 ms |
| Terrain edit apply | avg 0.09 ms, max 0.19 ms |
| Edit to mesh + collision updated (r=500) | avg 40 to 44 ms, max 50.4 ms (target under 100 ms) |
| FPS | about 48 on ShamanPrototype; 55 to 60 on the planet (60 cap), 105 to 112 uncapped with 306 units |
| Memory | about 1.2 to 1.39 GB editor process |
| Save/reload of 8 edits | identical hash; replay 139 to 148 ms; full remesh about 1.0 to 1.3 s |

Recurring single \~340 ms frames and 3 fps windows match the editor throttling while it is in the background.

## Acceptance fix history

The acceptance audit found these defects; each was fixed at its cause and covered by a test where practical.

| Defect | Cause | Fix |
| --- | --- | --- |
| UHT build error on first UE4 build | `UTerrainModification` and `FTerrainModification` collide once UHT strips prefixes | Interface renamed `ITerrainModifier` |
| SphericalMovement test: unit never moved | Bare automation world did not tick the movement component | Test steps the movement component directly; world bounds checks off as in the planet mode |
| "Missing" start scenario on ShamanPrototype | The map's GameMode Override had been switched to ShamanPlanetGameMode in the editor (planet mode spawns only 8 actors) | Map restored from git; planet mode now logs and shows an on-screen notice naming itself |
| Dead Shaman flew around the planet | A no-killer death (drowning) used 250 HP as damage, launching the body at \~4,000 uu/s, close to orbital speed | No death launch for Shamans or killer-less deaths; Shaman corpse laid on the ground with movement disabled; rebirth clears velocity and queued forces |
| Mountain peaks unreachable | Movement used the engine default 44.8 degree limit; the terrain defines walkable as 50 degrees | On planets the movement uses the terrain's `MaxWalkableSlopeDeg`; flat worlds unchanged |
| TAKA data test checked nothing | It looked for `DT_Spells`; the table is `Spells`, so it skipped and still passed | Test resolves the table exactly like the game and fails on any wrong Blast value |
| ShamanDeathRebirth "6.9 uu drift" | `acos` float precision: one ulp below 1 reads as 6.9 uu on this planet | `GetSurfaceDistance` uses `atan2` |
| "Unit row not found" in tests | Worlds without a Shaman game mode had no data tables | Such worlds resolve the same production data as the game |
| Enemy Shaman always reincarnated | No elimination rule existed | Elimination at 0 followers (decided end of frame), circle destroyed, level-won state + HUD banner |
| Follower count looked stale | Not a code defect: one Blast (16 to 40 damage) often does not kill a 60 HP Brave | Every death now logs the tribe's new follower count |

## Maps, game modes and data

| Map | GameMode Override | Role |
| --- | --- | --- |
| `ShamanPrototype` | **None** (project default `ShamanGameMode`) | Normal Phase 1 gameplay: full start scenario |
| `PlanetTest` | `ShamanPlanetGameMode` | Terrain test bed: circle, 1 Brave, 4 Wildmen, sea, surface probe |

Do not switch ShamanPrototype's override to the planet mode; that has caused every "missing content" report so far. Use PlanetTest for planet testing.

**Data:** the game resolves an optional `/Game/Data/DA_ShamanGameData` asset; it does not exist, so the built-in defaults are used (seed 1337, two tribes, Blast as starting spell) plus the DataTables found by row type: `Spells`, `Units`, `Buildings`, `Resources` (imported from `Content/Data/*.csv`). The three "Failed to load DA\_ShamanGameData" warnings come from that optional lookup.

**Controls:** WASD move, mouse look, Space jump, LMB cast, Q/wheel switch spell, RMB/F melee, E interact, R rally followers, F3 debug labels.

**Console commands:**

| Command | Mode | Does |
| --- | --- | --- |
| `ShamanRegenerate <seed>` | both | Rebuilds the world from a seed |
| `ShamanLearnSpell Convert` | both | Teaches a spell |
| `ShamanTerrainReport` | planet | Terrain stats, FPS, ground rescues, player altitude, memory |
| `ShamanTerrainProbe` | planet | Query under the crosshair |
| `ShamanTerrainRaise r s` / `Lower` / `Flatten` | planet | Terrain edits at the aim point |
| `ShamanTerrainProtectedTest` | planet | Protected-region check (prints PASS/FAIL) |
| `ShamanTerrainBenchmark 8` | planet | 8 edits, mesh + collision timing vs 100 ms |
| `ShamanTerrainSaveReload` | planet | Edit-log save/reload hash check |
| `ShamanSpawnStress 300` | planet | Spawns units for performance checks |

## Known limitations and open items

None of these block Phase 1; they are recorded so later milestones pick them up.

- **Ragdoll is a stand-in.** There are no skeletal meshes, so hits launch the capsule and the dead lie down as a cylinder; real physics ragdoll is untested.
- **World-up code on the planet:** Blast spawns 60 uu above in world Z and knockback biases toward world up, so both are off on most of the planet; physics ragdoll would fall toward world -Z.
- **No camera zoom; no end-of-level flow** beyond the VICTORY banner; no player defeat condition.
- **Planet navigation** is a great-circle prototype: it stops at water or cliffs and never routes around them. RVO avoidance is off on planets.
- **Planet streaming:** full-resolution collision only near the player; distant units may stand on coarse collision or the analytic ground.
- **Terrain save** (seed + edit log) is not wired into a game save system; the edit log holds 4,096 edits.
- **OpenVDB header** in the plugin's editor-only VoxelVDB module is MPL-2.0: flag for a licence review before shipping.
- **No `DA_ShamanGameData` asset** (see Maps, game modes and data).

## Rules for working on the project

- UE4.27.2 only: never recommend, migrate to or use UE5.
- C++ for authoritative gameplay; Blueprints only for composition, assets, animation, VFX, audio and tuning. Keep data in the DataTables and Gameplay Tags.
- Gameplay never calls Voxel Plugin types; terrain goes through `UShamanTerrainSubsystem` and its interfaces. No flat-world assumptions (world Z, Z = 0, XY distance) in new gameplay code: use `FShamanSpace` and `FPlanetFrame`.
- Do not change TAKA balance or the accepted Phase 1 behaviour without an explicit request; `Shaman.Data.TakaPreserved` guards Blast.
- Work in slices: inspect first, smallest coherent change, build in UE4.27.2, run all automation tests, run the in-editor acceptance check, then commit. Never commit generated folders. Push only when asked.
- Changes are delivered as patches in `Claude outputs` and applied with `git apply --check` then `git apply`; keep ShamanPrototype.umap unmodified.
