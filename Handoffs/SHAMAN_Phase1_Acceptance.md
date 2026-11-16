# SHAMAN – Phase 1 Acceptance (UE4.27.2)

**Result: PHASE 1 ACCEPTED** (2026-10-08, PM machine, UE4.27.2, editor PIE + Session Frontend).

## Evidence
- Automation: 32/32 Shaman tests pass (incl. Shaman.Data.TakaPreserved on the real `Spells` table, Shaman.Terrain.ShamanDeathRebirth, Shaman.Terrain.WalkableSlope, Shaman.Tribes.FollowerCountOnDeath, Shaman.Tribes.Elimination).
- ShamanPrototype (ShamanGameMode): seed 1337, Valid=1, 313 actors; settlements, resources, Wildmen, enemy tribe/Shaman/Warrior present.
- Follower count: logged per death, decrements immediately (7→6→5→4).
- Tribe elimination: enemy Shaman reborn with 2 followers; at 0 followers circle destroyed, tribe eliminated, "Level won", VICTORY banner shown.
- Player death/rebirth at own circle (flat and planet) OK.
- PlanetTest: peaks reachable, dead body stays still, clean respawn, normal movement after respawn.

## Fixes in the acceptance passes
- Pass 1: no death launch for Shamans / no-killer deaths, corpse frozen on planets, rebirth clears velocity and queued forces; planet walkable angle = FPlanetSettings::MaxWalkableSlopeDeg (50°); TAKA test resolves the production table like the game; planet-mode on-screen notice; new tests.
- Pass 2: FPlanetFrame::GetSurfaceDistance uses atan2 (acos lost ~6.9 uu at small angles); worlds without a Shaman game mode resolve production data (tests use real rows); tribe elimination + level-won state + HUD banner; per-death log line; tribe regression tests.

## Known limitations (non-blocking)
- No DA_ShamanGameData asset exists; game uses the documented fallback (C++ defaults + data tables found by row type). The 3 "Failed to load DA_ShamanGameData" warnings come from that optional lookup (pre-existing since Fix1).
- Ragdoll is the placeholder launch/lay-down stand-in (no skeletal meshes).
- Blast spawn offset and knockback up-bias use world Z (wrong on most of the planet).
- No camera zoom; no end-of-level flow beyond the VICTORY banner; no player defeat condition.
- ShamanPrototype's GameMode Override must stay None (ShamanGameMode); use PlanetTest for planet testing.
