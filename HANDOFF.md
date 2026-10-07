# SHAMAN: handoff notes

Paste this file into a new chat along with the original design spec and the repository.

## Project
Single-player, third-person fantasy god-game inspired mechanically by Populous: The Beginning. The player is the Shaman; the tribe supports the Shaman. Claude acts as lead gameplay programmer under a human project manager and must not redesign core mechanics unprompted. Priorities: fun over realism, systemic gameplay over visual detail, accessibility over complexity. Build systems, not features; everything data-driven; save system designed early; stop and report after each milestone and wait for PM confirmation.

## Decisions made so far
- Engine: **Unreal Engine 4.27** (C++ core, Blueprints for content). The spec says UE5.4; the PM chose to stay on UE4. Do not migrate silently.
- **Nothing has been compiled yet.** Expect first-build fixes.
- **Blast is the same spell as Taka**: the first-level tracking (homing) fire blast.
- All 19 listed spells have data rows (16 regular + Armageddon, Teleport, Bloodlust).
- Spell range and mana keep the original 0-100 scales. World units = RangeRaw * RangeUnitsPerPoint (default 50).
- Super spells cost no mana and run on **charges** (granted by Stone Heads).
- Monuments follow the PM's screenshot (supersedes the spec's Obelisk/Shrine split): Vault of Knowledge (Shaman only, permanent spell/building plan, carries to future maps), Stone Head (Shaman or Braves, spell charges, sinks when depleted), Obelisk (Shaman only, segments spin 90 degrees, world events), Gargoyle (rare, Shaman-only charge, game-winning effect).
- Terrain for Phase 1 is a runtime procedural mesh (Landscape cannot be generated from a seed at runtime). Deformation spells will go through the same terrain actor.
- Tribe ids: player 0, enemy 1, Wildmen -1. Tribe ids must be unique per world.

## Repository layout
- `Shaman.uproject`, `Source/Shaman.Target.cs`, `Source/ShamanEditor.Target.cs`, `Config/*.ini`
- `Source/Shaman` (single module; boundaries kept as folders):
  - root `Public/` / `Private/`: original Phase 1 spell code (SpellRow, SpellComponent, SpellProjectile, HitReactionConfig, RagdollReactionComponent, Monument*, ProgressionComponent) and the Convert milestone classes (Tribe*, SpellEffect*)
  - `Core/` target rules, interfaces, debug cvar, log, placeholder visuals
  - `World/` seeded world generator, terrain, water, resources
  - `Characters/` unit base, Shaman, health, AI
  - `Tribes/` tribe definitions and UTribeSubsystem (Shamans, buildings, capacity, reincarnation)
  - `Buildings/`, `Spells/`, `Game/` (GameMode, GameData asset), `UI/` (canvas HUD), `Private/Tests/`
- `Content/Data/*.csv`: Spells, Monuments, Units, Buildings, Resources
- `Tools/WorldGenHarness`, `Tools/ConvertHarness`: run the engine-independent logic outside Unreal (`build.sh`)

## Implemented
### Original Phase 1 slice
Data-table spells with mana regen = BaseRegen * (1 + RegenPerFollower * followers) * TemporaryModifier, cooldowns, charges, homing projectile, `OnSpellCast` for non-projectile effects. Hit reactions (stagger / knockdown / ragdoll / launch). Monuments + save-ready progression.

### Phase 1 completion
- Deterministic world generation from World/Biome/Resource/Tribe/Terrain seeds (`?Seed=123` or DA_ShamanGameData): island, hills, mountains, lakes, wadeable rivers, coast; start area validated (water, wood, food, stone, ore, Wildmen, reachable enemy settlement + Shaman) with retries and a pond fallback.
- Data-driven units (Shaman, Brave, Warrior, Wildman), buildings (House, Campfire, Reincarnation Circle), resources (Tree, Berry Bush, Stone, Ore). Placeholder shapes tinted per tribe until art exists.
- Third-person Shaman (camera, Blast, melee, interact, rally), enemy Shaman using the same spell component, simple C++ AI (follow / hold / guard / wander, engage hostiles), health, drowning in deep water, death and reincarnation (time = Base / (1 + k * followers)), canvas HUD, `shaman.Debug` / F3 visualization.
- Shared target rules (`FShamanTargetRules`): friendly fire, Wildmen-only, enemies-only. Blast keeps AllUnits + friendly fire (unchanged behaviour). `FSpellRow` gained `TargetFilter` and `bFriendlyFire` columns.

