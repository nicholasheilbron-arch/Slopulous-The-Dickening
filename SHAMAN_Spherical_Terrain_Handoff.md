# SHAMAN – Spherical Terrain Foundation (UE4.27.2) – Handoff

## 1. Status

**BUILT AND RUN IN UE4.27.2 ON THE PM MACHINE – 27 of 28 automation tests pass; fix r2.2 addresses the one failure and must be re-run before the milestone commit.**

*First in-engine run (2026-10-06, PM machine, UE4.27.2 installed build, VS2019):* the project and the vendored Voxel Plugin compile (`Rebuild All: 1 succeeded`). The flat `ShamanPrototype` map still plays (world generated, Convert learned, Shaman reincarnates). A `PlanetTest` map with `ShamanPlanetGameMode` was played and every acceptance console command was run. Results are in section 5, taken from `Saved/Logs/Shaman.log`.

*Fix r2.2 (test harness only):* `Shaman.Terrain.SphericalMovement` failed because the unit never moved at all in the bare automation world (position unchanged after 2 s of falling). In PIE the same movement code works (306 units on the planet, 0 ground rescues). The test now disables world-bounds checks like `AShamanPlanetGameMode` does, steps the movement component directly, and first asserts that the unit actually moved, so a repeat failure points at the cause. No game code changed.

*Fix r2.1 (first real UE4.27.2 build attempt):* UnrealHeaderTool rejected the build because `UTerrainModification` (interface) and `FTerrainModification` (struct) collide once UHT strips the U/F prefixes. The interface is now `ITerrainModifier` / `UTerrainModifier`; nothing else changed. All 80 reflected types were re-checked for prefix-stripped name collisions: none remain.

*Revision 2 (packaging fix):* the spike is now delivered as a patch package against the verified Fix1 base (section 8), and the ShamanVoxel public/private dependency boundary was corrected (section 3). No gameplay or milestone scope was added.

All code for the milestone is written. The engine-independent terrain core was compiled and tested off-engine (re-run for revision 2: **20056 passed, 0 failed**, ThreadSanitizer clean; numbers in section 5). The Unreal code was reviewed line-by-line against the UE 4.27 API and the vendored Voxel Plugin headers.

The cloud sandbox that wrote the code has no Unreal Engine; all in-engine results come from the PM machine.

No milestone commit was made.

## 2. Environment

| Item | Value |
|---|---|
| Engine target | Unreal Engine **4.27.2** (installed build, `C:/Program Files/Epic Games/UE_4.27`). No UE5 API used. |
| Voxel Plugin | **Voxel Plugin Free, "Free-Beta-415230fff-2021-06-08"**. Git commit `9433a668dd568d63a338bdb2b45067c683474f96` (github.com/Phyronnaz/VoxelPluginFree). README states it is compatible with 4.24 and 4.27. Vendored **unmodified** to `Plugins/VoxelFree` (byte-identical, verified with `diff -r`). |
| Toolchain intended | Visual Studio 2019, MSVC v142, Win64 Development Editor (standard for 4.27). |
| Actually used here | Linux sandbox, g++ (C++17), used only for the off-engine harness `Tools/TerrainHarness` with a CoreMinimal shim. |
| Platform tested in-engine | Windows PC of the PM, UE4.27.2 installed build, VS2019 (MSVC 14.29), Development Editor Win64. |

## 3. What changed

### New – SHAMAN terrain layer (module `Shaman`, no Voxel dependency)

