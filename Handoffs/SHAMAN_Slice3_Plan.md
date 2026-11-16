# SHAMAN Slice 3 Plan — Civilization Building (UE4.27.2)

Nov 6, 2026 · @Nicholas Heilbron

## Status and decisions

Slice 3 turns the tribe into a working settlement. Braves gather wood, stone, ore and food, build and upgrade huts, and keep the tribe happy with water and food. Training huts, transport and worship sites follow. It is split into 8 milestones, each built, tested and accepted before the next one starts. Milestone 3.1 (resources, gathering and stockpile) is delivered with this plan as a patch; **nothing is built or verified in UE4.27.2 yet.**

| Decision | Choice |
| --- | --- |
| Delivery | This plan first, then milestone 3.1 as a patch; later milestones one at a time after approval |
| Stone and ore | Tiered costs: basic buildings cost mostly wood; upgrades, towers, the Temple and the Well need stone; Warriors and Firewarriors cost ore per trained unit |
| Water and happiness | One happiness score (water, food, housing) that speeds up or slows hut Brave production and scales follower mana. Nobody dies from it |
| Baseline | `feature/rts-stage-a` at `2061fec` plus your uncommitted guard/patrol flags patch and the mana rounding fix |
| Engine | UE4.27.2 only; C++ authoritative; every value in a DataTable or `DA_ShamanGameData` |
| Out of scope | Enemy tribe economy and raids (Phase 4), terrain spells, save game |

Slice 3 covers what the original spec calls Phase 2 (gathering, construction, food, shelter, water, population growth, storage) plus the building half of Phase 3 (unit training, multiple resource types, Vault and Stone Head unlocks). The new spells of Phase 3 stay in their own slice.

## Design rules kept from Populous

Five Populous rules shape every milestone; together they make each Brave a choice between labour, mana and an army.

1. **Occupancy drives mana.** A hut makes no mana by itself. Braves resting inside it do, at the existing "resting in a hut" rate (34.6 mana/min). A bigger hut holds more of them.
2. **Training consumes mana.** Training huts draw from the same new-mana stream as spell charging, through the 50/50 split the mana economy already has (`ActiveTrainingConsumers`).
3. **Construction is physical labour.** Braves walk to a node, gather, carry materials to the blueprint and work there. Build time comes from work units, worker count, travel and supply, never a fixed timer.
4. **Each building accepts specific inputs.** A Warrior Hut takes followers to train, a Boat House takes Braves as workers, a Vault takes only the Shaman.
5. **No refunds.** Buildings are damaged or destroyed, never dismantled for materials. Damage sends a building back toward its construction state and Braves repair it.

Our version adds stone and ore as building materials and a happiness score from water, food and housing. Both reuse the same gather, carry and deliver loop as wood.

## Resources and costs

Wood is still the main material; stone gates upgrades and defences, and ore gates the two strongest fighters. All quantities are bundles, one bundle per Brave trip, and every number below is a starting value in a DataTable.

| Resource | Source | Per node | Regrows | Spent on |
| --- | --- | --- | --- | --- |
| Wood | Trees | 4 | 1 bundle every 90 s, back to 4 | Every building, boats, balloons |
| Stone | Stone outcrops | 12 | No | Hut upgrades, Guard Tower, Temple, Well, Balloon Hut |
| Ore | Ore deposits | 8 | No | Warrior and Firewarrior training |
| Food | Berry bushes | 6 | 1 every 60 s | Happiness (milestone 3.4) |
| Water | Lakes, sea shore, Wells | Not gathered | n/a | Happiness (milestone 3.4) |

| Building | Build cost | Per use | Notes |
| --- | --- | --- | --- |
| Small Hut | 3 wood |  | Houses 3 |
| Medium Hut (upgrade) | +1 wood, 2 stone |  | Houses 4 |
| Large Hut (upgrade) | +2 wood, 3 stone |  | Houses 5 |
| Guard Tower | 4 wood, 2 stone |  | Extends the build zone |
| Well | 2 wood, 4 stone |  | Water source for nearby huts |
| Warrior Hut | 6 wood, 2 stone | Mana + 1 ore per Warrior |  |
| Temple | 5 wood, 3 stone | Mana per Preacher |  |
| Firewarrior Hut | 6 wood, 2 stone | Mana + 1 ore per Firewarrior |  |
| Spy Hut | 6 wood, 2 stone | Mana per Spy |  |
| Boat House | 5 wood | 4 wood per boat | Coastal only |
| Balloon Hut | 9 wood, 2 stone | 3 wood per balloon |  |
| Guard flag | Free |  | Already in the game |

