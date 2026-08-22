/*
 *  zonemgr.h
 *  Copyright 2001,2007 Zaphod (dohpaz@users.sourceforge.net). All Rights Reserved.
 *  Copyright 2002-2012, 2012-2019 by the respective ShowEQ Developers
 *  Portions Copyright 2003 Fee (fee@users.sourceforge.net)
 *
 *  Contributed to ShowEQ by Zaphod (dohpaz@users.sourceforge.net)
 *  for use under the terms of the GNU General Public License,
 *  incorporated herein by reference.
 *
 *  This file is part of ShowEQ.
 *  http://www.sourceforge.net/projects/seq
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "zonemgr.h"
#include "seq-bridge-cxx/lib.h"
#include "packet.h"
#include "main.h"
#include "everquest.h"
#include "diagnosticmessages.h"
#include "netstream.h"

#include <cstring>

#include <QFile>
#include <QDataStream>
#include <QRegularExpression>

//----------------------------------------------------------------------
// constants
static const char magicStr[5] = "zon2"; // magic is the size of uint32_t + a null
static const uint32_t* magic = (uint32_t*)magicStr;
const float defaultZoneExperienceMultiplier = 0.75;

// Sequence of signals on initial entry into eq from character select screen
// EQPacket                              ZoneMgr                       isZoning
// ----------                            -------                       --------
// zoneEntry(ClientZoneEntryStruct)      zoneBegin()                   true
// PlayerProfile(charProfileStruct)      zoneBegin(shortName)          false
// zoneNew(newZoneStruct)                zoneEnd(shortName, longName)  false
//
// Sequence of signals on when zoning from zone A to zone B
// EQPacket                              ZoneMgr                       isZoning
// ----------                            -------                       --------
// zoneChange(zoneChangeStruct, client)                                true
// zoneChange(zoneChangeStruct, server)  zoneChanged(shortName)        true
// zoneEntry(ClientZoneEntryStruct)      zoneBegin()                   false
// PlayerProfile(charProfileStruct)      zoneBegin(shortName)          false
// zoneNew(newZoneStruct)                zoneEnd(shortName, longName)  false
//
ZoneMgr::ZoneMgr(QObject* parent, const char* name)
  : QObject(parent),
    m_zoning(false),
    m_zone_exp_multiplier(defaultZoneExperienceMultiplier),
    m_zonePointCount(0),
    m_zonePoints(0)
{
  setObjectName(name);
  m_shortZoneName = "unknown";
  m_longZoneName = "unknown";
  m_zoning = false;
  m_dzID = 0;

  if (showeq_params->restoreZoneState)
    restoreZoneState();
}

ZoneMgr::~ZoneMgr()
{
  if (m_zonePoints)
    delete [] m_zonePoints;
}

struct ZoneNames
{
  const char* shortName;
  const char* longName;
};

static const ZoneNames zoneNames[] =
{
#include "zones.h"
};

QString ZoneMgr::zoneNameFromID(uint16_t zoneId)
{
   const char* zoneName = NULL;
   if ((zoneId & 0x0fff) < (sizeof(zoneNames) / sizeof(ZoneNames)))
       zoneName = zoneNames[zoneId & 0x0fff].shortName;

   if (zoneName != NULL)
      return zoneName;

   seqDebug("ZoneMgr::zoneNameFromID: zone name not found: zoneId=%d", zoneId);
   QString tmpStr;
   tmpStr = QString::asprintf("unk_zone_%d", zoneId);
   return tmpStr;
}

QString ZoneMgr::zoneLongNameFromID(uint16_t zoneId)
{
   const char* zoneName = NULL;
   if ((zoneId & 0x0fff) < (sizeof(zoneNames) / sizeof(ZoneNames)))
       zoneName = zoneNames[zoneId & 0x0fff].longName;

   if (zoneName != NULL)
      return zoneName;

   seqDebug("ZoneMgr::zoneLongNameFromID: zone name not found: zoneId=%d", zoneId);
   QString tmpStr;
   tmpStr = QString::asprintf("unk_zone_%d", zoneId);
   return tmpStr;
}

const zonePointStruct* ZoneMgr::zonePoint(uint32_t zoneTrigger)
{
  if (!m_zonePoints)
    return 0;

  for (size_t i = 0; i < m_zonePointCount; i++)
    if (m_zonePoints[i].zoneTrigger == zoneTrigger)
      return &m_zonePoints[i];

  return 0;
}

void ZoneMgr::saveZoneState(void)
{
  QFile keyFile(showeq_params->saveRestoreBaseFilename + "Zone.dat");
  if (keyFile.open(QIODevice::WriteOnly))
  {
    QDataStream d(&keyFile);
    // write the magic string
    d << *magic;

    d << m_longZoneName;
    d << m_shortZoneName;
  }
}

void ZoneMgr::restoreZoneState(void)
{
  QString fileName = showeq_params->saveRestoreBaseFilename + "Zone.dat";
  QFile keyFile(fileName);
  if (keyFile.open(QIODevice::ReadOnly))
  {
    QDataStream d(&keyFile);

    // check the magic string
    uint32_t magicTest;
    d >> magicTest;

    if (magicTest != *magic)
    {
      seqWarn("Failure loading %s: Bad magic string!",
              fileName.toLatin1().data());
      return;
    }

    d >> m_longZoneName;
    d >> m_shortZoneName;

    seqInfo("Restored Zone: %s (%s)!",
            m_shortZoneName.toLatin1().data(),
            m_longZoneName.toLatin1().data());
  }
  else
  {
    seqWarn("Failure loading %s: Unable to open!",
            fileName.toLatin1().data());
  }
}

void ZoneMgr::zoneEntryClient(const uint8_t* data, size_t len, uint8_t dir)
{
  const ClientZoneEntryStruct* zsentry = (const ClientZoneEntryStruct*)data;

  m_shortZoneName = "unknown";
  m_longZoneName = "unknown";
  m_zone_exp_multiplier = defaultZoneExperienceMultiplier;
  m_zoning = false;

  emit zoneBegin();
  emit zoneBegin(zsentry, len, dir);

  if (showeq_params->saveZoneState)
    saveZoneState();
}

int32_t ZoneMgr::fillProfileStruct(charProfileStruct *player, const uint8_t *data, size_t len, bool checkLen)
{
  auto out = seq::rust::decode_player_profile(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) {
    seqDebug("decode_player_profile: parse failed (len=%zu)", len);
    return 0;
  }

  player->checksum = out.checksum;

  // profile.*
  player->profile.gender = out.gender;
  player->profile.race = out.race;
  player->profile.class_ = out.class_;
  player->profile.level = out.level;
  player->profile.level1 = out.level1;

  // binds[0]: only the first slot is consumed downstream (Player::loadProfile
  // logs it). Remaining slots stay zeroed.
  player->profile.binds[0].zoneId = out.bind0_zone_id;
  player->profile.binds[0].x = out.bind0_x;
  player->profile.binds[0].y = out.bind0_y;
  player->profile.binds[0].z = out.bind0_z;
  player->profile.binds[0].heading = out.bind0_heading;

  player->profile.deity = out.deity;
  player->profile.intoxication = out.intoxication;
  player->profile.points = out.points;
  player->profile.MANA = out.mana;
  player->profile.curHp = out.cur_hp;
  player->profile.STR = out.str_;
  player->profile.STA = out.sta;
  player->profile.CHA = out.cha;
  player->profile.DEX = out.dex;
  player->profile.INT = out.int_;
  player->profile.AGI = out.agi;
  player->profile.WIS = out.wis;

  // AAs — daemon iterates the dense array reading .AA and .value.
  {
    const auto* ids = out.aa_ids.data();
    const auto* vals = out.aa_values.data();
    const std::size_t n = std::min<std::size_t>(out.aa_ids.size(), MAX_AA);
    for (std::size_t i = 0; i < n; ++i) {
      player->profile.aa_array[i].AA = ids[i];
      player->profile.aa_array[i].value = vals[i];
    }
  }

  // Disciplines, recast timers, mem spells, refresh timers — copied
  // best-effort into their fixed-size slots.
  {
    const std::size_t n = std::min<std::size_t>(out.disciplines.size(),
        sizeof(player->profile.disciplines) / sizeof(uint32_t));
    std::memcpy(player->profile.disciplines, out.disciplines.data(),
                n * sizeof(uint32_t));
  }
  {
    const std::size_t n = std::min<std::size_t>(out.recast_timers.size(),
        sizeof(player->profile.recastTimers) / sizeof(uint32_t));
    std::memcpy(player->profile.recastTimers, out.recast_timers.data(),
                n * sizeof(uint32_t));
  }
  {
    const std::size_t n = std::min<std::size_t>(out.spell_book.size(),
        sizeof(player->profile.sSpellBook) / sizeof(int32_t));
    std::memcpy(player->profile.sSpellBook, out.spell_book.data(),
                n * sizeof(int32_t));
  }
  {
    const std::size_t n = std::min<std::size_t>(out.mem_spells.size(),
        sizeof(player->profile.sMemSpells) / sizeof(int32_t));
    std::memcpy(player->profile.sMemSpells, out.mem_spells.data(),
                n * sizeof(int32_t));
  }
  {
    const std::size_t n = std::min<std::size_t>(out.spell_slot_refresh.size(),
        sizeof(player->profile.spellSlotRefresh) / sizeof(uint32_t));
    std::memcpy(player->profile.spellSlotRefresh,
                out.spell_slot_refresh.data(), n * sizeof(uint32_t));
  }

  // Buffs — only spellid + duration are consumed (Player::loadProfile,
  // MessageShell::player, SpellShell::buffLoad). Rest of the spellBuff
  // slot stays zeroed.
  {
    const std::size_t n = std::min<std::size_t>(out.buff_spell_ids.size(),
        sizeof(player->profile.buffs) / sizeof(spellBuff));
    for (std::size_t i = 0; i < n; ++i) {
      player->profile.buffs[i].spellid  = out.buff_spell_ids[i];
      player->profile.buffs[i].duration = out.buff_durations[i];
    }
  }

  player->profile.platinum = out.platinum;
  player->profile.gold = out.gold;
  player->profile.silver = out.silver;
  player->profile.copper = out.copper;
  player->profile.platinum_cursor = out.platinum_cursor;
  player->profile.gold_cursor = out.gold_cursor;
  player->profile.silver_cursor = out.silver_cursor;
  player->profile.copper_cursor = out.copper_cursor;
  player->profile.aa_spent = out.aa_spent;
  player->profile.aa_assigned = out.aa_assigned;
  player->profile.aa_unspent = out.aa_unspent;
  player->profile.endurance = out.endurance;

  // charProfileStruct top-level
  player->expAA = out.exp_aa;
  std::memcpy(player->name, out.name.data(),
              std::min(out.name.size(), sizeof(player->name) - 1));
  std::memcpy(player->lastName, out.last_name.data(),
              std::min(out.last_name.size(), sizeof(player->lastName) - 1));
  player->birthdayTime = out.birthday_time;
  player->accountCreateDate = out.account_create_date;
  player->lastSaveTime = out.last_save_time;
  player->timePlayedMin = out.time_played_min;
  player->expansions = out.expansions;

  {
    const std::size_t n = std::min<std::size_t>(out.languages.size(),
        sizeof(player->languages));
    std::memcpy(player->languages, out.languages.data(), n);
  }

  player->zoneId = out.zone_id;
  player->zoneInstance = out.zone_instance;
  player->x = out.x;
  player->y = out.y;
  player->z = out.z;
  player->heading = out.heading;
  player->standState = out.stand_state;
  player->anon = out.anon;
  player->guildID = (int32_t)out.guild_id;
  player->guildServerID = out.guild_server_id;
  player->platinum_inventory = out.platinum_inventory;
  player->gold_inventory = out.gold_inventory;
  player->silver_inventory = out.silver_inventory;
  player->copper_inventory = out.copper_inventory;
  player->platinum_bank = out.platinum_bank;
  player->gold_bank = out.gold_bank;
  player->silver_bank = out.silver_bank;
  player->copper_bank = out.copper_bank;
  player->platinum_shared = out.platinum_shared;
  player->careerTribute = out.career_tribute;
  player->currentTribute = out.current_tribute;
  player->currentRadCrystals = out.current_rad_crystals;
  player->careerRadCrystals = out.career_rad_crystals;
  player->currentEbonCrystals = out.current_ebon_crystals;
  player->careerEbonCrystals = out.career_ebon_crystals;
  player->autosplit = out.autosplit;
  player->ldon_guk_points = out.ldon_guk_points;
  player->ldon_mir_points = out.ldon_mir_points;
  player->ldon_mmc_points = out.ldon_mmc_points;
  player->ldon_ruj_points = out.ldon_ruj_points;
  player->ldon_tak_points = out.ldon_tak_points;
  player->ldon_avail_points = out.ldon_avail_points;

  const int32_t retVal = static_cast<int32_t>(out.bytes_consumed);
  if (checkLen && static_cast<int32_t>(len) != retVal)
  {
    seqDebug("ZoneMgr::fillProfileStruct - expected length: %zu, read: %d for player '%s'",
             len, retVal, player->name);
  }

  return retVal;
}


void ZoneMgr::zonePlayer(const uint8_t* data, size_t len)
{
  charProfileStruct *player = new charProfileStruct;

  memset(player,0,sizeof(charProfileStruct));

  fillProfileStruct(player,data,len,false); // don't bother checking the length since it's always going to not match up

  // fillProfileStruct's netstream parser handles most variable-length
  // sections correctly, but it explicitly skips the skills array (reads
  // its length prefix, then skipBytes through the values). Overlay
  // skills directly so Player::charProfile sees real values. Confirmed
  // 2026-05-02 across two captures (combat.vpk @ 23875 bytes; login.vpk
  // @ 37088 bytes): wire offset 4618 holds u32=100 (MAX_KNOWN_SKILLS
  // length prefix) and 4622..5021 holds uint32[100] skills.
  //
  // For reference (no overlay needed — the netstream parser populates
  // it correctly): wire 1014 holds u32=300 (MAX_AA prefix), 1018..4617
  // is AA_Array[300] (12 bytes each: aa_id, value, unk). On this opcode
  // every entry's `value` is 0 — only auto-granted / server-tracked AA
  // ids appear here. Purchased AA ranks (Origin, Veteran's Enhancement,
  // etc.) come over OP_SendAATable (still unresolved).
  //
  // Defensive: bail if the wire is too short.
  constexpr size_t kSkillsWireOffset = 4622;
  if (len >= kSkillsWireOffset + sizeof(player->profile.skills)) {
    memcpy(player->profile.skills, data + kSkillsWireOffset,
           sizeof(player->profile.skills));
  }

  // Buffs: same parser-skid story as skills, plus the buff array offset
  // *varies between captures* (observed 0x30e4 / 0x31c1 / 0x330b across
  // 19 OP_PlayerProfile dumps) because the netstream parser consumes
  // different numbers of bytes upstream depending on what else is in
  // the profile.
  //
  // The slot's field layout shifted post-patch — the leading 12-byte
  // unknown block in legacy spellBuff is gone, so spellid / duration /
  // initDuration / level / effect all sit 12 bytes earlier than their
  // legacy comment-offsets. See the updated spellBuff in everquest.h.
  //
  // Locate the array by per-slot signature. The ONLY field the server
  // initializes identically on every slot (occupied or empty) is a run
  // of six 0xFFFFFFFF sentinels at +26, +38, +50, +62, +74, +86 (vacant
  // sub-effect cells). That 24-byte fingerprint is the discriminator.
  //
  // Do NOT anchor on the +98 'modifier' float: it is 1.0 on occupied
  // slots but holds residual garbage (e.g. 1.02) on some EMPTY slots, so
  // requiring it splits the array at the first such slot. A 3-buff
  // profile (buff3.vpk: Summon Frostreaver@slot0, Armor of Faith@slot2,
  // Symbol of Naltron@slot5, with an empty-but-dirty slot4) was decoding
  // only Symbol — the longest float+marker run started *after* slot4,
  // dropping the two buffs before it.
  //
  // The buff array is adjacent to another 110-strided 0xff-marker
  // structure, so the longest marker run alone is ambiguous. Pick the
  // run whose 42-slot window holds the MOST plausible occupied buffs
  // (valid spellid/duration/level); tie-break on run length so an empty
  // buff array still resolves to the right region. Overlay the
  // 42*110=4620-byte array onto player->profile.buffs.
  {
    constexpr uint32_t kFF = 0xffffffffu;
    constexpr size_t kMarkers[] = {26, 38, 50, 62, 74, 86};
    auto markersOk = [&](size_t off) {
      if (off + sizeof(spellBuff) > len) return false;
      uint32_t v;
      for (size_t m : kMarkers) {
        std::memcpy(&v, data + off + m, sizeof(v));
        if (v != kFF) return false;
      }
      return true;
    };
    auto countBuffs = [&](size_t start) {
      int n = 0;
      for (int i = 0; i < MAX_BUFFS; ++i) {
        const size_t b = start + size_t(i) * sizeof(spellBuff);
        if (b + sizeof(spellBuff) > len) break;
        int32_t dur, sp;
        std::memcpy(&dur, data + b, sizeof(dur));
        std::memcpy(&sp, data + b + 9, sizeof(sp)); // unaligned spellid
        const uint8_t lvl = data[b + 8];
        if (sp >= 1 && sp <= 200000 && dur >= 1 && dur <= 400000 &&
            lvl >= 1 && lvl <= 127)
          ++n;
      }
      return n;
    };
    size_t buffArrayStart = 0;
    int bestBuffs = -1;
    size_t bestRun = 0;
    for (size_t off = 0x3000; off + sizeof(spellBuff) <= len; ) {
      if (!markersOk(off)) { ++off; continue; }
      size_t run = 1;
      size_t cur = off + sizeof(spellBuff);
      while (markersOk(cur)) { ++run; cur += sizeof(spellBuff); }
      const int nb = countBuffs(off);
      if (nb > bestBuffs || (nb == bestBuffs && run > bestRun)) {
        bestBuffs = nb; bestRun = run; buffArrayStart = off;
      }
      off = cur;
    }
    if (bestRun >= 3) {
      const size_t bufBytes = sizeof(player->profile.buffs);
      const size_t copyBytes =
          std::min(bufBytes, len - buffArrayStart);
      std::memcpy(player->profile.buffs, data + buffArrayStart, copyBytes);
    }
  }

  if (m_rustLifecycleProbe && m_rustLifecycleProbe()) {
    if (!m_rustProfileAcceptedProbe || m_rustProfileAcceptedProbe()) {
      // The shared Phase-4 profile event intentionally omits Live's zone id.
      // Once Rust has accepted the profile, retain this host-only projection
      // input so map/filter/public zone behavior is not lost in Rust mode.
      m_shortZoneName = zoneNameFromID(player->zoneId);
      m_longZoneName = zoneLongNameFromID(player->zoneId);
      m_zone_exp_multiplier = defaultZoneExperienceMultiplier;
      m_zoning = false;
      emit zoneBegin(m_shortZoneName);
      emit playerProfileSupplement(player);
    }
    return;
  }

  m_shortZoneName = zoneNameFromID(player->zoneId);
  m_longZoneName = zoneLongNameFromID(player->zoneId);
  m_zone_exp_multiplier = defaultZoneExperienceMultiplier;
  m_zoning = false;

  emit zoneBegin(m_shortZoneName);
  emit playerProfile(player);

  if (showeq_params->saveZoneState)
    saveZoneState();
}

void ZoneMgr::zoneChange(const uint8_t* data, size_t len, uint8_t dir)
{
  auto out = seq::rust::decode_zone_change(
      rust::Slice<const uint8_t>{data, sizeof(zoneChangeStruct)});
  if (!out.ok) return;
  zoneChangeStruct tmp{};
  std::memcpy(tmp.name, out.name.data(),
              std::min(out.name.size(), sizeof(tmp.name) - 1));
  tmp.zoneId       = out.zone_id;
  tmp.zoneInstance = out.zone_instance;
  const zoneChangeStruct* zoneChange = &tmp;

  m_shortZoneName = zoneNameFromID(zoneChange->zoneId);
  m_longZoneName = zoneLongNameFromID(zoneChange->zoneId);
  m_zone_exp_multiplier = defaultZoneExperienceMultiplier;
  m_zoning = true;

  if (dir == DIR_Server)
    emit zoneChanged(m_shortZoneName);
    emit zoneChanged(zoneChange, len, dir);

  if (showeq_params->saveZoneState)
    saveZoneState();
}

void ZoneMgr::beginZoning()
{
  m_zone_exp_multiplier = defaultZoneExperienceMultiplier;
  m_zoning = true;
  emit zoneTransitionStarted();
  if (showeq_params->saveZoneState)
    saveZoneState();
}

void ZoneMgr::applyLifecycleTransition(const QString&, bool hasZoneId,
                                       uint32_t zoneId, bool,
                                       uint32_t, bool confirmed)
{
  if (!confirmed || !hasZoneId) {
    beginZoning();
    return;
  }
  m_shortZoneName = zoneNameFromID(uint16_t(zoneId));
  m_longZoneName = zoneLongNameFromID(uint16_t(zoneId));
  m_zone_exp_multiplier = defaultZoneExperienceMultiplier;
  m_zoning = true;
  emit zoneChanged(m_shortZoneName);
  if (showeq_params->saveZoneState)
    saveZoneState();
}

void ZoneMgr::applyLifecycleZone(const QString& shortName,
                                 const QString& longName)
{
  // NewZone arrives as ZoneChanged + ZoneEnvironmentChanged in one Rust
  // batch. Stage the names here; the environment event publishes the visible
  // signal only after safe point/zone-file/experience state is installed.
#if defined(SEQ_TARGET_EQL)
  m_shortZoneName = shortName;
  m_longZoneName = longName;
#else
  adoptLiveZoneNames(shortName, longName);
#endif
  m_zoning = false;
}

void ZoneMgr::adoptLiveZoneNames(const QString& shortName,
                                 const QString& longName)
{
  // Prefer the canonical zone-id name already learned from Profile/ZoneChange.
  // Wire NewZone text can be localized and may carry instance/progression
  // suffixes that have no map package.
  if (m_shortZoneName.isEmpty() || m_shortZoneName.startsWith("unk")) {
    m_shortZoneName = shortName;
    m_shortZoneName.remove(QRegularExpression("_\\d+$"));
    m_shortZoneName.remove(QRegularExpression("_progress$"));
    m_shortZoneName.remove(QRegularExpression("_int$"));
    m_shortZoneName.remove(QRegularExpression("_errand$"));
  }
  m_longZoneName = longName;
}

void ZoneMgr::applyLifecycleEnvironment(const QString& zoneFile,
                                        float experienceMultiplier,
                                        float safeXValue, float safeYValue,
                                        float safeZValue)
{
  m_zoneFile = zoneFile;
  m_zone_exp_multiplier = experienceMultiplier;
  m_safeX = safeXValue;
  m_safeY = safeYValue;
  m_safeZ = safeZValue;
  m_safePoint.setPoint(lrintf(safeXValue), lrintf(safeYValue),
                       lrintf(safeZValue));
#if defined(SEQ_TARGET_EQL)
  // EQL resolves NewZone after its spawn bulk, so preserve that bulk and use
  // the non-clearing map/filter/web path.
  setZoneByName(m_shortZoneName, m_longZoneName);
#else
  // Live/Test historically publish zoneEnd only, after all environment state
  // is installed. SessionAdapter/map consumers depend on this ordering.
  emit zoneEnd(m_shortZoneName, m_longZoneName);
  if (showeq_params->saveZoneState)
    saveZoneState();
#endif
}

void ZoneMgr::zoneNew(const uint8_t* data, size_t len, uint8_t dir)
{
  auto out = seq::rust::decode_new_zone(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;

  m_safePoint.setPoint(lrintf(out.safe_x), lrintf(out.safe_y),
                       lrintf(out.safe_z));
  m_safeX = out.safe_x;
  m_safeY = out.safe_y;
  m_safeZ = out.safe_z;
  m_zoneFile = QString::fromLatin1(out.zonefile.data(), out.zonefile.size());
  m_zone_exp_multiplier = out.zone_exp_multiplier;

  // ZBNOTE: Apparently these come in with the localized names, which means we
  //         may not wish to use them for zone short names.
  //         An example of this is: shortZoneName 'ecommons' in German comes
  //         in as 'OGemeinl'.  OK, now that we have figured out the zone id
  //         issue, we'll only use this short zone name if there isn't one or
  //         it is an unknown zone.
  adoptLiveZoneNames(
      QString::fromLatin1(out.short_name.data(), out.short_name.size()),
      QString::fromLatin1(out.long_name.data(), out.long_name.size()));
  m_zoning = false;

#if 1 // ZBTEMP
  const std::string longNameStd(out.long_name);
  seqDebug("Welcome to lovely downtown '%s' with an experience multiplier of %f",
           longNameStd.c_str(), out.zone_exp_multiplier);
  seqDebug("Safe Point (%f, %f, %f)",
           out.safe_x, out.safe_y, out.safe_z);
#endif // ZBTEMP

  emit zoneEnd(m_shortZoneName, m_longZoneName);

  if (showeq_params->saveZoneState)
    saveZoneState();
}

// Target-neutral primitive for the eql backend (EqlDispatch): the caller has
// already parsed OP_NewZone into short/long names, so just adopt them and drive
// the map/filter/web via zoneResolved. Unlike zoneNew this does not touch the
// exp multiplier / safe point / saved zone state. Deliberately emits
// zoneResolved, NOT zoneChanged: on eql the zone name arrives after the bulk
// spawn list + profile (fresh box per zone), so zoneChanged's spawn-clear +
// player-reset slots would wipe just-loaded state. See setZoneByName() in
// zonemgr.h.
void ZoneMgr::setZoneByName(const QString& shortName, const QString& longName)
{
  m_shortZoneName = shortName;
  m_longZoneName  = longName.isEmpty() ? shortName : longName;
  m_zoning = false;

  seqInfo("ZoneMgr: zone '%s' (%s)",
          m_shortZoneName.toLatin1().data(), m_longZoneName.toLatin1().data());

  emit zoneResolved(m_shortZoneName);
}

void ZoneMgr::zonePoints(const uint8_t* data, size_t len, uint8_t)
{
  // Wire format: u32 count, then count × zonePointStruct (24b each),
  // then a 24-byte trailing block we ignore.
  if (len < 4) return;
  uint32_t count;
  std::memcpy(&count, data, sizeof(count));
  constexpr size_t ZP_LEN = sizeof(zonePointStruct);
  if (len < 4 + static_cast<size_t>(count) * ZP_LEN) return;

  m_zonePointCount = count;

  if (m_zonePoints)
    delete [] m_zonePoints;
  m_zonePoints = new zonePointStruct[m_zonePointCount];

  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* p = data + 4 + i * ZP_LEN;
    auto out = seq::rust::decode_zone_point(
        rust::Slice<const uint8_t>{p, ZP_LEN});
    if (!out.ok) continue;
    m_zonePoints[i].zoneTrigger  = out.zone_trigger;
    m_zonePoints[i].y            = out.y;
    m_zonePoints[i].x            = out.x;
    m_zonePoints[i].z            = out.z;
    m_zonePoints[i].heading      = out.heading;
    m_zonePoints[i].zoneId       = out.zone_id;
    m_zonePoints[i].zoneInstance = out.zone_instance;
  }
}

void ZoneMgr::dynamicZonePoints(const uint8_t *data, size_t len, uint8_t)
{
   if(len == sizeof(dzSwitchInfo))
   {
      auto out = seq::rust::decode_dz_switch_info(
          rust::Slice<const uint8_t>{data, len});
      if (!out.ok) return;
      m_dzPoint.setPoint(lrintf(out.x), lrintf(out.y), lrintf(out.z));
      m_dzID = out.zone_id;
      m_dzLongName = zoneLongNameFromID(m_dzID);
      if(out.kind != 1 && out.kind > 2 && out.kind <= 5)
         m_dzType = 0; // green
      else
         m_dzType = 1; // pink
   }
   else if(len == 8)
   {
      // we quit the expedition
      m_dzPoint.setPoint(0, 0, 0);
      m_dzID = 0;
      m_dzLongName = "";
   }
}

void ZoneMgr::dynamicZoneInfo(const uint8_t* data, size_t len, uint8_t)
{
   if (len != sizeof(dzInfo)) return;
   auto out = seq::rust::decode_dz_info(
       rust::Slice<const uint8_t>{data, len});
   if (!out.ok) return;

   if(!out.new_dz)
   {
      m_dzPoint.setPoint(0, 0, 0);
      m_dzID = 0;
      m_dzLongName = "";
   }
}