| File(s) | What it is |
|---|---|
| `Terrain/TerrainTypes.h` | Shared terrain types. `FPlanetSettings` (seed, centre, radius, sea level, noise, fordable depth, slope, voxel size). `FTerrainSample` (location, normal, up, height, material, walkable, underwater, water depth, flags). `FTerrainModification` with ops Raise / Lower / Flatten / Smooth / Paint / RaisePath. `FTerrainModificationResult`, `FTerrainProtectedRegion`, `FTerrainChangeEvent`, `FTerrainRaycastHit`. |
| `Terrain/TerrainInterfaces.h` | `ITerrainWorld`, `ITerrainQuery`, `ITerrainModifier`: native UINTERFACEs, the gameplay contract. |
| `Terrain/PlanetFrame.h` | All planet geometry in one place: radial up, gravity direction, altitude, depth below sea, tangent basis, surface distance, parallel transport. |
| `Terrain/PlanetHeightField.*` | Deterministic seeded spherical height field: hash-based gradient noise with continents, hills, ridged mountains and seabed. Holds an append-only edit log with lock-free publication for voxel worker threads, a signed distance function, and conservative height bounds. Engine-independent. |
| `Terrain/PlanetTerrainQueries.*` | Samples, raycast (sphere tracing + bisection), protected-region overlap, edit footprint. Engine-independent. |
| `Terrain/ShamanTerrainBackend.*` | `UShamanTerrainBackend` (abstract backend contract) and `UAnalyticPlanetTerrainBackend` (pure math; used by tests/headless and as the query layer of the voxel backend). |
| `Terrain/ShamanTerrainSubsystem.*` | `UShamanTerrainSubsystem` (UWorldSubsystem + tickable). Implements all three interfaces. Handles validation, protected regions, timing, `OnTerrainChanged` (sync) and `OnTerrainUpdateCompleted` (after mesh/collision rebuild), edit-log save/replay, and stats. |
| `Terrain/ShamanSpace.*` | `FShamanSpace`: the gameplay helpers that replaced every world-Z call site. Gives exactly the old flat math when no planet is active. |
| `Characters/ShamanCharacterMovementComponent.*` | Radial gravity. Walking/Falling are redirected to `MOVE_Custom` PlanetWalk/PlanetFall: tangent-plane movement, floor sweep along −Up, capsule aligned to radial up, radial jump, rotation about local up. Uses the analytic ground as a safety net when collision is missing or late. On flat worlds every override calls `Super`. |
| `Navigation/ShamanSurfaceNavigation.*` | `IShamanSurfacePathfinder` extension point, plus `FGreatCirclePathfinder`. That pathfinder is a **prototype**: a great-circle line sampled for walkability; it gives a partial path at water or cliffs and never routes around obstacles. |
| `World/PlanetWaterActor.*` | Debug icosphere sea at sea-level radius. Visual only. |
| `World/SurfaceProbeActor.*` | Test surface object. Snaps to the terrain, aligns to the normal, and re-aligns on `OnTerrainChangedNative`. |
| `Game/ShamanPlanetGameMode.*` | Spike game mode. Builds the planet, then places a Reincarnation Circle (protected region), the player, a following Brave, Wildmen and the probe. Provides the acceptance console commands. |
| `Tests/ShamanTerrainTests.cpp` | 10 automation tests (`Shaman.Terrain.*`). |

### New – module `ShamanVoxel`: the only Voxel-Plugin-specific code

- **`UShamanPlanetVoxelGenerator`** (`Source/ShamanVoxel/Private/ShamanPlanetVoxelGenerator.h/.cpp`) and its instance turn the height field into a signed-distance volume, with value-range culling, RGB material colours and radial `GetUpVector`. It derives from `UVoxelGenerator`, so it is **private** to the module.
- **`UVoxelPluginTerrainBackend`** (derives from the analytic backend):
  - spawns an `AVoxelWorld` at the planet centre, with octree depth sized to the planet plus edit headroom;
  - turns on collision for invoker and visible chunks;
  - attaches a player-pawn invoker;
  - remeshes the edit box after each edit (`ClearCachedValues/Materials` + `UpdateBounds`);
  - reports pending work (`GetTaskCount`, `IsVoxelWorldMeshLoading`);
  - records generation and initial-mesh timing.
- **Dependency boundary (revision 2, option B: smallest correct fix).** Revision 1 had `ShamanPlanetVoxelGenerator.h` in `Public/`, including Voxel headers and deriving from `UVoxelGenerator`, while `ShamanVoxel.Build.cs` listed `Voxel` as a *private* dependency – an inconsistent boundary. Fix:
  - `ShamanPlanetVoxelGenerator.h` moved to `Source/ShamanVoxel/Private/` (only the backend .cpp uses it; `SHAMANVOXEL_API` removed).
  - `VoxelPluginTerrainBackend.h` (the only public header) no longer names any Voxel type: the voxel world, generator and invoker are held as `AActor*` / `UObject*` / `UActorComponent*` and cast in the .cpp. `GetVoxelWorld()` became `GetVoxelWorldActor()` returning `AActor*` (nothing called it).
  - Result: no public ShamanVoxel header includes a Voxel header or names a Voxel type, so `Voxel` correctly stays in `PrivateDependencyModuleNames`. `Shaman` stays a public dependency (the public header derives from `UAnalyticPlanetTerrainBackend`).