Populous charges 8 wood for each training hut and 11 for the Balloon Hut. Here part of that cost moves to stone, so totals stay close while stone becomes worth gathering.

## Structure catalogue

Every structure is a row in `DT_Buildings` run by the existing `ABuildingActor`; its behaviour comes from row flags and small components, never from a subclass per building.

| Category | Structure | Accepts | Produces | Mana | Milestone |
| --- | --- | --- | --- | --- | --- |
| Infrastructure | Reincarnation Circle | Nothing | Rebirth; build zone centre | None | Exists |
| Infrastructure | Campfire | Resource deliveries | Drop-off point; gathering call | None | 3.1 |
| Housing | Small, Medium, Large Hut | Braves to rest; materials to upgrade | Housing 3/4/5; a new Brave when below capacity | Resting occupants | 3.3 |
| Water | Well | Nothing | Water for huts within its radius | None | 3.4 |
| Defence | Guard Tower | One follower or the Shaman | Build zone; occupant bonus | None | 3.5 |
| Defence | Guard flag | Selected followers | Patrol and guard point | None | Exists |
| Training | Warrior Hut, Temple, Firewarrior Hut, Spy Hut | One follower at a time, plus a queue | Converts it to the specialist | Consumes | 3.6 |
| Transport | Boat House | Up to 4 Braves; wood | Boats (5 seats) | None | 3.7 |
| Transport | Balloon Hut | Up to 6 Braves; wood | Balloons (2 seats) | None | 3.7 |
| Knowledge | Vault of Knowledge | Shaman only | Permanent spell or building unlock | None | 3.8 |
| Spiritual | Obelisk | Shaman only | Mana burst, spell refill or world event | One-off | 3.8 |
| Spiritual | Totem Pole | 1 to 6 followers | Level-specific world effect | None | 3.8 |
| Spiritual | Stone Head | 1 to 6 followers | Single-use spell charges | None | 3.8 |

Guard Tower occupant bonuses (per Populous):

- **Shaman:** longer spell range.
- **Brave:** longer lookout.
- **Warrior:** calls reinforcements.
- **Preacher:** longer conversion range.
- **Firewarrior:** longer attack range.
- **Spy:** detects enemy spies from further away.

Preachers, Firewarriors and Spies do not exist as units yet: 3.6 adds their unit rows and the smallest behaviour each needs. The Magical Prison is a spell effect, not a building, so it stays with the spells.

## Happiness and water

A tribe's happiness is one number from 0 to 1, recomputed every few seconds from water, food and housing. It changes how fast huts make Braves and how much mana followers make, and it never kills or deserts anyone.

```latex
H = w_{water} \cdot W + w_{food} \cdot F + w_{housing} \cdot S \qquad (w_{water} = 0.4,\ w_{food} = 0.3,\ w_{housing} = 0.3)
```

| Factor | Measures | 1.0 when |
| --- | --- | --- |
| Water W | Share of huts with water in reach | Every hut is within 2,500 uu of a Well, lake or shore |
| Food F | Food stock against what the tribe eats | The store covers 3 minutes of eating (0.2 food per follower per minute) |
| Housing S | Share of followers with a bed | Every follower fits in a hut |

| Effect | At H = 0 | At H = 0.5 | At H = 1 |
| --- | --- | --- | --- |
| Hut Brave production speed | ×0.5 | ×1.0 | ×1.5 |
| Follower mana | ×0.85 | ×1.0 | ×1.15 |

At 0.5 the tribe makes exactly what it makes today, so balance is unchanged until a player builds Wells or neglects food. The Shaman's own 66.6 mana/min is never scaled. The HUD shows the score and its weakest factor (for example "Thirsty: build a Well").

