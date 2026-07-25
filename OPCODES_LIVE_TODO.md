# Unresolved opcodes (live)

`showeq-daemon/conf/zoneopcodes.xml` has **222 of 275** zone opcodes still set to `id="ffff"`. `worldopcodes.xml` is clean.

Target server: **live EQ** (Tasks/AA/DZ/Tribute/Fellowship/Marketplace/Mercenaries/etc. all in scope).

Confirmation bar: see `feedback_opcode_disambiguation.md` — count + zero-competitor over n>=5; n=2-3 is enough if `--opcode-stats` reports no other unknowns at the target size+direction.

Checkbox legend: `[ ]` unresolved, `[x]` resolved, `[~]` superseded / obsolete on current Live (won't be hunted; eligible for batch removal — `grep '\[~\]'` to list).

---

## ⤺ Re-hunt after the 2026-07-25 re-port to legacy 6.4.25 (07/15 patch)

The Live opcode table was reset to legacy `upstream/master` (07/15/26). These six
were hand-discovered in the PREVIOUS patch cycle, are NOT in legacy, and their old
ids point at different opcodes on the current patch — so they were set to `ffff` to
avoid size mismatches. Their full hunt notes survive in the `conf/opcodes.toml`
comment blocks (grep `STALE post-07/15`). Re-hunt each on a capture taken on the
CURRENT patch (per `feedback_opcode_hunt_no_size_match` — content-match, don't gate
on size):

- [ ] OP_SpawnAppearance2 — was `0x8cd3`. 24B `spawnAppearance2Struct{spawn_id,type,value,pad}`; type=0x2c S>C = TLP mob-lock/FTE flag (value 0=attackable / 1=locked). **Only wired one** — drives `Spawn.locked` → web; dormant on Live until re-found.
- [ ] OP_BuffQuery — was `0x306c`. C>S 8B `{buff_slot, self_spawn_id}`; fires per occupied buff slot on the SECOND zone-in after a buff was applied.
- [ ] OP_GroupInviteAccepted — was `0x59ab`. S>C to inviter when invitee accepts an ungrouped invite; 168B, invitee name @64. (unverified last cycle)
- [ ] OP_GroupInvite2Sent — was `0xe2b7`. S>C to leader when a grouped invite is sent (before accept); 168B, both names.
- [ ] OP_GroupMemberLeave — was `0x1325`. S>C broadcast when a member leaves (≥2 remain); 168B, name @64. (unverified last cycle)
- [ ] OP_GroupMemberInfo — was `0x7324`. Recipient-only group event, 168B, recipient @64; not the roster (that's OP_GroupMemberList). Purpose still unattributed.

---

## Tier 1 — Core gameplay loop (79)

### Combat (7)
- [x] OP_AutoAttack — `0x4e71` (2026-05-12)
- [x] OP_AutoAttack2 — `0x993d` (2026-05-12)
- [ ] OP_CombatAbility
- [ ] OP_Stun
- [ ] OP_Taunt
- [ ] OP_Sacrifice
- [ ] OP_ApplyPoison

### Spells (5)
- [x] OP_BeginCast — `0x271f` (2026-05-11)
- [x] OP_CastSpell — `0xf571` (2026-05-11)
- [x] OP_MemorizeSpell — `0xe42f` (2026-05-13)
- [ ] OP_SwapSpell
- [ ] OP_LoadSpellSet

### Stats / HP / mana / xp (10)
- [x] OP_ExpUpdate — `0x6961` (2026-05-01)
- [x] OP_LevelUpdate — `0x7a97` (2026-05-01)
- [x] OP_ManaUpdate — `0x37ad` (2026-05-07)
- [x] OP_SkillUpdate — `0x6f75` (2026-05-01)
- [ ] OP_LeaderExpUpdate
- [x] OP_MobHealth — `0x2171` (2026-05-01)
- [ ] OP_InitialMobHealth
- [ ] OP_MendHPUpdate
- [ ] OP_IncreaseStats
- [ ] OP_UIUpdate

### Spawn / appearance (8)
- [ ] OP_ZoneSpawns
- [ ] OP_SpawnRename
- [x] OP_WearChange — `0x800e` (2026-05-12)
- [ ] OP_Illusion
- [ ] OP_Shroud
- [x] OP_Animation — `0x3d3d` (2026-05-01)
- [ ] OP_FaceChange
- [ ] OP_Dye

### Movement / zone (18)
- [ ] OP_Jump
- [ ] OP_SetRunMode
- [x] OP_MovementHistory — `0xdc5d` (2026-05-01)
- [ ] OP_EnvDamage
- [ ] OP_Weather
- [ ] OP_SafePoint
- [ ] OP_Translocate
- [ ] OP_ClickDoor
- [ ] OP_MoveDoor
- [ ] OP_BoardBoat
- [ ] OP_LeaveBoat
- [ ] OP_ReqNewZone
- [ ] OP_RequestZoneChange
- [ ] OP_SaveOnZoneReq
- [ ] OP_ReqZoneObjects
- [ ] OP_ReqClientSpawn
- [ ] OP_SendExpZonein
- [ ] OP_ZoneUnavail

### Skill actions (16)
- [ ] OP_BindWound
- [ ] OP_Mend
- [ ] OP_Forage
- [ ] OP_Hide
- [ ] OP_Sneak
- [ ] OP_Track
- [ ] OP_Begging
- [ ] OP_DisarmTraps
- [ ] OP_SenseTraps
- [ ] OP_SenseHeading
- [ ] OP_FindPersonRequest
- [ ] OP_FindResponse
- [x] OP_RandomReq — `0x953e` (2026-05-12)
- [x] OP_RandomReply — `0x3d0f` (2026-05-12)
- [ ] OP_Consume
- [ ] OP_ReadBook

### Group / raid / pet / target (8)
- [x] OP_GroupInvite2 — `0x0d97` (2026-05-11)
- [x] OP_GroupCancelInvite — `0xbf04` (2026-05-07)
- [x] OP_GroupFollow2 — `0xe92d` (2026-05-07)
- [x] OP_GroupMemberList — `0xb7c6` (2026-05-07)
- [ ] OP_RaidInvite
- [ ] OP_RaidJoin
- [ ] OP_PetCommands
- [ ] OP_TargetCommand

### Buffs / inspect / chat (7)
- [x] OP_BuffWindow — `0xbbf0` (2026-05-13)
- [x] OP_BuffQuery — `0x306c` (2026-05-13)
- [ ] OP_BuffFadeMsg
- [x] OP_ClickBuffOff — `0x5855` C>S type=0x24 (subtype of OP_SpawnAppearance2) (2026-05-12)
- [x] OP_InspectRequest — `0xa63b` (2026-05-11)
- [x] OP_InspectAnswer — `0xce34` (2026-05-11)
- [ ] OP_ItemTextFile
- [ ] OP_Emote

---

## Tier 2 — Trade / loot / inventory / economy (41)

### Loot / corpse (8)
- [ ] OP_LootRequest
- [ ] OP_LootItem
- [ ] OP_EndLootRequest
- [ ] OP_LootComplete
- [ ] OP_MoneyOnCorpse
- [ ] OP_CorpseLocResponse
- [ ] OP_ConsiderCorpse
- [ ] OP_SummonCorpse

### Player trade (8)
- [ ] OP_TradeRequest
- [ ] OP_TradeRequestAck
- [ ] OP_TradeDeny
- [ ] OP_FinishTrade
- [ ] OP_CancelTrade
- [ ] OP_TradeAcceptClick
- [ ] OP_TradeCoins
- [ ] OP_TradeCoins2

### Inventory / items (8)
- [x] OP_MoveItem = 0xcb03 (C>S, 28B; resolved 2026-05-02)
- [x] OP_ItemPacket = 0x3f3b (S>C, variable; resolved 2026-05-02)
- [~] OP_ItemPlayerPacket — appears obsolete on current Live; per-item OP_ItemPacket fires now cover both bulk-on-zone-in and per-move delivery
- [ ] OP_ItemLinkClick
- [ ] OP_ItemLinkResponse
- [ ] OP_ClickInventory
- [ ] OP_ClickInventoryAck
- [ ] OP_OpenObject

### Money (4)
- [ ] OP_GainMoney
- [ ] OP_MoneyUpdate
- [ ] OP_MoveCash
- [ ] OP_Split

### Vendor / Bazaar / Trader (8)
- [ ] OP_ShopRequest
- [ ] OP_ShopItem
- [ ] OP_ShopPlayerBuy
- [ ] OP_ShopPlayerSell
- [ ] OP_ShopEnd
- [ ] OP_BazaarSearchRequest
- [ ] OP_BazaarSearchResponse
- [ ] OP_Trader

### Tradeskill (5)
- [ ] OP_OpenTradeskillContainer
- [ ] OP_TradeskillRecipes
- [ ] OP_TradeSkillCombine
- [ ] OP_TradeSkillCombineOld
- [ ] OP_TradeSkillResult

---

## Tier 3 — Social / guild / GM / petition (52)

### Social (11)
- [ ] OP_FriendsList
- [ ] OP_FriendsOnline
- [ ] OP_WhoAllRequest
- [ ] OP_WhoAllResponse
- [ ] OP_Surname
- [ ] OP_LFGCommand
- [ ] OP_LFPCommand
- [ ] OP_LFGGetMatchesRequest
- [ ] OP_LFGGetMatchesResponse
- [ ] OP_LFPGetMatchesRequest
- [ ] OP_LFPGetMatchesResponse

### Guild (12)
- [ ] OP_GetGuildMOTD
- [ ] OP_GuildsList
- [ ] OP_NewGuildInZone
- [ ] OP_GuildBank
- [ ] OP_GuildDemote
- [ ] OP_GuildLeader
- [ ] OP_GuildPeace
- [ ] OP_GuildWar
- [ ] OP_GuildPublicNote
- [ ] OP_GuildRemove
- [ ] OP_GuildTributeInfo
- [ ] OP_GuildTributeStatus

### GM (21)
- [ ] OP_GMApproval
- [ ] OP_GMBecomeNPC
- [ ] OP_GMDelCorpse
- [ ] OP_GMEmoteZone
- [ ] OP_GMEndTraining
- [ ] OP_GMEndTrainingResponse
- [ ] OP_GMGoto
- [ ] OP_GMHideMe
- [ ] OP_GMInquire
- [ ] OP_GMKick
- [ ] OP_GMKill
- [ ] OP_GMLastName
- [ ] OP_GMSearchCorpse
- [ ] OP_GMServers
- [ ] OP_GMSoulmark
- [ ] OP_GMSummon
- [ ] OP_GMToggle
- [ ] OP_GMTraining
- [ ] OP_GMTrainSkill
- [ ] OP_GMZoneRequest
- [ ] OP_GMZoneRequest2

### Petition (8)
- [ ] OP_Petition
- [ ] OP_DeletePetition
- [ ] OP_PetitionCheckIn
- [ ] OP_PetitionCheckout
- [ ] OP_PetitionQue
- [ ] OP_PetitionResolve
- [ ] OP_PetitionUnCheckout
- [ ] OP_ViewPetition

---

## Tier 4 — Live-era / endgame systems (36)

### Tasks (3)
- [ ] OP_TaskDescription
- [ ] OP_TaskActivity
- [ ] OP_CompletedTasks

### AAs (4)
- [x] OP_AAAction — `0x773e` (2026-05-02)
- [x] OP_RespondAA — `0xa1e1` (2026-05-03) — per-spend response carrying the player's full AA list
- [~] OP_SendAAStats — likely obsolete on current Live; not observed firing in `aa_point.vpk`
- [x] OP_SendAATable — `0xa30a` (2026-05-03) — static ability-definition menu, NOT purchased ranks

### DZ / Expedition (4)
- [ ] OP_DzInfo
- [ ] OP_DzSwitchInfo
- [ ] OP_DzMembers
- [ ] OP_Lockouts

### Adventure / LDoN (5)
- [ ] OP_Adventure
- [ ] OP_AdventureRequest
- [ ] OP_AdventureMerchantPurchase
- [ ] OP_AdventureMerchantResponse
- [ ] OP_AdventurePointsUpdate

### Mercenaries (1)
- [ ] OP_MercenaryList

### Tribute (2)
- [ ] OP_TributeInfo
- [ ] OP_TributeUpdate

### Fellowship / Campfire (3)
- [ ] OP_Fellowship
- [ ] OP_Campfire
- [ ] OP_SelectCampfire

### Marketplace / Claims / Voice / Polls / Shroud (8)
- [ ] OP_Marketplace
- [ ] OP_MarketplaceSelect
- [ ] OP_Claims
- [ ] OP_VoiceChat
- [ ] OP_PollQuestions
- [ ] OP_PollResponses
- [ ] OP_ShroudProgression
- [ ] OP_ShroudTemplates

### Housing (2)
- [ ] OP_HouseAddress
- [ ] OP_HouseContents

### Misc rewards / titles (4)
- [ ] OP_VeteranRewards
- [ ] OP_Rewards
- [ ] OP_CustomTitles
- [ ] OP_WelcomeScreenTitle

---

## Tier 5 — Protocol / system / bug-report (24)

### Session / system (11)
- [ ] OP_AckPacket
- [ ] OP_DeltaCheck
- [ ] OP_SetDataRate
- [ ] OP_SetServerFilter
- [ ] OP_ClientReady
- [ ] OP_Save
- [ ] OP_Camp
- [ ] OP_Logout
- [ ] OP_RespawnWindow
- [ ] OP_ConfirmDelete
- [ ] OP_ExpansionSetting

### Misc world / consent (6)
- [ ] OP_TGB
- [ ] OP_FTPNags
- [ ] OP_ConsentResponse
- [ ] OP_DenyResponse
- [ ] OP_BecomePK
- [ ] OP_YellForHelp

### Bug / feedback (4)
- [ ] OP_Bug
- [ ] OP_Feedback
- [ ] OP_Report
- [ ] OP_CrashDump

### Unknown (3)
- [ ] OP_Unknown1
- [ ] OP_Unknown2
- [ ] OP_Unknown3

---

## Confirmation log

### 2026-05-01 — first tier-1 sweep (monk solo, ~10 min varied activity)

Capture: `--opcode-stats` over one mob fight + movement + /random ×2 + /findperson + inspect + /emote + door click + sit/stand + jumps + one /mend.

- **OP_Animation = `0x3d3d`** (S>C, 4 bytes, n=78). Bytes match `{u16 spawn_id, u8 anim, u8 speed}` — spawn_id alternates between player and mob across the fight, anim varies, speed=10 constant. Dominates all other 4-byte S>C unknowns (next highest n=2). No daemon handler today; XML id set so future stats reports mark it known.
- **OP_MobHealth = `0x2171`** (S>C, 6 bytes, n=26 in discovery + n=5/9 across two kill verifies). Struct is `{u16 spawnId, int32 hpPercent}` — value is **HP percentage 0-100**, not raw HP. Confirmed 2026-05-01 by kill captures where the first observed value was exactly 100. Wired through `SpawnShell::updateMobHealth` → `setHP(maxHP * pct / 100)` when maxHP is known from a prior OP_HPUpdate / OP_InitialMobHealth.

  **Side finding worth carrying forward**: in the verify capture both targeted mobs (yellow skel + white mob) showed `maxHP=100` from `OP_HPUpdate`. That suggests `hpNpcUpdateStruct` is also a percent on live (its XML comment already says "HP % of a PC or NPC") — `maxHP` is normalized to 100, not raw. Means the daemon's HP gauge for spawns is fundamentally percent-based on live, and any future tier-1 work that wants real raw HP needs an out-of-band source (mob level/race lookup tables).
- **OP_MovementHistory = `0xdc5d`** (C>S, base 18 bytes batched up to 2500, n=407). Per-record 18 bytes with X/Y/Z floats; batched bursts during movement. Only loud C>S unknown; behavioral fit is unmistakable. No handler; XML-only.

Hint table refresh (`src/opcodestats.cpp`): dropped the 9 stale entries (all already-resolved opcodes), added tier-1 hints with sizes from `everquest.h`:
- OP_ExpUpdate / 16 / S>C
- OP_LevelUpdate / 16 / S>C (struct is `levelUpUpdateStruct`)
- OP_SkillUpdate / 12 / S>C
- OP_InspectRequest / 8 / S>C
- OP_InspectAnswer / 1956 / S>C
- OP_EnvDamage / 46 / S>C

### 2026-05-01 — exp + level + skill triple-confirm (monk Gloomingdeep, ~10 min, level 1→2 + 4 kills + 11 skill-ups)

The new tier-1 hint table immediately surfaced all three at the right size+direction:

- **OP_SkillUpdate = `0x6f75`** (S>C, 12 b, n=11 — exact match for the 11 skill-ups). Sample bytes decode cleanly as `skillIncStruct{u32 skillId, i32 value, u8[4] unknown}`: skillId=30 (H2H), value=12 ✓ matches "H2H to 12". Three competing 12-byte S>C unknowns existed (300/30/n hits) but counts didn't match user activity.
- **OP_ExpUpdate = `0x6961`** (S>C, 16 b, n=9 across 4 kills + level transition; ~2 fires per kill = set+update). Sample bytes decode as `expUpdateStruct{u32 exp, u32 unknown, u32 type, u32 unknown}` with **exp on a 0-100000 scale** (not legacy 0-330 or percent-100). Pre-level sample: exp=97900, type=0 then type=2; post-level: exp=814 — matches the OP_LevelUpdate cross-reference exactly.
- **OP_LevelUpdate = `0x7a97`** (S>C, 16 b, n=1 at level transition). Sample bytes: `02 00 00 00 01 00 00 00 2e 03 00 00 00 00 00 00` → `levelUpUpdateStruct{level=2, levelOld=1, exp=814, unknown=0}`. The `exp=814` cross-references the OP_ExpUpdate post-level sample byte-for-byte. This is about as definitive as it gets.

Player.cpp lost the legacy `calc_exp() / 330` machinery (per-class exp tables that don't apply to live's per-level percent format). The init / charProfile / updateLevel paths now hard-code `m_minExp=0, m_maxExp=100000, m_tickExp=1`. Bar UI uses `expCur/expMax` ratio so percent correctly displays without further changes.

Round-2 ideas:
- Isolation captures for the 16-byte S>C field (six candidates) — kill a mob with no other activity to nail OP_ExpUpdate.
- Skill-up fishing on a low skill (BindWound, Sense Heading) for OP_SkillUpdate at 12 bytes S>C.
- Track session on a ranger to chase OP_Track.
- Add struct definitions + handlers for OP_Animation / OP_MobHealth / OP_MovementHistory if we want decoded events in the daemon (separate work — current XML-only state means stats report cleanup, no behavioral decode).

### 2026-05-02 — OP_PlayerProfile wire layout (AA + skills array prefixes)

OP_PlayerProfile (`0xdb56`) was already resolved; this is a layout refinement. The struct in `everquest.h` documents fixed offsets, but the live wire is variable-length with explicit u32 length prefixes before each array. Confirmed against two captures (`combat.vpk` lvl 1 monk @ 23875 bytes; a fresh login capture for a lvl 60 @ 37088 bytes — both via `--dump-payload 0xdb56:path`):

- **wire 1014**: `u32 = 300` (MAX_AA prefix)
- **wire 1018..4617**: `AA_Array[300]` (12 bytes each: `aa_id`, `value`, `unk`)
- **wire 4618**: `u32 = 100` (MAX_KNOWN_SKILLS prefix)
- **wire 4622..5021**: `uint32[100]` skills

Only the skills overlay in `zonemgr.cpp::zonePlayer` is necessary — `fillProfileStruct`'s netstream parser already populates `aa_array` correctly (via length-prefix reads); it just explicitly skips the skills values.

**aa_array on this opcode carries the FULL AA list, including purchased ranks** — early indices populated with auto-grants at `value=0` (e.g. `1371-1377` vet rewards, `4665`, `4700`, `9000`, plus `1000`, `15073`, `8000` at lvl 60), then trailing slots with the player's purchased AAs at `value=rank_count`. Confirmed 2026-05-03 against `aa_point.vpk`: index 13 = `(163, 5)` Mend Pet × 5, index 14 = `(64, 3)` Innate Run Speed × 3 in fire 4 (post-spend). The earlier "auto-grants only" claim came from a capture taken before the user had purchased anything; once `aa_spent > 0`, the trailing slots populate. _(Original note left below for context, struck through.)_

> ~~aa_array on this opcode is auto-granted only. Every entry has value=0. Auto-granted ids observed across captures: 1371-1377 (sequential vet rewards), 4665, 4700, 9000 on the lvl 1 capture; the lvl 60 capture adds 1000, 15073, 8000. Purchased AA ranks (Origin, Veteran's Enhancement, etc.) — even at 1/1 — do not appear here. They almost certainly come over OP_SendAATable, still unresolved.~~

Round-2 ideas (specific to AA work):
- Hunt OP_SendAATable: capture during a fresh login and `--opcode-stats` for repeated S>C unknowns sized like a per-AA record (~16-32 bytes per entry × N).
- Once AA ranks are wired, also nail down `aa_spent` / `aa_unspent` / `aa_assigned` / `expAA` wire offsets in OP_PlayerProfile (post-skills, in the variable-tail region — needs a non-zero capture for disambiguation).

### 2026-05-02 — OP_PlayerProfile expAA wire offset

Capture: `aa_progress.vpk` recorded during a 4-zone session that progressed AA from 22.846% to 40.318%, with `--dump-payload 0xdb56:/tmp/pp_aa` extracting all four OP_PlayerProfile fires (each 37108 bytes, identical to the lvl 60 baseline length).

- **expAA at wire offset 20411** (u32 LE). Values across the four dumps: `22846 / 22846 / 22846 / 40318` — exact match for the in-game progress (display % = `expAA / 1000.0`). Zero-out competing candidates: a Python sweep of every offset ≥5022 for a u32 in [10000, 100000] that is monotonically non-decreasing across the four dumps AND brackets the start/end values returned a single hit.

  The legacy parser had `skipBytes(70)` in `fillProfileStruct` between `endurance` and the firstName length-prefix; expAA sits 58 bytes into that gap. Fix is splitting it into `skip(58) + read expAA + skip(8)`. Mirrored to legacy `showeq/src/zonemgr.cpp`.

  Confirmed scale by replaying the vpk through the patched daemon — all four `player->expAA` reads matched the wire-offset Python analysis byte-for-byte. Tier-2 `.pbstream` goldens regenerated for the `aa_exp_max` 15M→100K change (encoder `protoencoder.cpp`; legacy `player.cpp` emit args also 15M→100K so StatList's integer-truncated % calc stops collapsing to 0).

- **aa_spent / aa_unspent / aa_assigned**: still unresolved on the wire. All four dumps had every nearby u32 either 0 or constant — the user hadn't earned a full AA point during the capture, so there's no signal to disambiguate the field positions. Round-3: re-capture after spending one AA point (`aa_spent` 0→1 plus likely OP_AAAction round-trip + OP_SendAATable refresh).

### 2026-05-02 — OP_PlayerProfile aa_spent / aa_assigned / aa_unspent wire offsets

Capture: `aa_point.vpk` recorded across 4 zone-ins spanning an AA level-up event + spending. Final state: 8 spent (5 Mend Pet + 3 Run Speed), 1 unspent. expAA progression confirms the 88533 → 27 reset pattern that follows a level-up (same wrap behavior as normal exp).

- **aa_spent at wire offset 19487, aa_assigned at 19495, aa_unspent at 19519** (all u32 LE). Progression across the four fires:

  | fire | aa_spent | aa_assigned | aa_unspent | expAA  | story |
  |------|----------|-------------|------------|--------|-------|
  | 1    | 0        | 0           | 8          | 88533  | pre-level-up |
  | 2    | 0        | 0           | 8          | 88533  | same |
  | 3    | 0        | 0           | 9          | 27     | crossed expAA threshold → +1 point |
  | 4    | 8        | 8           | 1          | 27     | spent 5 + 3 = 8 → 1 left |

  The existing parser already lands at the right offsets (verified by replaying with a temporary `qInfo` log of `netStream.pos() - netStream.data()`); prior captures showed all-zero spent/assigned because the user genuinely had `aa_spent=0` then. No code change.

- **aa_spent vs aa_assigned**: identical values (8/8) without a glyph applied — the parser order matches `everquest.h`'s `aa_spent → unknown(4) → aa_assigned → unknown(16, declared as 4×u32) → aa_unspent`. Wire stride between aa_assigned and aa_unspent is 20 bytes, +4 over the struct's 16. Disambiguation requires a glyph capture (`aa_spent` includes glyphs, `aa_assigned` doesn't).

- **`aa_points` proto field semantics**: ~~daemon currently emits `aa_points = aa_spent` (`player.cpp:349`). For a UI showing "AA: N pool to spend", `aa_unspent` is the more useful number — but that's a proto schema change, deferred.~~ Resolved 2026-05-02: added `aa_unspent` field to `seq.v1.PlayerStats`; daemon emits both. UI shows `AA · N spent · M ready`.

### 2026-05-02 — OP_AAExpUpdate (0xbf67) wire fields

Captures: `aa_progress.vpk`, `aa_point.vpk`, `aa_slider50.vpk`. Each 12-byte payload decodes as:

| field | type | semantic |
|-------|------|----------|
| `altexp` | u32 LE | 0..100000 sub-percent progress to next AA point (matches charProfile `expAA` scale; in-game % = `altexp/1000`). Direct; no scaling. |
| `aapoints` | u32 LE | **Unspent** count, NOT spent — decrements as the player allocates points. Legacy code routed this into `m_currentAApts` (the "spent" tracker), corrupting it on every tick. Now goes to `m_currentAAUnspent`. |
| `percent` | u8 | The player's "% of exp to AA" slider. Constant during kills; fires its own OP_AAExpUpdate when the slider moves (`aa_slider50.vpk` shows the walk 100→90→80→70→60→50 across consecutive fires). |

Implication: pre-fix the AA bar was driven by `altexp * (15000000/330)` (legacy x/330 scaling) but the encoder advertised `aa_exp_max=100000`, so post-tick the bar pegged to 100% or showed wildly wrong values. Direct use of `altexp` resolves it.

Round-3 ideas:
- Apply a glyph and re-capture to disambiguate `aa_spent` vs `aa_assigned`.
- Hunt OP_SendAATable now that there are non-zero purchased ranks (Mend Pet × 5, Run Speed × 3) — the table should include records with `value=5` and `value=3` matching those ranks, easy to spot by `--opcode-stats` + `--dump-payload`.

### 2026-05-02 — OP_AAAction = 0x773e

Capture: same `aa_point.vpk`. Method: time-correlation via the new daemon `--list-events` flag (`<unix_ms> <C|S> 0xXXXX <bytes> <stream> <name>` per packet). The naive count-based hunt against `--opcode-stats` failed because the `aaActionStruct` is 16 bytes but no global 4-count C>S unknown matched at that size; the win came from filtering events to the **spend window** (between OP_PlayerProfile fire #3 with `aa_unspent=9, aa_spent=0` and fire #4 with `aa_unspent=1, aa_spent=8`).

In that window exactly one unknown C>S opcode fired exactly four times at 16 bytes:

| fire | u32_le payload | meaning |
|------|----------------|---------|
| 1 | `[3, 58, 0, 0]` | action=3 (buy), ability=58 — Mend Pet, single buy of 5 ranks |
| 2 | `[3, 13, 0, 0]` | ability=13 — Innate Run Speed rank 1 |
| 3 | `[3, 13, 0, 0]` | rank 2 |
| 4 | `[3, 13, 0, 0]` | rank 3 |

Decode matches EQMacEmu's `AA_Action { action, ability, unknown08, exp_value }` 16-byte struct. Resolution landed in both `showeq-daemon/conf/zoneopcodes.xml` and legacy `showeq/conf/zoneopcodes.xml`.

The same opcode also fires 400× S>C at 12 bytes across the full capture — a companion server-side message (legacy XML comment hinted at multi-purpose: "changing percent, buying, activating skills"). Not investigated; the C>S resolution alone covers the user-visible "buy AA" event.

Generalizable lesson: when count-based correlation against `--opcode-stats` totals returns nothing, narrow the window. `--list-events` + an in-game-event-bracketing pair of known opcodes (here, two OP_PlayerProfile fires) often resolves a rare opcode that was lost in the noise of the global tally.

### 2026-05-02 — OP_ItemPacket = 0x3f3b, OP_MoveItem = 0xcb03 (full inventory pipeline)

OP_MoveItem (`0xcb03`, C>S 28B) and OP_ItemPacket (`0x3f3b`, S>C variable ~960B) resolved via `inventory.opcodestats.txt` from a slot-swap session — Whitened Treant Fists ↔ Adamantite Club moves produced exactly the expected counts at unique size+direction combos.

OP_ItemPacket's `serializedItem[0]` blob decoded into `parsedItemTemplateStruct` (everquest.h). Layout pinned by aligning post-name offsets across five reference items (Crescent Gi, Platinum Fire Wedding Ring, Outstanding Necklace/Mask/Band of Distant Echoes) with known stats:

| Offset | Type | Field |
|--------|------|-------|
| +0  | u32 | format constant (always 63) |
| +8  | u32 | itemId |
| +12 | u32 | weight × 10 |
| +20 | u32 | slot bitmask (0x6000 = Pri\|Sec, 0x18000 = Ring1\|Ring2, 0x20 = Neck, 0x20000 = Chest, etc.) |
| +34..+38 | i8 × 5 | resists [CR, DR, PR, MR, FR] |
| +39 | i8 | corruption resist |
| +40..+46 | i8 × 7 | stats [STR, STA, AGI, DEX, CHA, INT, WIS] |
| +47..+50 | i32 LE | HP modifier |
| +51..+54 | i32 LE | mana modifier |
| +55..+58 | i32 LE | endurance modifier |
| +59..+62 | i32 LE | AC modifier |

Wire gives BASE values; equipped augments contribute via separate OP_ItemPacket fires for the aug item. Total stats = base + each filled aug.

Notable: legacy bulk OP_ItemPlayerPacket appears obsolete on current Live. Modern Live sends per-item OP_ItemPacket fires for both zone-in pickup and slot moves.

Daemon side: parser in `src/itempacket.{h,cpp}`, persistent itemId → ItemTemplate cache at `~/.showeq/daemon/itemcache.json` (`src/itemcache.{h,cpp}`), emitted to clients as ItemLearned + ItemCacheTotals envelopes (proto messages 33/34, plus Snapshot fields 8/9). Web client renders an InventoryStatsPanel.

### 2026-05-02 — OP_PlayerProfile worn-slot decode: stalled (research notes)

**Goal:** find the offset(s) in OP_PlayerProfile where worn-slot itemIds live, so the daemon can sum HP/mana/AC across the *currently equipped* set instead of every observed item in the cache.

**Approach tried:** differential capture. Three sequences across `inventory-worn.vpk`, `inventory-worn2.vpk`, `inventory-naked.vpk`:

1. Single-item un-equip + zone (Mask off, Mask on, Necklace off, Necklace on).
2. Multi-item un-equip with manual drag (Mask + Necklace + Band off + on).
3. Full strip (every worn slot) + zone, repeated for cross-validation.

**Findings:**

- Itemid bytes for known worn items appear at three FIXED offsets each (Mask at 36131/36471/36811, Necklace at 36171/36511/36851) and **do not change when the item is un-equipped**. So those offsets are not the worn-slot indicator. They appear to be persistent inventory listings or some account-wide record (a Wedding Ring from a different character on the same account also appears at one of these offsets, supporting "account-wide" interpretation).
- 320–416 bytes consistently change when gear is removed, scattered across PP offsets 20591–36025 in 1–3 byte runs. No clean contiguous worn-slot block.
- Densest cluster: offsets **21300–21399** (45 changing bytes). With gear on, this region holds float-shaped values; with gear off, it's all zeros. Likely a calculated-stats / aug-summary table rather than itemId mapping.
- Money block confirmed at offsets **19435 (and duplicated at 20625)** — uint32 LE platinum/gold/silver/copper; usable as a stable landmark for future PP work.
- Buff timers tick continuously and produce ~10–60 byte noise floor between supposedly-identical baselines, contaminating naive diffs. Strip all buffs before any future differential capture.

**Hypothesis:** modern Live's OP_PlayerProfile does not carry a flat "slot → itemId" table. Equipment data appears split across multiple sub-blocks (calculated stat totals, weight/encumbrance contributions, possibly cached aug-array data) that the client uses to render computed totals. Worn-slot itemId mapping may live in a dedicated opcode that fires on each equip/un-equip rather than in PP.

**Next investigation paths (in priority order):**

1. **Hunt for a dedicated worn-update opcode.** Look for unknown C>S+S>C pairs that fire alongside drag-equip/un-equip but NOT alongside other inventory moves. Use `--list-events` + an in-game-event window bracketed by zoning events. Promising candidates from the inventory-worn captures' opcode-stats: any C>S that fires exactly N times during N slot transitions.
2. **Isolate one stat at a time.** Equip ONLY a +HP item, capture; then ONLY a +mana item, capture; then ONLY a +AC item. Each diff against naked should reveal the byte offset for ONE calculated-stat field. Multi-day work but accurate.
3. **Cross-reference EQ client serialization.** The 21300-21399 region looks like the wire format used for serialized-item-via-stack on the client side. May be possible to reverse-engineer from public-source EQ-emulator references with care for licensing.

For now, the v1 ItemCache + ItemCacheTotals approach (sum across ALL observed items, not worn-only) is shipped and useful as a "potential gear stats" total. Worn-only is a long-tail follow-up.

### 2026-05-03 — OP_ItemPacket wrapper carries the worn-slot (PP detour resolved)

The 2026-05-02 hypothesis was right (PP doesn't carry a flat slot→itemId table) but the suggested next step (hunt a dedicated worn-update opcode) turned out to be unnecessary: **OP_ItemPacket already broadcasts each slot transition.** The daemon's parser was just discarding the wrapper bytes that name the destination slot.

**Method.** Replayed `inventory-worn2.vpk` and `inventory-naked.vpk` with `--dump-payload 0x3f3b:…` and inspected the wrapper bytes preceding the (already-decoded) item-name region. No new captures needed.

**Wrapper layout** (validated across 90+ fires):

| Offset | Type | Field |
|--------|------|-------|
| +0  | u32 | `packetType` — 0x74=item-in-bag, 0x76=bag-itself, 0x78=move-response |
| +4  | char[16] | ASCII instance-id (e.g. `"vIS00B80001H3G00"`) |
| +20 | u8  | NUL terminator |
| +21 | u32 | stack/charges count |
| +25 | u32 | `main_slot` — 0=top-level (worn/inv/cursor), nonzero=parent bag slot |
| +29 | u16 | `sub_slot` — when main=0, this IS the worn/inv slot index |

Slot enum (matches the legacy `slot_bitmask` bit indices on `parsedItemTemplateStruct`):
`0=Charm, 1=Ear_L, 2=Head, 3=Face, 4=Ear_R, 5=Neck, 6=Shoulder, 7=Arms, 8=Back, 9=Wrist_L, 10=Wrist_R, 11=Range, 12=Hands, 13=Primary, 14=Secondary, 15=Finger_L, 16=Finger_R, 17=Chest, 18=Legs, 19=Feet, 20=Waist, 21=PowerSrc, 22=Ammo, 23-30=PersonalInv, 35(0x23)=Cursor`.

**Cross-validation in `inventory-naked.vpk`** (full strip → re-equip × 2). A monk with 21 slots filled produced exactly 21 type-0x78 fires per re-equip, slot indices and items aligning perfectly:

| Slot | Item |
|------|------|
| 0  Charm    | Outstanding Charm of Distant Echoes |
| 1  Ear_L    | Outstanding Summoner Stud of Distant Echoes |
| 2  Head     | Apothic Crown |
| 3  Face     | Outstanding Mask of Distant Echoes |
| 4  Ear_R    | Outstanding Stud of Distant Echoes |
| 5  Neck     | Outstanding Necklace of Distant Echoes |
| 6  Shoulder | Outstanding Shawl of Distant Echoes |
| 7  Arms     | Apothic Sleeves |
| 8  Back     | Outstanding Cloak of Distant Echoes |
| 9  Wrist_L  | Bracelet of Exertion |
| 10 Wrist_R  | Supple Scale Armband |
| 11 Range    | Iksar Hide Manual |
| 12 Hands    | Dusty Bloodstained Gloves |
| 13 Primary  | Orb of Mastery |
| 14 Secondary| Book of Obulus |
| 15 Finger_L | Outstanding Band of Distant Echoes |
| 16 Finger_R | Excellent Band of Distant Echoes |
| 17 Chest    | Robe of the Azure Sky |
| 18 Legs     | Apothic Kilt |
| 19 Feet     | Apothic Boots |
| 20 Waist    | Damask Sash |
| 22 Ammo     | Metamorph Wand: Undead Gingerbread Man |

Strip phase showed all 21 items moving to slot 35 (Cursor). The pattern repeats once more at the second strip→redress cycle, so the result reproduces.

**Ruled-out leads** (worth recording so we don't re-chase them):
- `0x6c39` (14B S>C): count tracked transitions (5/6/etc.) but payload is byte-identical (`0a000000…0`) across every fire — it's a periodic end-of-burst sentinel after bag-snapshot bursts of OP_ItemPackets, not a wear-change.
- `OP_WearChange` as a dedicated player-self update: not visible in these captures. May still exist as a *visual broadcast* for **other** players' gear changes (legacy comment: "or give a pet a weapon (model changes)"), but for the player's own worn-slot bookkeeping the data is in OP_ItemPacket.
- `0x549c / 0x9d44 / 0x37ad / 0x800e / 0xbbf0`: counts loosely correlate with various inventory-burst-tier events but none are worn-slot identifiers.

**Daemon parser change** (sketch — see `src/itempacket.{h,cpp}`):

1. Add to `ItemTemplate`:
   ```cpp
   uint32_t packetType = 0;   // 0x74 / 0x76 / 0x78
   uint32_t mainSlot   = 0;   // 0 = top-level
   uint16_t subSlot    = 0;   // worn/inv slot index when mainSlot==0
   ```
2. In `parseItemPacket`, before the existing `findNameStart` scan, read the fixed-offset wrapper fields:
   ```cpp
   if (len < 31) return false;
   out->packetType = readU32LE(data + 0);
   // bytes [4..20] = 16-byte ASCII instance-id + NUL @ +20
   if (data[20] != 0) return false;                    // sanity check
   // bytes [21..24] = stack/charges (currently unused)
   out->mainSlot = readU32LE(data + 25);
   out->subSlot  = uint16_t(data[29]) | (uint16_t(data[30]) << 8);
   ```
3. In `ItemCache::onItemPacket`, when `mainSlot==0 && subSlot<=22`, also push a `(slot, itemId)` update into a new `WornSlotMap` keyed by slot index. Move-to-cursor (`subSlot==0x23`) clears the slot. The map is the authoritative "currently equipped" set.
4. Rework `ItemCacheTotals` to sum over `WornSlotMap` instead of all cached items. Keep the legacy "every observed item" sum behind a flag if it's still useful as a "potential" view.
5. Proto: add a `WornSet` message (slot → itemId, plus per-slot summed stats), wired through `protoencoder.cpp`. Pull the legacy OP_ItemPlayerPacket todo entry — that opcode is obsolete on Live.

**No new opcode resolution.** OP_ItemPacket was already known/resolved on 2026-05-02; this is a wrapper-decoding refinement on top of it. No XML changes needed in `zoneopcodes.xml`.

### 2026-05-03 — OP_SendAATable = 0xa30a (static AA-definition menu, NOT purchased ranks)

Capture: existing `aa_point.vpk` (the same one that nailed OP_AAAction). Method: `--opcode-stats` triage of fire-count=4 S>C unknowns followed by `--dump-payload 0xa30a:/tmp/aa_table/op_a30a` over a daemon replay.

- **OP_SendAATable = `0xa30a`** (S>C, 15640 B, n=4 — one fire per zone-in, matching OP_PlayerProfile's cadence). All four fires are **bit-identical**, which is what proves it's a static reference table rather than per-character state.

  Layout: `u32 count` (= 314 in this capture) followed by 314 records of a 44-byte base + variable-length prereq trailer.

  Base record (11 u32 LE, internally consistent for the first 121 records walked at fixed 44 B stride):

  | Offset | Type | Field |
  |--------|------|-------|
  | +0  | u32 | `ability_id` (sequential per series; rank-1 of a series uses the series base id) |
  | +4  | u32 | `title_sid` (string id; constant within a series — e.g. 101 for Innate Strength) |
  | +8  | u32 | `hotkey_sid` (mostly equals `ability_id`) |
  | +12 | u32 | `type` (1/4/8/… — likely tab/category code) |
  | +16 | u32 | `cost` (recurring 100/101/102/103/104; varies by tier) |
  | +20 | u32 | `rank` (1..N within the series) |
  | +24 | u32 | `?` (often 1 or 2 — possibly `class_mask` or `tier_index`) |
  | +28 | u32 | `level_req` (e.g. 20→100 in 5-step ranks 1..17, then 101+ for tier-2 ranks) |
  | +32 | u32 | `spell_id` (1016, 1024, 1025, 1026, … — the spell triggered when the rank is purchased) |
  | +36 | u32 | `?` |
  | +40 | u32 | `prereq_count` (0 for many ranks; 1+ for ranks ≥ 2 of multi-rank series) |

  Sample of the first three records (sid=101 = Innate Strength tier-1):

  ```
  rec[0] @    4: id=101  sid=101  hk=101  type=1  cost=100  rank=1   _=1  lvl=20  spell=1016  _=0  prereq_count=0
  rec[1] @   48: id=102  sid=101  hk=102  type=1  cost=100  rank=2   _=1  lvl=25  spell=1016  _=0  prereq_count=0
  rec[2] @   92: id=103  sid=101  hk=103  type=1  cost=100  rank=3   _=1  lvl=30  spell=1016  _=0  prereq_count=0
  ```

  Series transitions cleanly: at rec[17] (id=118, still sid=101) the cost/spell/level_req shift (cost=101, spell=1024, lvl=101) — Innate Strength tier-2. Distinct sids in the table: 101, 201, 301, 401, … (the canonical Innate Str/Sta/Agi/Dex/Cha/Wis/Int chains) plus dozens of class/archetype-specific sids.

  Past rec[121] the records have a per-record **prereq prefix** that prepends an `(skill_id u32, min_points u32)` pair to the base. The original "the trailer is broken" intuition was wrong — the trailing 8 bytes after a base record actually belong to the *next* record as its prefix.

  **Full record schema** (validated against all 314 records in `aa_point.vpk`'s OP_SendAATable):

  ```
  Record:
    + (optional) 8-byte prereq prefix:
        u32 prereq_skill_id
        u32 prereq_min_points
    + 44-byte base:
        u32 ability_id      // unique per rank
        u32 title_sid       // string id; constant within a series
        u32 hotkey_sid      // typically equals ability_id
        u32 type            // tab/category code (1, 2, 3, 4, 8, 10, 12, 14, …)
        u32 cost            // typically 100-104
        u32 rank            // 1..N within the series
        u32 ?               // 1 or 2 (possibly tier_index)
        u32 level_req       // min character level (20, 25, 30, …, 125)
        u32 spell_id        // spell triggered (1016, 1024, 1025, …)
        u32 ?               // typically 0
        u32 has_prereq_flag // 0 or 1; predicts whether the *next* same-series record has a prefix (with edge cases below)
  ```

  Counts in this capture: **122 records with no prefix, 192 records with an 8-byte prefix, 0 with 16-byte (the 16B observation from the rough first walk turned out to be misalignment)**. Edge cases the strict "f10 → has_prefix" rule misses, both at series boundaries:

  - **rec[120]** (id=1000, rank-1 of series 1000): `f10=1` but **no prefix** — first rank of a new prereq-bearing series, nothing to point to yet.
  - **rec[312]** (id=5000, rank-1 of series 5000): `f10=0` but **has prefix** `(skill=103, min=26)` — first rank of an epic series carries an entry-condition prereq even though no later ranks reference it.

  After all 314 records the file ends with a **284-byte trailing summary block** (71 u32s) attached to rec[313] (id=5001 with constant-5001 fields):

  ```
  (9, 1016, 1, 1, 2, 5523, 5524, 1024, 1, 1, 2,
   5535, 5536, 1025, 1, 1, 2, 5537, 5538, 1026, 1, 1,
   2, 5539, 5540, 1028, 1, 1, 2, 5543, 5544, 1030, 1,
   1, 2, 5547, 5548, 1032, 1, 1, 2, 5561, 5562, 5000,
   2, 1, 1, 8311, 2, 1, 8313, 5001, 6, 1, 1,
   8315, 2, 1, 8316, 3, 1, 8317, 4, 1, 8318, 5,
   1, 8319, 6, 1, 8320)
  ```

  The pattern looks like cross-reference rows mapping pairs of late-game ability ids (5523-5548, 5561-5562) to their series ids (1024, 1025, 1026, 1028, 1030, 1032, 5000), then an additional run for ids 8311 and 8313-8320 (likely glyph or veteran-mastery slots). The exact field semantics within each row are a future-work item — the daemon doesn't need them to display the AA window, but a future "show me what AAs exist" panel would.

- **Important correction to the 2026-05-02 round-3 note**: that note speculated `OP_SendAATable` "likely carries purchased AA ranks." The bit-identical 4-fire payload disproves that — `OP_SendAATable` is the static client-side AA-window menu data, not state. Purchased AA ranks remain unresolved on the wire (OP_PlayerProfile gives `aa_spent`/`aa_unspent`/`aa_assigned` totals, OP_AAExpUpdate gives the unspent-pool counter, but the per-ability rank breakdown still has no known opcode).

  Auto-grant evidence: the auto-granted AA ids observed in OP_PlayerProfile's `aa_array` (1371-1377, 4665, 4700, 8000, 9000, 15073) **do not appear** in the `0xa30a` payload — only id `1000` is present. That confirms `0xa30a` is the *purchasable*-AA definitions table, while auto-grants are reported separately via `aa_array`.

**Ruled-out leads** (so we don't re-chase them):

- `0xa1e1` (3628 B, S>C, n=4): looked promising because its payload contains the exact set of auto-grant ids from OP_PlayerProfile (1000, 15073, 1371-1377, 4665, 4700, 8000, 9000). But fire-by-fire diff shows the payload **changes every fire** with a monotonic counter at offsets 0/4/8 (`5,5,0` → `6,6,1` → `7,7,2` → `8,8,3` across the four zone-ins), and only one additional 8-byte field changes between fire 1 and fire 4 (`(0,0)` → `(64,3)` at offset 196..203). Delta isn't structured like AA-rank state (Mend Pet × 5 + Run Speed × 3 = two records, but only one offset moved). Likely a per-zone-in sequence-number or zone-progress packet that happens to carry the auto-grant id list as static reference data; not the AA table.
- `0xb92d` (6249 B, S>C, n=4): zlib-deflated (`78 da` signature) — almost certainly map / static-content blob.
- `0xe49f` (7023 B, S>C, n=4): plaintext char-create dropdowns ("Yes/No/I Don't Care", race names) — character-creation reference data.
- `0xac8b` (494 B, S>C, n=4): item-name reference list (`Broken Key of Sands`, `Pocket Full of Keys`, …). Not AA.
- `0xa12c` (48 B, S>C, n=4): `(5,1,2,3,4,5,5,1,2,3,4,5)` — looks like a 5×2 ordering table (party slots? raid roles?). Too small for OP_SendAATable.
- `0x2e6e` (1603 B, S>C, n=4): structured but starts `(8,0x37,10,1,10,…)` — looks like spawn / NPC reference rather than ability defs.

**Round-N ideas**:
- The 284-byte trailing summary block in OP_SendAATable maps glyph/mastery ability ids onto their parent series. Field semantics within each row are unconfirmed — future work if the daemon ever needs to render the late-game AA tabs.

### 2026-05-03 — OP_RespondAA = 0xa1e1 (per-spend AA snapshot)

Capture: same `aa_point.vpk`. Method: timestamp correlation via `--list-events`.

The 2026-05-03 OP_SendAATable entry called out `0xa1e1` as a suspicious 4-fire S>C opcode that carried the auto-grant id list with a per-fire counter. The hypothesis was "per-zone-in zone snapshot." Wrong: it fires **per OP_AAAction spend event**, not per zone-in. Timestamp evidence:

| event | times relative to PP fire 1 (ms) |
|-------|----------------------------------|
| OP_PlayerProfile (S>C 37108B) — zone-ins | 0, 98, 252, 353 |
| OP_AAAction (C>S 16B) — spend events | 303, 305, 305, 306 |
| `0xa1e1` (S>C 3628B) — 4 fires | 303, 305, 306, 306 |

The four `0xa1e1` fires line up with the four `OP_AAAction` C>S spend events within ~1 ms — server's "spend confirmation" reply.

**Payload layout** (validated against fires 1-4 of `aa_point.vpk`):

| Offset | Type | Field |
|--------|------|-------|
| +0  | u32 | `total_purchased_ranks` (5, 6, 7, 8 across the four spends) |
| +4  | u32 | duplicate of `total_purchased_ranks` |
| +8  | u32 | `last_action_progress` (0, 1, 2, 3 — Run Speed rank as it tickles up) |
| +12 | u32 | constant 5 in this capture |
| +16 | u32[3] | zeros |
| +28 onward | record[] | 12 B each: `(ability_id u32, rank u32, _ u32)` |

Records observed in fire 4 (post-spend snapshot):

```
@ 28: (1000,    0, 0)   auto-grant
@ 40: (15073,   0, 0)   auto-grant
@ 52: (1371,    0, 0)   vet-reward auto-grant
... (1372-1377 same shape)
@136: (4665,    0, 0)   auto-grant
@148: (4700,    0, 0)
@160: (8000,    0, 0)
@172: (9000,    0, 0)
@184: (163,     5, 0)   Mend Pet rank-5 (purchased!)  ← was already (163,5) in fire 1; bulk buy preceded fire 1
@196: (64,      3, 0)   Innate Run Speed rank-3 (purchased!) ← grew (62,1)→(63,2)→(64,3) across fires 2-4
@208 onward: zeros (empty slots up to ~3628 B)
```

Auto-grant entries match the OP_PlayerProfile `aa_array` set exactly. Purchased entries carry the rank count in the `rank` field (vs. `rank=0` for auto-grants). The `ability_id` field is the AA_Ability id (per-rank id from OP_SendAATable's record[0]) of the *highest* rank purchased, not the AA_Skill id used by OP_AAAction (which had ability=58 for Mend Pet, ability=13 for Run Speed).

Cross-validation: at fire 1 (after Mend Pet × 5 bulk buy, before any Run Speed), the (163, 5) Mend Pet entry is already present and the Run Speed slot is `(0, 0)`. Fire 2-4 walk the Run Speed slot from `(62, 1)` → `(63, 2)` → `(64, 3)` as each individual rank is bought.

**Implication for the daemon**: OP_RespondAA is a per-action delta. The full per-ability snapshot also lives in **OP_PlayerProfile's `aa_array`** (see correction in the 2026-05-02 PP entry above) — fires on every zone-in and contains both auto-grants and purchased ranks. So a daemon connecting mid-session WILL see the player's complete AA breakdown via PP, no need to wait for a spend. OP_RespondAA is the convenient real-time delta; OP_PlayerProfile is the authoritative snapshot.

**Negative result on OP_SendAAStats**: methodically checked all n=4 S>C zone-stream opcodes in `aa_point.vpk` for fire-3-vs-fire-4 (pre-spend vs post-spend zone-in) payload diffs. PP's `aa_array` already carries this state on every zone-in, so OP_SendAAStats may genuinely be obsolete — the per-ability rank breakdown doesn't need a separate opcode.

- 0x7932 (804/100 alternating) — zone-area-specific data, not AA state
- 0xb634 (1/5 alternating, 1 byte) — too small
- 0xe44a (24 B all fires) — values like (56320, 900000, 180000, 6, 0, 0) and (3087136512, 900000, …) don't fit AA shape, look like a session/timer field
- All other n=4 zone-stream S>C opcodes are bit-identical across fires (static reference data)

So **OP_SendAAStats does not fire on zone-in in current Live**, at least not as a separate opcode. The 2009 reference may be obsolete; the per-spend OP_RespondAA mechanism appears to subsume it. Marking OP_SendAAStats with that note in the tier list rather than `[x]`.

**Round-N ideas**:
- Capture a fresh login (just-zoned-in, mid-session reconnect) where the player already has spent AAs, to verify nothing else carries the per-ability breakdown. If confirmed, the daemon's "AA tab" view is fundamentally limited to in-session spends unless we also persist the OP_RespondAA snapshot to disk like the item cache.
- Decode the leading 4 u32s of `0xa1e1` more carefully — `last_action_progress` might actually be "rank within most-recent series" and the constant `5` at +12 might be a per-character constant worth understanding.

### 2026-05-07 — sweep over existing captures: OP_ManaUpdate + OP_GroupFollow2

Replayed `combat.vpk`, `buffs.vpk`, `group-form.vpk`, `group-disband.vpk`, plus the existing `inventory-mana.opcodestats.txt`, into `--opcode-stats` + `--list-events`. Two solid finds, several medium-confidence leads.

- **OP_ManaUpdate = `0x37ad`** (S>C, 10 B, n=28 in `inventory-mana.vpk`; also dominant 10-byte S>C unknown in `combat.vpk` at n=43). Decode as `{u16 spawn_id, u32 cur, u32 max}` — exact wire-shape parallel to OP_EndUpdate. Sample bytes from `inventory-mana.vpk` show classic regen monotonicity for a single spawn:
  - fire #5 onward: spawn=0x20d6, cur=5243→5253→5263→5273→5283→5293→5303→5313→5323→5333, max=5379 throughout — +10/tick mana regen.
  - Reference OP_EndUpdate fires from the same capture: spawn=0x20d6, cur=3516→3544→3572→3600→…→3766, max=3766 — same player, parallel endurance ticks at +28/tick.

  Wire layout adopted as `manaUpdateStruct` in everquest.h, registered in s_everquest.h, mirrored to legacy showeq. Distinct from already-resolved `OP_ManaChange` (0x130c, 20 B, fires post-cast with manaDecrementStruct).

- **OP_GroupFollow2 = `0xe92d`** (S>C, 168 B, n=1 in `group-form.vpk`). Disambiguated from two same-size siblings by *content fingerprint*. In `group-form.vpk`, three S>C 168-byte unknowns fire in the same millisecond burst as `C>S OP_GroupInvite (0x7380)` and `S>C OP_GroupFollow (0x1bcd)`:

  | opcode | name1 (offset 0..63) | name2 (offset 64..127) | trailer fingerprint |
  |--------|----------------------|------------------------|---------------------|
  | 0xe92d | empty                | **member** (the joiner) | sparse, single u32 at +132 |
  | 0x59ab | empty                | leader (self)           | sparse              |
  | 0xb7c6 | empty                | leader (self)           | rich (multiple non-zero u32s) |

  0xe92d is the only one that names the new member, matching the documented purpose of OP_GroupFollow2 ("Player joins your group"). Negative control: `grep` over every events.txt I have shows 0xe92d, 0x59ab, 0xb7c6 *only* fire in `group-form.vpk` — purely group-formation-bound.

  Wired with `groupFollowStruct` typename + `sizechecktype="none"` (wire is the modern 168-byte expansion of the 152-byte legacy struct, same scheme as OP_GroupDisband / OP_GroupLeader).

**Unresolved 168-byte group-family fires (4 ops, need follow-up captures):**

| op | dir | capture | name pattern | hypothesis |
|----|-----|---------|--------------|------------|
| 0x59ab | S>C | group-form | name=leader, sparse trailer | OP_GroupInvite2 or OP_GroupMemberList |
| 0xb7c6 | S>C | group-form | name=leader, rich trailer | OP_GroupMemberList more likely (rich state) |
| 0xbf04 | C>S | group-disband | self+self | client-side leave-group request (unmapped — no entry on the unresolved list matches semantically) |
| 0x1325 | S>C | group-disband | name=self at offset 64 | possible OP_GroupDisband2 — but currently-mapped 0x6c57 didn't fire here, so verdict pending |

Disambiguation requires:
- A 3+ member group capture (separates Invite2 / MemberList by per-member record count in the rich trailer).
- A capture from the *invitee's* perspective (different burst pattern; might collapse 0x59ab/0xb7c6 into a single op).
- A capture where a *peer* (not the leader-self) leaves the group — that would fire the real OP_GroupDisband2 and let us confirm whether 0x6c57 (currently mapped) or 0x1325 is correct. Until that, leaving the 0x6c57 mapping from commit `964e493` in place — leader-self-disband legitimately wouldn't fire OP_GroupDisband2.

**Combat / buffs — too noisy without an event window**

`combat.vpk` and `buffs.vpk` had three to four competing 16-byte S>C unknowns each (e.g. 0xe42f, 0x15b4, 0xcace, 0x11b8) at sizes that match buff/spell-action structs. None of these are confirmable by count alone — all require time-correlated event marks (e.g. `--list-events` correlated to known cast/attack moments). Round-N: re-capture with a script that logs "I just hit auto-attack on/off at T_n" so we can window the C>S tally at exact moments. Same for buff click-off vs server-emitted buff fade.

**Generalizable lesson**: when one capture has multiple unknowns at the same size+direction, the count-based confirmation bar is the wrong tool. Switch to *content fingerprinting* — what does the payload actually carry? OP_GroupFollow2 was solved by noticing 0xe92d was the only one of the three carrying the *invitee's* name vs. self's name, which decoded the function purely from the byte content.

### 2026-05-07 — two-client capture: OP_GroupMemberList, OP_GroupCancelInvite, OP_GroupDisband2 confirmed

Captures: `trade-side-a.vpk` + `trade-side-b.vpk`. Both clients on the same LAN, captured simultaneously via the new `--ip` flag (commit `2d3dc29`). Activity: 3 group form/disband cycles (invites both ways), plus a mace-trade exchange (trade analysis pending — see "trade phase" below).

**OP_GroupMemberList = `0xb7c6`** (S>C, 168 B, n=3 each side). Strong fingerprint:
- Fires once per form event; broadcasts to **both** members (server pushes the same opcode to all current group members at form time).
- Does NOT fire on disband events.
- Matches OP_GroupMemberList's documented purpose: "List of group members - Variable length" — the rich 40-byte trailer in each fire is the variable-member-list payload, fixed at 168 B in 2-person groups.

**OP_GroupCancelInvite = `0xbf04`** (C>S only, 168 B, n=3 across both captures combined — 1 from side-A, 2 from side-B). Strong fingerprint:
- Pure C>S: never observed S>C in either capture. Rules out a server-broadcast mapping.
- Payload shape: `name1 = self, name2 = self` at offsets 0..63 / 64..127 — matches `groupDeclineStruct` ("yourname[64], membername[64]") for the self-leave case.
- Fires immediately before each `OP_GroupDisband (0x02a6)` server broadcast: 290–300 ms gap (client request → server confirm).
- Best fit on the unresolved list given the C>S-only direction and self-self name shape. Note: the legacy semantic was "Declining to join a group", but modern Live appears to unify decline/leave under one client-side opcode — the captures only contain disband events (no declines), so the decline branch is unverified.

**OP_GroupDisband2 = `0x6c57` confirmed** (existing mapping from commit `964e493`). The previous `group-disband.vpk` fixture was leader-self-disband, so OP_GroupDisband2 didn't fire. With three new disband events captured from both sides:

| Disband | Group leader | Disband-clicker | 0x6c57 fired on |
|---------|--------------|-----------------|-----------------|
| 1       | side-A       | side-B          | side-A (leader) |
| 2       | side-B       | side-B          | side-B (leader) |
| 3       | side-A       | side-A          | side-A (leader) |

**Rule: OP_GroupDisband2 fires on the leader's side after any group dissolution, regardless of who clicked disband.** The legacy comment "Other member disbanded" was misleading — the right framing is "you-as-leader, your group is gone." Mapping is correct as is, no change needed.

**Unmapped 168 B group-family fires (still unresolved)**:

- **`0x59ab`** (S>C, 168 B, n=3 across both captures). Fires once per form, **on the inviter's side only**. Carries the leader's (=inviter's) name at offset 64. Best unresolved-list candidate is `OP_GroupInvite2` (legacy: "you're inviting someone and you are grouped or get invited by a group"), but our captures all start ungrouped on each form, so the legacy semantic doesn't quite match. Could also be a brand-new modern Live opcode without a legacy name (e.g. `OP_GroupAcknowledge`). Skipping commit — round-N: capture an invite from a leader to a third party while already grouped (need 3+ players) to verify the OP_GroupInvite2 trigger.
- **`0x1325`** (S>C, 168 B, n=3 each side). Fires on every disband, broadcast to all members. No clean match on the unresolved list. Possibly a modern Live cleanup-broadcast opcode that doesn't have a legacy name, or a renamed entry on the unresolved list under different terminology. Round-N: cross-reference EQEmu's `OP_*` enum for a "post-disband cleanup" entry; alternatively monitor whether `0x1325` also fires on group-leader-changes or other state transitions.

**OP_GroupInvite size split**: observed `OP_GroupInvite (0x7380)` firing at **168 bytes C>S** (when sending an invite) and **176 bytes S>C** (when receiving an invite). The 8-byte tail delta is unexplored — likely a server-added "from-this-server" prefix. Mapping accepts both via `sizechecktype="none"`; no action needed.

**Trade phase — inconclusive**: 4 OP_MoveItem (`0xcb03`) fires per side during the group session indicate the mace bouncing between trade slots. Candidate trade opcodes (`0x4094` 484 B C>S × 4 each side; `0x8b75` 12508 B C>S × 1 side-A; `0x5855` 24 B bidirectional) are buried under post-zone-in chatter and group-state-broadcast bursts that contaminated the trade window. `0x4094` looks like a periodic client heartbeat (fires every ~12 s independent of activity), not a trade op. Round-N: capture a **pure trade-only session** (both clients sit in one zone, no group, no zone change, exactly one mace exchange) — that should drop the noise floor enough to nail OP_TradeRequest, OP_TradeRequestAck, OP_TradeAcceptClick, OP_FinishTrade, OP_TradeCoins.

### 2026-05-11 — OP_InspectRequest + OP_InspectAnswer (inspect-enabled-a.vpk)

Capture: `tests/replay/inspect-enabled-a.vpk`. Method: `--opcode-stats` + candidate matcher, n=5 inspect actions (A right-clicks B exactly 5 times).

- **OP_InspectAnswer = `0xce34`** (S>C, 1956 bytes, n=5). Zero competitors at 1956 bytes S>C. Payload at offset 4 = `94 00 00 00` = inspected spawn ID; item names appear starting around offset 136 ("Curscale Skullcap" visible). Matches `inspectDataStruct` field-for-field. Wired as `dir="server"` + `sizechecktype="match"`.

- **OP_InspectRequest = `0xa63b`** (C>S, 8 bytes, n=5). One fire per inspect click, zero C>S 8-byte competitors at n=5. Payload: `94 00 00 00 d4 00 00 00` = `{u32 target_spawn_id=0x94, u32 self_spawn_id=0xd4}`. The legacy hint had this as S>C (the old permission-ask to the inspected player); with modern always-on inspect that handshake is gone and the opcode is now purely C>S from the inspector. Updated hint direction to `DIR_Client` in opcodestats.cpp.

**Modern inspect flow** (always-on, no permission gate — confirmed with simultaneous A+B captures):
1. A right-clicks B → A's client sends `OP_InspectRequest` C>S (`0xa63b`, 8 bytes)
2. Server responds to A with `OP_InspectAnswer` S>C (`0xce34`, 1956 bytes, B's full gear + text)
3. **B receives nothing.** `0xa63b` and `0xce34` are completely absent from B's capture. The old S>C "you are being inspected" notification no longer exists.

**`0x551f` ruled out for inspect**: fired only 4 times (not 5) with three distinct spawn IDs (0x2d81, 0x73d1, 0x8e) — none matching B's spawn (0x94). Consistent second-field value of 4 across all samples looks like a SpawnAppearance-family broadcast for appearance type 4 changes from other zone members. Not inspect-related.

**`0x5719` direction flip**: in today's capture appears only C>S (3 fires, A's own spawn 0xd4, types 7/8/9 + small values). In May 7 trade VPKs it appeared S>C with 148 fires. Opcode table shuffled between those patch dates — treat as different opcodes sharing the same hex slot. The C>S variant here looks like a modern SpawnAppearance client-to-server state report (client reports own animation/appearance state to server); distinct from `OP_SpawnAppearance` (0xca91, S>C, server broadcasts to others).

**Round-N ideas**:
- Run B's capture simultaneously during inspect to see whether B receives a "you are being inspected" S>C notification and at what opcode.
- The `0x551f` appearance-type-4 pattern: could be PVP flag changes or some other appearance state; worth correlating with a capture where a known appearance change (PVP toggle?) occurs.

### 2026-05-11 — OP_BeginCast=0x271f, OP_CastSpell=0xf571 (buff-lifecycle-a/b.vpk)

Captures: `buff-lifecycle-a.vpk` (caster A) + `cuff-lifecycle-b.vpk` (observer B). 3 self-buff casts.

- **OP_CastSpell = `0xf571`** (C>S, 39 bytes, n=3). Only on A's capture; absent from B. Matches `startCastStruct` exactly (39 bytes): `{int32 slot, uint32 spellId, uint8[10] target_block=-1 for self, uint32 targetId=self_spawn, uint8[15] unknowns}`. Sample: slot=4, spellId=421, targetId=477 (A's own spawn = self-cast).

- **OP_BeginCast = `0x271f`** (S>C, 15 bytes, n=3 on A, n=3 on B). Broadcast to all nearby. Confirmed broadcast by B's capture — B sees the same n=3 fires. `beginCastStruct` in everquest.h is stale at 11 bytes; wire layout is `{uint32 spellId, uint16 casterSpawnId, uint16 castTime_ms, uint8[7] pad}` = 15 bytes. Using `uint8_t`/`none` in XML until struct is updated. Cross-confirmed in `aa_progress.vpk` (34 CastSpell vs 46 BeginCast — excess BeginCasts are from NPC/other-player casts visible in zone, not A's casts, as expected for a broadcast opcode).

**Pending per-cast opcodes (personal S>C, not broadcast to B):**

| opcode | dir | size | count | content | hypothesis |
|--------|-----|------|-------|---------|------------|
| `0xe42f` | S>C | 16 | =cast count | `{slot, spellId, 4, zeros}` | server ack to caster: "you are casting slot X spell Y" |
| `0x15b4` | S>C | 16 | =cast count | `{zeros[8], ts1, ts2}` | cast timing: start/deadline timestamps |
| `0x306c` | C>S | 8 | =cast count | `{0/1/2, self_spawn_id}` | unknown; sequential value per cast (buff slot target?) |

`0xe42f` and `0x15b4` always appear in exactly equal counts across all sessions (buff-lifecycle, aa_point, aa_progress). They fire paired, S>C, personal only. No matching names on unresolved list — likely modern server responses without legacy equivalents.

**`0x12cb` NOT the buff window**: 254 fires in this session, size distribution identical to all other sessions (153/129/177 base). Completely unaffected by the 3 spell casts. Not event-driven. Fires independently at high frequency. Different test needed — zone into a quiet/empty area and monitor frequency vs NPC count.

**`0xf571` struct update note**: `startCastStruct.unknown0008[10]` at offset 8 appears to be 10 bytes of `-1` (all 0xff) for self-targeted spells — likely an extended target list (5 × uint16 target IDs, all sentinel -1). `targetId` at offset 18 is the primary target (self spawn ID for self-buffs). Bytes 22–38 contain unknowns including what may be a random/session seed at bytes 24–27.

### 2026-05-11 — OP_GroupInvite2=0x0d97 + group-leave broadcast (group-3p-a/b/c.vpk)

Captures: simultaneous A (leader) + B + C (joiner) three-client session. Sequence: A+B form group, A invites C while grouped, B leaves, C leaves.

- **OP_GroupInvite2 = `0x0d97`** (C>S, 168 B, n=2 on A). Fires when A sends an invite while *already grouped* — behaviorally distinct from `OP_GroupInvite (0x7380)` which fires when ungrouped. Payload: `{char inviter[64], char invitee[64], uint8[40]}` matching groupInviteStruct layout. n=2 likely reflects the client sending the invite packet twice for one invite action (double-send or flow artifact — both fires carry the same invitee name). Absent from B and C captures (pure C>S).

**Unresolved 168-byte opcodes with confirmed behavior (no legacy names):**

| opcode | dir | fires on | content | behavior |
|--------|-----|----------|---------|----------|
| `0x59ab` | S>C | inviter only | offset 64 = invitee name | server ack to inviter: "your invite to X was sent" |
| `0x1325` | S>C | all current members on leave | offset 64 = a remaining member name | broadcast to all members when someone leaves; does NOT fire when final member dissolves (no one left to notify) |
| `0xe2b7` | S>C | leader only | offset 0 = leader name, offset 64 = joiner name | leader-specific "X joined your group" notification |

**`0x1325` semantics confirmed**: fires n=1 on A, B, and C when B leaves (2 remaining: A+C). Does not fire again when C leaves (only A remains — no broadcast needed). So trigger condition is: member leaves AND ≥2 members remain.

**Ruled out**: the prior hypothesis that `0x59ab` = `OP_GroupInvite2`. `0x59ab` is S>C (server→inviter), while `OP_GroupInvite2` is the client-initiated grouped invite. `0x0d97` (C>S) is the correct mapping. `0x59ab` has no matching legacy opcode name; leaving unmapped until a name is confirmed.

**Round-N ideas**: capture a group where B invites C (not the leader) to determine if `0x0d97` + `0x59ab` fire on B or only on the leader. This would clarify whether these are leader-specific or any-member-can-invite flows.

### 2026-05-12 — OP_ClickBuffOff=0x5855 (buff-clickoff.vpk + buff-clickoff2.vpk)

Captures: `buff-clickoff.vpk` (SK: Endure Cold + IVU, cast→remove×2) and `buff-clickoff2.vpk` (SK: IVU/Endure Cold/Dead Eye, messier session). Method: `--opcode-stats` + `--dump-payload 0x5855` + Python struct parser on `buffStruct`.

- **OP_ClickBuffOff = `0x5855`** (C>S, 24 bytes). Payload: `{u32 spawn_id=0, u32 type=0x24, u32 buff_slot, u8[12] pad}`. The opcode is bidirectional — S>C fires carry other appearance types (22, 42, 5, 6, etc.) and are unhandled. The C>S type=0x24 variant fires once per buff manually clicked off, with `buff_slot` identifying the slot index. Confirmed by:
  - `buff-clickoff`: 3 type-0x24 C>S fires (slots 8, 38, 68) appearing after the final round of buffs landed, matching the 2-round cast+remove sequence.
  - `buff-clickoff2`: 3 more type-0x24 C>S fires (slots 2, 0, 0) during a messier session with IVU auto-drop and Dead Eye.
  - Zone-in state report also uses this same opcode/type with a persistent slot (slot 12 in both captures = same SK character baseline).

**Side findings:**

- **`0x0691` (S>C, 4 bytes)**: fires once per `OP_Buff` event, payload = recipient spawn_id. Likely a buff-window companion notification ("buff window needs update for spawn X"). n=7/8 matching OP_Buff counts across both captures.

- **`buffStruct` (168B) layout confirmed at wire level**: `spawnid` at +0, `spellid` at +116, `duration` at +120, `spellslot` at +160, `changetype` at +164. `changetype=1` fires for BOTH buff-applied and buff-cleared (struct comment `1=fading` is stale/wrong). Slot-cleared fires have `spellid=0, duration=0`. Spell IDs confirmed: 235=Endure Cold, 225=IVU, 352=Dead Eye(SK).

- **`0x306c` (C>S, 8B)**: resolved as OP_BuffQuery — see 2026-05-13 entry below.

### 2026-05-13 — OP_BuffQuery=0x306c (buff-zone-sync.vpk + buff-306c-test.vpk)

Captures: `buff-zone-sync.vpk` (SK L18, Dead Eye zone-sync test) and `buff-306c-test.vpk` (SK L18, clean single-buff validation). Method: `--opcode-stats` + `--dump-payload` + Python struct parsing.

- **OP_BuffQuery = `0x306c`** (C>S, 8 bytes). Payload: `{u32 buff_slot, u32 self_spawn_id}`. Client requests the server reconfirm the current state of buff slot N. Server responds with OP_Buff containing the spell ID, remaining duration, and spellslot for that slot. The slot field in `0x306c` always matches the `spellslot` field in the responding OP_Buff.

**Key behavioural finding — "second zone" rule (confirmed across two independent captures):**

| Zone-in | Condition | `0x306c` fires? |
|---------|-----------|-----------------|
| Zone where buff was applied | Buff just cast | no |
| **First zone after buff applied** | Buff active | **no** |
| **Second zone after buff applied** | Buff active | **yes, once per slot** |
| Any zone after buff removed | No buff | no |

The buff must survive **one full zone cycle** before the client requests server verification. The client does not send `0x306c` for a "fresh" buff on the first zone — only after it has persisted through one zone boundary does it treat the buff as established and worth reconfirming on the next entry.

**Wire cross-validation:**

| Capture | `0x306c` payload | OP_Buff response | Match |
|---------|-----------------|-----------------|-------|
| buff-zone-sync | `{slot=1, spawn=14947}` | IVU(225) spellslot=1 dur=267 | ✓ |
| buff-306c-test | `{slot=0, spawn=15692}` | IVU(225) spellslot=0 dur=237 init=270 | ✓ |
| buff-lifecycle-a | `{2,477}`,`{1,477}`,`{0,477}` | SLW/SoW/Regen slots 2/1/0 | ✓ |

Duration delta in buff-306c-test: initialDuration=270, remaining=237 → 33 ticks × 6s = **198 seconds elapsed** since buff was cast, consistent with the user waiting ~3 minutes before zoning.

**`0xbbf0` clarification:** fires 10 times per zone-in (slots 0-9, all 0xffffffff). It always reports all slots as empty regardless of active buffs. The client uses its own tracking (not `0xbbf0`) to decide which slots to query via `0x306c`.