- The `Shaman` module never includes Voxel headers and calls no Voxel API (verified by grep: only comments, and the soft class path string `/Script/ShamanVoxel.VoxelPluginTerrainBackend` in `ShamanPlanetGameMode.cpp`). The game mode falls back to the analytic backend if that class is missing.
- The **analytic backend is kept** (`UAnalyticPlanetTerrainBackend`): it is the off-engine test backend, the deterministic terrain source the voxel backend reads from, the runtime fallback, and the reference for a future cube-sphere backend.

### Modified (minimal, isolated)

- **`ShamanUnitBase`**
  - Uses the new movement component through `FObjectInitializer`; the constructor signature changed.
  - Melee distance and facing, death launch direction and the reincarnate pose go through `FShamanSpace`.
  - The planet branch of `UpdateWater` measures water depth and out-of-world radially.
- **`ShamanCharacter`**
  - Planet camera: absolute-rotation boom built from radial up, with a parallel-transported view forward and its own yaw/pitch.
  - `Turn`/`LookUp` now go to new handlers that call `AddController*Input` on flat worlds.
  - Movement input on planets.
  - Aim fallback, cast facing and focus scoring go through `FShamanSpace`.
- **`ShamanUnitAIController`**
  - `Dist2D`, XY offsets and wander go through `FShamanSpace` (same RNG call order).
  - On planets, steers every frame through `FShamanSurfaceNavigation`, with decisions still at 4 Hz.
- **`ShamanGameMode`**: `EnsureWorldGenerated` is now virtual. `ShamanRegenerate` now calls a virtual `RegenerateWorld`.
- **`Shaman.uproject`**: adds module `ShamanVoxel` and plugin `VoxelFree`. Your EngineAssociation GUID is preserved.
- **Target files**: add `ShamanVoxel`.
- **`.gitignore`**: adds the harness binary.
- **`.gitattributes`**: `Plugins/VoxelFree/** -text`, so the vendored files stay byte-identical.

### Untouched

- Spell framework, TAKA Blast (`SpellComponent`, `SpellProjectile`).
- Convert (`SpellEffect_ConvertUnits`, `TribeMemberComponent`, `TribeComponent`, `TribeRegistrySubsystem`).
- Tribe data, data tables, HUD, `RagdollReactionComponent`.
- The flat world generator and the existing tests.

## 4. Architecture

```
Gameplay (units, AI, spells, buildings, game modes)
   │  FShamanSpace (up / ground distance / upright rotation), ITerrainWorld/Query/Modification
   ▼
UShamanTerrainSubsystem  ── protected regions, validation, events, edit log, stats
   │  UShamanTerrainBackend (abstract)
   ▼
UAnalyticPlanetTerrainBackend ── FPlanetHeightField (source of truth) + FPlanetTerrainQueries
   │  (subclass, ShamanVoxel module only)
   ▼
UVoxelPluginTerrainBackend ── UShamanPlanetVoxelGenerator ── AVoxelWorld (Voxel Plugin Free)
```

**Key decision: the height field, not voxel data, is the source of truth.**

- Queries, raycasts, determinism and saves (seed + edit log) never depend on voxel state.
- The voxel world only turns the field into LOD meshes and collision.
- A cube-sphere heightmap backend can replace it by subclassing the analytic backend; gameplay code does not change.
- The Voxel-specific code is exactly `Source/ShamanVoxel/**`, plus the vendored plugin.

## 5. Acceptance results

In-engine evidence: PM machine, 2026-10-06, editor PIE on `Content/Maps/PlanetTest` (GameMode Override `ShamanPlanetGameMode`, voxel backend), Session Frontend automation run of the `Shaman` group. Off-engine numbers are from the g++ harness (revision 2).

