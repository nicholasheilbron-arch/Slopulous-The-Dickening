# SHAMAN: status for future sessions (updated 2026-10-05)

- Engine decision (PM): **stay on UE 4.27** (spec says UE5.4). Do not migrate silently.
- **Current source of truth: Shaman_Merged.zip** (PM is starting a git repository from it). It merges the Phase 1 completion work and the Convert milestone; Convert's classes win where they overlap.
  - Unit identity: UTribeMemberComponent on every AShamanUnitBase. Follower roster and capacity: UTribeComponent on every AShamanCharacter (+ SpellEffectDispatcherComponent). UTribeSubsystem keeps Shamans, buildings, capacity (BaseCapacity + houses, pushed into UTribeComponent) and reincarnation.
  - Tribe ids: player 0, enemy 1, Wildmen -1.
  - Includes .uproject, Target.cs files, Config ini, .gitignore, .gitattributes (LFS), HANDOFF.md (full setup + known issues).
- Nothing compiled in Unreal yet. Outside-engine harnesses pass (world gen 3,000 seeds; Convert 55 checks). Three read-only code reviews applied.
- Waiting for PM to build in the editor and confirm Phase 1 + Convert.
- Next per spec Phase 3 order: Invisibility, then Magical Shield, Land Bridge, Swarm, Lightning (one at a time, PM-approved), each a USpellEffect subclass.
- Monument names follow the handoff (Vault of Knowledge / Stone Head / Obelisk / Gargoyle).