### Convert milestone
- `TribeMemberComponent` (unit identity: tribe, kind, convertible, population cost; `ConvertToTribe`), `TribeComponent` (roster + capacity, on each Shaman), `TribeRegistrySubsystem` (lookup, radius queries without collision channels).
- `SpellEffect` + `SpellEffectDispatcherComponent` route `OnSpellCast` EffectIds to effects; `SpellEffect_ConvertUnits` handles `ConvertWildmen`. Capacity checked before each conversion; no damage; Wildmen only.
- In the game, every unit has a TribeMember and every Shaman a TribeComponent + dispatcher. With no `ConvertedFollowerClass`, a converted Wildman becomes a Brave in place (row, colour and orders switch). Tribe capacity = BaseCapacity + houses (pushed by UTribeSubsystem).
- Player starts with Blast only (spec). Playtest Convert with the console: `ShamanLearnSpell Convert`.
- AShamanCharacter already contains TribeComponent + SpellEffectDispatcherComponent, and AShamanUnitBase a TribeMemberComponent. Blueprint children must not add them again (duplicates would register twice / run effects twice).

## Tests
- Automation (Session Frontend > Automation, filter `Shaman.`): `Shaman.World.*`, `Shaman.Spells.*` (incl. 11 `Shaman.Spells.Convert.*`), `Shaman.Tribes.*`, `Shaman.Data.TakaPreserved`.
- Outside Unreal: `Tools/WorldGenHarness/build.sh` (3,000 seeds valid), `Tools/ConvertHarness/build.sh` (55 checks). Not yet run inside Unreal.

## Setup
1. Open `Shaman.uproject` with UE 4.27 and let it build the module (or generate VS project files first).
2. Import the CSVs in `Content/Data` as DataTables: Spells (SpellRow), Units (UnitRow), Buildings (BuildingRow), Resources (ResourceRow), Monuments (MonumentRow). Any asset name/folder works: the game finds tables by row type (preferred names DT_Spells etc. in /Game/Data). If one is missing, a red on-screen message names it. Optional: DA_ShamanGameData (ShamanGameData) for tuning.
3. Create `/Game/Maps/ShamanPrototype`: empty level with a directional light, sky light, sky atmosphere, and a NavMeshBoundsVolume covering +/-13,000 uu XY and -1,000..3,000 Z. GameMode is set globally to ShamanGameMode.
4. Play. Controls: WASD/mouse, LMB cast, RMB/F melee, E interact, Q/wheel change spell, R rally followers, F3 debug.
5. Later art: set SkeletalMesh/AnimClass in DT_Units, meshes in DT_Buildings/DT_Resources, projectile class in DA_ShamanGameData, `ConvertedFollowerClass` on a Shaman Blueprint once BP_Brave exists (must carry a TribeMemberComponent; a child of ShamanUnitBase works).

## Known issues / not done
- Uncompiled. Two read-only reviews found no definite compile errors.
- Existing Phase 1 behaviour left unchanged on purpose (needs PM approval to change): ragdoll impulse uses velocity change (BaseImpulse 20000 likely launches skeletal ragdolls very far; placeholders use a scaled launch); ragdoll collision profile not restored after recovery; Blast fizzles silently at max range (500 uu) and projectiles with range 0 never expire; homing ignores target rules.
- Terrain triangle winding defaults to the order a review expects to face up in UE (`bFlipWinding = true`); if the ground is invisible from above or units fall through, untick it on the terrain actor.
- Setting Health directly on in-place conversion does not fire OnHealthChanged (cosmetic until health UI listens to it).
- Mana regen now uses the real follower count (the old default of 3 was a placeholder).
- Not done: effect handlers for the other spells, building damage/ignite, cast time, world events, save system, tribe economy/population growth, enemy tribe economy, Shaman-kill reward, permanent enemy Shaman death (Phase 4).

## Suggested next steps
After the PM confirms the build and Convert in the editor: Invisibility (Phase 3 order: Convert, Invisibility, Magical Shield, Land Bridge, Swarm, Lightning), as a second `SpellEffect` subclass.