| # | Test | Result | Evidence / measurement |
|---|---|---|---|
| A | Project compiles (incl. Voxel Plugin) | **PASS** | `Rebuild All: 1 succeeded, 0 failed` after fix r2.1. Only upstream plugin C4996 deprecation warnings. |
| A | Project opens, flat map unaffected | **PASS** | `ShamanPrototype` PIE x3: world generated in ~10 ms, 313 actors, `Learned Convert`, `Shaman of tribe 0 reborn in 8.6s`. |
| A | No UE5 dependencies | PASS (static) | No UE5 API or modules. |
| B | Same seed → same planet | **PASS** | `Shaman.Terrain.Determinism` passed in UE; harness checksum `21358966.752`. `Shaman.World.Determinism` (flat) passed. |
| B | Different seeds differ | PASS (off-engine) | Harness: >3000/4000 samples differ by more than 1 uu. |
| C | Movement on the sphere (land, walk across curvature, align, jump) | **FAIL in automation → fix r2.2, re-run pending** | `Shaman.Terrain.SphericalMovement`: unit did not move in the test world (clearance stayed 311.7, walked 0 uu). In PIE: player standing on the surface (altitude 749, ground 656, up.z 1.00); 306 units on the planet with 0 ground rescues. |
| C | No world-Z in the core spherical movement path | PASS (review) | PlanetWalk/PlanetFall/rotation/jump use only `FPlanetFrame`. |
| D | Surface position / normal / height / walkability / underwater | **PASS** | `Shaman.Terrain.SurfaceQuery`, `PlanetCenterRadius`, `RadialUp` passed. `ShamanTerrainProbe`: height 616, Grass, walkable, normal.up 0.97, raycast hit at 2998. |
| E | Runtime terrain modification | **PASS** | `ShamanTerrainBenchmark 8`: 8 edits r=500 applied, apply avg 0.094 / max 0.174 ms. `Shaman.Terrain.ModificationRequest` passed. |
| E | Collision updates < 100 ms | **PASS** | Mesh + collision up to date avg 39.6 / max 50.3 ms per edit (target 100 ms). |
| E | Change event fires | **PASS** | `Shaman.Terrain.ChangedNotification` passed; `SurfaceProbe … re-aligned after terrain edit` logged. |
| E | Protected region rejects | **PASS** | `ShamanTerrainProtectedTest: PASS` (centre and edge rejected, edits 0→0); `Shaman.Terrain.ProtectedRejection` passed. `ShamanTerrainRaise 500 300` aimed at the start site was also correctly rejected (region 1). |
| F | Spherical sea / underwater / depth | **PASS** | `Shaman.Terrain.Underwater` passed. Visual check of the water sphere not reported. |
| G | Brave on the surface | PASS (indirect) | 6 units at start, 0 ground rescues. Not visually confirmed in the report. |
| G | Surface object conforms to terrain | **PASS** | `SurfaceProbeActor_0 re-aligned after terrain edit 6/7/-1`. |
| G | Spells / TAKA Blast / Convert / tribe data intact | **PASS** | All `Shaman.Spells.*` (incl. `Convert.BlastUnchanged`), `Shaman.Data.TakaPreserved`, `Shaman.Tribes.PopulationFormulas`, `Shaman.World.StartAreaConstraints` passed. Note: `TakaPreserved` logged "DT_Spells not imported yet; skipped" (pre-existing). |
| H | Generation time | **PASS** | Voxel world created in 43.9 ms; start site + content 2.2 ms. |
| H | 300 units | **PASS** | `ShamanSpawnStress 300`: 306 units, steady 105–112 fps (worst frame 12–19 ms) over one minute. One unit was lost (306→305), cause not logged. |
| H | Save / reload | **PASS** | `SaveReload: PASS (8 edits, hash 69505468 -> 69505468, replay 139.08 ms)`; full remesh after reload 1275 ms (load-time, not per edit). |
| H | FPS / memory | PASS, with a note | 80–120 fps typical, ~1.29–1.39 GB editor process. Recurring single ~335 ms frames and two 5-s windows at 3.0 fps match the editor's background throttling when its window loses focus; they did not occur during the focused minute of the stress run. Re-check in a standalone game if needed. |

Automation summary: 28 run, 27 passed, 1 failed (`Shaman.Terrain.SphericalMovement`, addressed by fix r2.2).

## 6. Known limitations

- **Navigation.** No navmesh on planets.
  - `FGreatCirclePathfinder` walks a straight great-circle line and stops at water or cliffs (partial path). It never routes around obstacles. This is enough for a Brave following the Shaman over open land, and nothing more.
  - The next step is a globe-wide graph (cube-sphere cells or an icosphere grid) with A*, walkability from `ITerrainQuery`, and invalidation via `OnTerrainChanged`.
- **Follower movement.**
  - RVO avoidance is turned off on planets because it is 2D in world XY.
  - AI steering is direct `AddMovementInput`, with no stuck detection.
  - The rest of the AI state machine is unchanged.
