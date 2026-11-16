# SHAMAN: spherical voxel planet research (2026-10-06)

Constraint: UE4 only (4.27.2). No UE5 APIs/plugins.

## Verified findings (from source repos; web search was unavailable)
- Voxel Plugin Free (github.com/Phyronnaz/VoxelPlugin): commit 9433a66 "Build 108" (2021-06-08) is the last UE4-era build; README: compatible with 4.24-4.27. MIT license. Includes octree LOD with transitions, invokers, chunk collision, navmesh option (Recast, Z-up), save/load objects, undo/redo, multiplayer examples, Voxel Graph, C++ generator API (GetValues/GetMaterials/GetUpVector), tools (Sphere, Flatten, Level, Smooth, Surface, Trim, Revert), Planet/HollowPlanet/LayeredPlanet examples. Unmaintained for UE4 since 2021; master is UE5.6-5.8 only. Tags 1.0 (4.23), 1.2 (4.23-4.25).
- UE4VoxelTerrain / UnrealSandboxTerrain (bw2012): GPL-3.0. UE4.27 version discontinued (pinned commit dc80a87, 2023-01). Transvoxel, runtime editing, LOD, caves; no planet/radial support (only sphere density helpers). Reference only; do not copy code.
- MiniSolarSystem (setg2002): MIT. Cube-sphere heightfield (6 TerrainFace procedural meshes, Sebastian Lague style), N-body gravity on a flying pawn. Current master is UE5 (custom 5.8 fork); older history UE4. Reference only.
- UE4-CustomGravityPlugin (mhousse1247): MIT, has a 4.27 commit (5609627, 2021-10-02). Point/surface gravity for Pawns via UFloatingPawnMovement-based component; no ACharacter, no AI/nav. Reference.
- "Project Planet": not found/verified (search blocked). Need a link from PM.

## Recommendation
- UE 4.27.2.
- Voxel Plugin Free Build 108 as terrain backend, behind our own interfaces, after a 1-2 week spike with go/no-go criteria. Keep a cube-sphere heightfield backend as fallback/test backend (PTB terrain is a heightfield; voxels are optional).
- Must build ourselves regardless: radial-gravity movement (UE4 CharacterMovement and Recast navmesh are Z-up), spherical navigation graph, planet frame utilities, deterministic planet generator, terrain command/edit log + save, hazard/fertility layer, protected regions, placement on sphere.

## Abstraction
UShamanTerrainSubsystem owns an ITerrainWorld backend. Interfaces ITerrainWorld (GetPlanetCenter/Radius, GetWaterLevel), ITerrainQuery (GetSurfaceLocation/Normal, GetTerrainHeight/Material, IsWalkable, QueryTerrain, Raycast), ITerrainModification (ModifyTerrain command, protected regions, OnTerrainChanged). FPlanetFrame helpers. Hazards separate from geometry.

## Stages
0 spike/decision; 1 abstraction + planet frame + heightfield backend + spherical worldgen; 2 radial gravity movement + camera; 3 voxel backend adapter + water; 4 spherical nav graph + AI; 5 placement; 6 terrain modification pipeline + save; 7 hazards/fertility; 8 perf.
