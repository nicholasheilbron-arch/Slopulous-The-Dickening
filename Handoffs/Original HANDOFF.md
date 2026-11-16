# SHAMAN: handoff notes

Paste this file into the new chat along with the original design spec (not included in this zip) and the zip contents.

## Project
Single-player, third-person fantasy god-game inspired mechanically by Populous: The Beginning. The player is the Shaman; the tribe supports the Shaman. Claude acts as lead gameplay programmer under a human project manager and must not redesign core mechanics unprompted. Priorities: fun over realism, systemic gameplay over visual detail, accessibility over complexity. Build systems, not features; everything data-driven; save system designed early; stop and report after each milestone and wait for PM confirmation.

## Decisions made so far
- Engine: **Unreal Engine 4** (C++ core, Blueprints for content). Code is uncompiled; expect small first-build fixes.
- **Blast is the same spell as Taka**: the first-level tracking (homing) fire blast.
- All 19 listed spells added (16 regular + Armageddon, Teleport, Bloodlust). The source list said 20 but named 19.
- Spell range and mana keep the original 0-100 scales. World units = RangeRaw * RangeUnitsPerPoint (default 50).
- Super spells cost no mana and run on **charges** (granted by Stone Heads).
- Worshippable monuments follow the PM's screenshot, which supersedes the original spec's "Obelisk/Shrine" split:
  - Vault of Knowledge: Shaman only; permanent new spell or building plan; carries to future maps (replaces the spec's "Shrine")
  - Stone Head: Shaman or Braves; generates charges of a specific spell; sinks when depleted (this is what the spec called "Obelisk")
  - Obelisk: Shaman only; segments spin 90 degrees; triggers world events or unique rewards (open paths, alter landscape, teleport Shaman)
  - Gargoyle: rare Angel-of-Death-shaped Stone Head; only the Shaman charges it, followers still play the animation; grants a game-winning effect (five Angels of Death kill the enemy Shaman)

## Implemented (Source/Shaman)
- `SpellRow.h`, `SpellComponent`, `SpellProjectile`: data-table-driven spells, mana regen = BaseRegen * (1 + RegenPerFollower * followers) * TemporaryModifier, cooldowns, charges, homing projectile, `OnSpellCast` event for non-projectile effects.
- `HitReactionConfig`, `RagdollReactionComponent`: damage -> stagger / knockdown / ragdoll / launch; impulse = BaseImpulse * DamageMultiplier * Direction.
- `MonumentRow`, `MonumentActor`, `ProgressionComponent`: monument archetypes in a data table; reward chosen per placed instance; progression state is save-ready.
- Data: `Content/Data/Spells.csv`, `Content/Data/Monuments.csv` (import as DataTables with FSpellRow / FMonumentRow).

## Setup
1. Add the `Shaman` C++ module to the UE4 project.
2. Import the two CSVs as DataTables.
3. Blueprint child of `ASpellProjectile` for Blast; register it in the SpellComponent's `ProjectileClasses` under `FireBlast`.
4. Create a `HitReactionConfig` asset; assign it to enemies' `RagdollReactionComponent`. Add a `Ragdoll` collision profile.
5. Shaman character: `SpellComponent` (+ `ProgressionComponent`), learn `Blast`, call `CastSpell("Blast", AimPoint)`.

## Stubs / not done
- No effect handlers yet for the 18 non-projectile spells (they spend mana and fire `OnSpellCast` only).
- No world-event system (Obelisk/Gargoyle `OnWorldEvent`), no building damage/ignite, cast time unused, no health/death system.
- Damage, radius, cooldown, duration, worship times, and unlock conditions in the CSVs are placeholders for the PM to tune.
- Not started: tribe framework (3 Braves, house, campfire, reincarnation circle), procedural world, enemy tribe and Shaman, save system.

## Suggested next steps
Tribe framework, or the effect system starting with Convert, Lightning, and terrain spells.