- **Ragdoll and other world-Z code, left as is.** These systems are TAKA-adjacent or out of scope, so they were not changed:
  - `RagdollReactionComponent`: `UpwardBias` uses `FVector::UpVector`; `EndRagdoll` uses Z offsets; physics ragdolls fall toward world −Z.
  - `SpellComponent.cpp:86`: Blast spawns at +60 Z. It still flies to the aim point, but the start is offset sideways or down on most of the globe.
  - `BuildingActor::GetRebirthLocation` adds +100 Z (`Reincarnate` re-projects onto the ground on planets).
  - The flat `AShamanGameMode`, world generator, water actor and debug drawing are flat-only by design.
- **Streaming and LOD.**
  - Full-resolution LOD and collision exist only around the player (invoker ranges 3000 / 5000 uu).
  - Elsewhere, collision only exists on visible chunks up to LOD 3. Units far away stand on coarse collision or on the analytic ground (movement safety net), so they may float or sink visually by up to roughly a coarse voxel.
  - The voxel world cannot grow after creation; raises are limited by `EditHeadroom` (4000 uu).
- **Save.**
  - Terrain save is `FPlanetSettings` + the edit log (`GetEditLog` / `ReplayEditLog`). It is not wired into a game save system (out of scope).
  - The edit log has 4096 slots. With the voxel backend, a replay consumes new slots (worker-thread safety), so slots are only reclaimed on world rebuild.
- **Performance.**
  - Edit cost is linear in the edit count for every SDF evaluation (no spatial bucketing yet).
  - The first full planet mesh is asynchronous; the time is logged as `initial meshes + collision ready after … ms`.
  - The completion timing is an upper bound: it includes frame time and any unrelated LOD work in flight.
- **Terrain ops.** Smooth and RaisePath are declared but rejected as `RejectedUnsupported`. Protection already covers RaisePath footprints.
- **Content.**
  - Upstream's GitHub copy of the plugin lacks 53 large example assets (`*.REMOVED.git-id` placeholders). This only affects the plugin's example maps, not runtime.
  - The terrain material is the plugin's RGB example material, with a fallback to the engine default.
- **World bounds.** The planet game mode disables `bEnableWorldBoundsChecks`, because the southern hemisphere lies below KillZ.

## 7. Licensing

| Component | License | Notes |
|---|---|---|
| Voxel Plugin Free (commit `9433a66`) | **MIT**, © 2020 Phyronnaz | `Plugins/VoxelFree/LICENSE.txt` kept. Matches the expected license. |
| FastNoise | MIT, © 2017 Jordan Peck | `Source/Voxel/Private/FastNoise/LICENSE` kept. |
| Transvoxel tables | Eric Lengyel, free use with attribution | Attribution kept in `Transvoxel.h`. |
| sorttable.js | X11/MIT | Editor/debug report helper only. |
| OpenVDB headers (`VoxelVDB/Private/OpenVDB7`) | **MPL-2.0** (Ken Museth) | Used only by the editor-only, Win64 `VoxelVDB` module, which links the engine's bundled OpenVDB. **Flag for legal review** if a shipping build ever includes it. Easy option: disable `VoxelVDB`/`VoxelVDBEditor` in the uplugin. |
| GPL code | None | UE4VoxelTerrain / UnrealSandboxTerrain were not used or copied. |

The plugin also depends on engine modules HTTP, Networking and Sockets; their use was not audited for runtime network calls.

## 8. Git state and patch package

### Verified base (the user's real Fix1 project)

- Project folder: `C:\Nick\other forms of comp\fun\Shaman\1st Go\Merged 1st steps\Shaman 4.27`.
- All **73 files under `Source/`**, plus `.gitignore` and `.gitattributes`, were copied from that folder and hashed (SHA-256). They are **byte-identical** to merge-repo commit `eec16ab` ("Find data tables by row type, log missing rows once, clearer spell/learn feedback" – the Fix1 state). This includes `Source/Shaman/Shaman.Build.cs`, `Source/Shaman/Private/Shaman.cpp` and all existing module source.
- **One difference:** your `Shaman.uproject` was rewritten by the editor (CRLF line endings, expanded JSON, EngineAssociation GUID `{03AFFC7F-4036-DCA1-5300-3A8BDD054D06}`). The patch is made against **your** file, not the repo's.
- Your Git repo (`main`, `993fdd0 Initial Commit`) is behind this working folder (its index lacks Fix1 and the imported `Content/*.uasset`). Its state was not touched.
- `BASE_MANIFEST.sha256` in the package lists every base file with its hash, so the base can be re-checked before applying.

### What the package contains