## Architecture

Slice 3 extends the systems already in the game rather than adding parallel ones: the tribe's task book, the Phase 2.2 task executor, `ABuildingActor` rows and the mana economy.

&#91;embedded content: milestone 3.1 · order to stockpile\]

A gather order is an ordinary task, so dying, being converted or getting a new order ends it exactly like a move or guard task does today.

| Part | Change | Milestone |
| --- | --- | --- |
| `UTribeComponent` | Owns the stockpile (`FTribeStockpile`): add, spend, can-afford, change event | 3.1 |
| `AResourceNode` | Gatherer slots, harvest a bundle, regrow, depleted state | 3.1 |
| `UTribeMemberComponent` | Carries one bundle (type + amount); a dying carrier drops it | 3.1 |
| `FUnitTaskExecutor` | New `Gather` kind with a pure, tested phase machine (`FGatherRules`) | 3.1 |
| `ABuildingActor` / `DT_Buildings` | `bIsResourceDropOff`; later cost, housing, build work, training columns | 3.1 to 3.6 |
| `AShamanPlayerController` | RMB on a resource node with Braves selected gives a gather order | 3.1 |
| `ABuildingSite` (new) | Blueprint that collects materials and work, then becomes the building | 3.2 |
| `UHutComponent`, `UTrainingComponent` (new) | Occupancy and Brave production; training queue | 3.3, 3.6 |
| `UTribeHappiness` (new, in the tribe) | Computes H and exposes the two multipliers | 3.4 |
| `UWorshipComponent` (new) | Worshipper count, progress, reward, on the existing monuments | 3.8 |

## Milestones

Eight milestones, built in this order: 3.1 now, then 3.2 to 3.8 one at a time after your approval. Every gate is your build, the full automation run with no regressions, and the in-editor check written into that milestone's handoff.

&#91;embedded content: Slice 3 milestones · 8, with gates\]

1. **3.1 Gathering and stockpile (this patch).** Resource nodes with gatherer slots and regrowth; the tribe stockpile; the gather loop; RMB gather orders; HUD stockpile line.
2. **3.2 Construction.** Place a blueprint inside the build zone (Reincarnation Circle radius), Braves carry materials and add work. A damaged building loses progress; Braves repair it.
3. **3.3 Huts and population growth.** Small, Medium and Large huts; idle Braves rest inside; resting occupants make mana; a hut below capacity makes a Brave over time; the Phase 1 House becomes a Small Hut.
4. **3.4 Water, food and happiness.** Well building, lake and shore water, food store and eating, the happiness score and its two multipliers.
5. **3.5 Guard Tower and settlement expansion.** Towers extend the build zone, raise an alarm and give occupant bonuses.
6. **3.6 Training huts.** Warrior Hut, Temple, Firewarrior Hut and Spy Hut with queues; training draws mana and ore; Preacher, Firewarrior and Spy unit rows.
7. **3.7 Transport.** Boat House and Balloon Hut, boats and balloons as vehicles units can board.
8. **3.8 Worship sites.** Vault of Knowledge, Obelisk, Totem Pole and Stone Head driven by worship tasks; Vault unlocks gate which buildings the player may place.

The enemy tribe keeps its fixed starting settlement through Slice 3; its gathering and building are Phase 4.

## Open questions for you

None of these block 3.1; each needs an answer before the milestone named.

- [ ] **3.2:** Does construction spend from the stockpile (Braves fetch from the Campfire), or do Braves still chop and deliver straight to the blueprint as in Populous? The plan assumes the stockpile, with Braves carrying from it.
- [ ] **3.3:** Should the Phase 1 starting House become a Small Hut (3 beds), or start as a Medium Hut so the opening tribe size is unchanged?
- [ ] **3.4:** Is 2,500 uu the right water reach for a hut, and should the sea count as water or only fresh lakes and Wells?
- [ ] **3.6:** Which unlock source gates each training hut: Vault of Knowledge only, or available from the start on some maps?
- [ ] **3.7:** Boats and balloons need vehicle movement on water and in the air. Do you want them in Slice 3, or moved to the slice with Land Bridge?