| File | Purpose |
|---|---|
| `SPIKE_CHANGED_FILES.txt` | Every file the spike touches: MODIFIED / NEW / DELETED (no deletions). |
| `SPIKE_CHANGES_tracked.diff` | Complete line-level unified diff of the 13 MODIFIED existing text files. |
| `SPIKE_NEW_FILES.patch` | Unified diff creating every NEW SHAMAN text file (not the vendored plugin). |
| `files/` | Exact post-spike copies of every modified and new file, including `Plugins/VoxelFree/` (unmodified upstream, binary assets). |
| `BASE_MANIFEST.sha256` | Hashes of the base files the patch expects. |
| `APPLY.md` | Step-by-step application instructions. |

Nothing in the package replaces or re-creates an existing project file that the spike does not modify.

### Repository state

- **No commit of any kind was made** (no milestone commit, no merge into main, nothing pushed, no remote created). The user's repository was not modified.
- The work also exists uncommitted on branch `terrain/spherical-spike` of my merge repo (base `eec16ab`), with `Shaman.uproject` updated to your editor-written version plus the two additions.
- **Recommended order on your machine:**
  1. Commit your current Fix1 state (source, data tables, map) as its own commit.
  2. Verify the base with `BASE_MANIFEST.sha256`, then apply this package (see `APPLY.md`).
  3. Build and run section 10.
  4. Only then commit `terrain: add UE4 spherical terrain foundation`.
- **Excluded by `.gitignore`:** Binaries, Intermediate, Saved, DerivedDataCache, Build, `Plugins/*/Binaries`, `Plugins/*/Intermediate`, `.vs`, `*.sln`, the harness binary. Plugin uassets go through LFS (existing `.gitattributes`); `Plugins/VoxelFree/** -text` keeps vendored files byte-identical.

## 9. Next recommended milestone

1. **Validation pass (blocking).** Build, run section 10, record the real numbers, fix whatever breaks, then make the milestone commit.
2. **Then, if the gates pass:** globe-wide navigation, covering:
   - a surface graph + A* behind `IShamanSurfacePathfinder`;
   - re-pathing on `OnTerrainChanged`;
   - stuck handling;
   - radial versions of the remaining world-Z call sites (ragdoll bias, Blast spawn offset, rebirth offset).
3. **If the voxel edit or collision timing fails the 100 ms gate:** prototype the cube-sphere heightmap backend behind the same interfaces before going further.

## 10. Runbook for the PM (to produce the missing results)

### 1. Build

1. Apply the patch package to the Fix1 project (`APPLY.md`).
2. Right-click `Shaman.uproject` → Generate Visual Studio project files.
3. Build Development Editor Win64.
4. Record any errors verbatim.

The Voxel Plugin is a large first build (~10–20 min).

### 2. Test map

1. File → New Level → Empty Level, and save it as `Content/Maps/PlanetTest`.
2. World Settings → GameMode Override = **ShamanPlanetGameMode**.
3. Lights are auto-spawned if missing.

Alternatively, run `open PlanetTest?game=/Script/Shaman.ShamanPlanetGameMode`.

### 3. Automation tests

Session Frontend → Automation → run `Shaman.` (all). Record PASS/FAIL per test, especially:

- `Shaman.Terrain.*` (10)
- `Shaman.Spells.*`
- `Shaman.Spells.Convert.*`
- `Shaman.World.*`
- `Shaman.Data.TakaPreserved`

### 4. Play-in-editor on PlanetTest

1. Wait for `Terrain(Voxel): initial meshes + collision ready after … ms`.
2. Walk far in one direction (the horizon curves; up stays radial). Jump.
3. Check that the Brave follows.
4. Convert the Wildmen.
5. Blast a Wildman.
6. Walk into the sea and confirm drowning.

### 5. Console commands

| Command | What to record |
|---|---|
| `ShamanTerrainReport` | Baseline. |
| `ShamanTerrainProbe` | The aimed sample. |
| `ShamanTerrainRaise 500 300` | Visible bump, collision, the probe re-aligns if nearby. |
| `ShamanTerrainProtectedTest` | Expect **PASS**. |
| `ShamanTerrainBenchmark 8` | Apply and update ms vs the 100 ms target. |
| `ShamanTerrainSaveReload` | Expect **PASS**. |
| `ShamanSpawnStress 300` | Wait 30 s, then `ShamanTerrainReport` and the `Perf:` lines. |

### 6. Paste the log back

Send `Saved/Logs/Shaman.log`. These results fill in section 5.
