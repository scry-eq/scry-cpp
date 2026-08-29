/*
 *  player.cpp
 *  Copyright 2000-2008, 2012-2016, 2019 by the respective ShowEQ Developers
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

#include "player.h"
#include "seq-bridge-cxx/lib.h"
#include "util.h"
#include "packetcommon.h"

#include "diagnosticmessages.h"
#include "guild.h"
#include "zonemgr.h"
#include "main.h"

#include <cstdio>
#include <algorithm>
#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QDataStream>


// #define DEBUG_PLAYER

//----------------------------------------------------------------------
// constants
// "plr3": widened m_maxSTR..m_maxCHA from uint8_t to uint16_t. Older
// "plr2" files would deserialize 1 byte where 2 are expected and
// corrupt every following field, so the version bump rejects them.
static const char magicStr[5] = "plr3"; // magic is the size of uint32_t + a null
static const uint32_t* magic = (uint32_t*)magicStr;


Player::Player (QObject* parent,
		ZoneMgr* zoneMgr,
		GuildMgr* guildMgr,
		const char* name)
  : QObject(parent),
    Spawn(),
    m_zoneMgr(zoneMgr),
    m_guildMgr(guildMgr)
{
  setObjectName(name);
#ifdef DEBUG_PLAYER
  qDebug ("Player()");
#endif

  connect(m_zoneMgr, SIGNAL(zoneChanged(const QString&)),
          this, SLOT(zoneChanged()));
  
  m_NPC = SPAWN_SELF;

  QString section = "Defaults";
  m_useAutoDetectedSettings = 
    pSEQPrefs->getPrefBool("useAutoDetectedSettings", section, true);
  m_defaultName = pSEQPrefs->getPrefString("DefaultName", section, "You");
  m_defaultLastName = pSEQPrefs->getPrefString("DefaultLastName", section, "");
  m_defaultLevel = pSEQPrefs->getPrefInt("DefaultLevel", section, 1);
  m_defaultRace = pSEQPrefs->getPrefInt("DefaultRace", section, 1);
  m_defaultClass = pSEQPrefs->getPrefInt("DefaultClass", section, 1);
  m_defaultDeity = pSEQPrefs->getPrefInt("DefaultDeity", section, DEITY_AGNOSTIC);
  
  setUseDefaults(true);

  // set the name to the default name
  Spawn::setName(m_defaultName);

  // restore the player state if the user requested it...
  if (showeq_params->restorePlayerState)
    restorePlayerState();
  else
  {
    reset();
    clear();
  }
}

Player::~Player()
{
}

void Player::clear()
{
  m_plusMana = 0; 
  m_plusHP = 0;
  m_maxSTR = 0;
  m_maxSTA = 0;
  m_maxCHA = 0;
  m_maxDEX = 0;
  m_maxINT = 0;
  m_maxAGI = 0;
  m_maxWIS = 0;
  m_maxMana = 0;
  m_maxHP = 0;
  m_curHP = 0;
  m_food = 0;
  m_water = 0;
  m_fatigue = 0;
  m_enduranceCur = 0;
  m_enduranceMax = 0;

  m_validStam = false;
  m_validMana = false;
  m_validHP = false;
  m_validAttributes = false;

  m_lastSpawnKilledName = "unknown";
  m_lastSpawnKilledLevel = 0;
  m_freshKill = false;

  m_heading = 0;
  m_headingDegrees = 360 - ((m_heading * 360) >> 11);
  
  setID(0);
  setPoint(0,0,0);
  m_validPos = false;

  updateLastChanged();
}

void Player::reset()
{
  setUseDefaults(true);

  m_currentAltExp = 0;
  m_currentExp = 0;
  // Without an explicit init this stays uninitialized until the first
  // OP_AAExpUpdate or playerProfile arrives. The PlayerStats encoder
  // serializes it unconditionally, so before the player profile loads
  // (e.g. at the start of a --replay golden capture) we'd otherwise
  // emit whatever happened to be on the heap, breaking determinism.
  m_currentAApts = 0;
  m_currentAAUnspent = 0;
  // Live ships per-level exp progress as 0-100000 (see expUpdateStruct in
  // everquest.h); the legacy `calc_exp() / 330` machinery was for the old
  // cumulative-exp-on-a-330-tick-scale wire format and doesn't apply.
  m_minExp = 0;
  m_maxExp = 100000;
  m_tickExp = 1;

  for (int a = 0; a < MAX_KNOWN_SKILLS; a++)
    m_playerSkills[a] = 255; // indicate an invalid value

  for (int a = 0; a < MAX_KNOWN_LANGS; a++)
    m_playerLanguages[a] = 255; // indicate an invalid value
  
  m_mana = 0;
  setLevel (1);
  setRace (1);
  setClassVal (1);

  emit deleteSkills();
  emit deleteLanguages();

  m_validExp = false;
  m_validAttributes = false;

  updateLastChanged();
}

void Player::killSpawn()
{
#ifdef DEBUG_PLAYER
    seqDebug("Player: killSpawn()");
#endif
    // We're dead. No buffs for us.
    
    // Call our parent.
    Spawn::killSpawn();
}

void Player::setUseAutoDetectedSettings(bool enable)
{
  m_useAutoDetectedSettings = enable;
  pSEQPrefs->setPrefBool("useAutoDetectedSettings", "Defaults", enable);
}

void Player::setDefaultName(const QString& name)
{
  m_defaultName = name;
  pSEQPrefs->setPrefString("DefaultName", "Defaults", name);
}

void Player::setDefaultLastname(const QString& lastName)
{
  m_defaultLastName = lastName;
  pSEQPrefs->setPrefString("DefaultLastName", "Defaults", lastName);
}

void Player::setDefaultLevel(uint8_t level)
{
  m_defaultLevel = level;
  pSEQPrefs->setPrefInt("DefaultLevel", "Defaults", level);
}

void Player::setDefaultRace(uint16_t race)
{
  m_defaultRace = race;
  pSEQPrefs->setPrefInt("DefaultRace", "Defaults", race);
}

void Player::setDefaultClass(uint8_t classVal)
{
  m_defaultClass = classVal;
  pSEQPrefs->setPrefInt("DefaultClass", "Defaults", classVal);
}

void Player::setDefaultDeity(uint16_t deity)
{
  m_defaultDeity = deity;
  pSEQPrefs->setPrefInt("DefaultDeity", "Defaults", deity);
}

void Player::loadProfile(const playerProfileStruct& player)
{
  const bool applyProgression =
      !m_progressionMutationGuard || m_progressionMutationGuard();
  setUseDefaults(false);

  setGender(player.gender);
  setRace(player.race);
  setClassVal(player.class_);
  setDeity(player.deity);
  // Intentionally do NOT seed m_curHP from player.curHp here. On modern
  // Live the profile's curHp slot is stale (typically reflects the last
  // server-side snapshot rather than current HP), so it shows e.g. 2777
  // when the player is actually at 4000/4000. Authoritative cur HP comes
  // from OP_HPUpdate (Player::updateNpcHP). m_validHP stays false until
  // the first such packet, and the encoder displays "—" in the meantime.
  setLevel(player.level);

  emit levelChanged(level());

  // Reset to the profile's base stats. wearItem() accumulates equipment
  // bonuses on top via +=, and a fresh OP_ItemPacket burst re-arrives
  // after every OP_PlayerProfile (i.e. on every zone-in), so a multi-
  // zone session would otherwise stack the base stats — observed as
  // doubled str/sta/etc. after a tutoriala -> tutorialb hop.
  m_maxSTR = player.STR;
  m_maxSTA = player.STA;
  m_maxCHA = player.CHA;
  m_maxDEX = player.DEX;
  m_maxINT = player.INT;
  m_maxAGI = player.AGI;
  m_maxWIS = player.WIS;
  
  emit statChanged (LIST_STR, m_maxSTR, m_maxSTR);
  emit statChanged (LIST_STA, m_maxSTA, m_maxSTA);
  emit statChanged (LIST_CHA, m_maxCHA, m_maxCHA);
  emit statChanged (LIST_DEX, m_maxDEX, m_maxDEX);
  emit statChanged (LIST_INT, m_maxINT, m_maxINT);
  emit statChanged (LIST_AGI, m_maxAGI, m_maxAGI);
  emit statChanged (LIST_WIS, m_maxWIS, m_maxWIS);
  
  // Done with stats
  m_validAttributes = true;

  // Intentionally do NOT seed m_mana from player.MANA here. Like curHp,
  // the profile's MANA slot is a stale snapshot — it can equal max (when
  // the player zoned at full) or some unrelated past value (e.g. 3844
  // when actual was 2985/5379). Authoritative cur mana comes from
  // OP_ManaChange (Player::manaChange). m_validMana stays false until
  // the first such packet, and the encoder displays "—" in the meantime.
  //
  // m_maxMana from calcMaxMana is a lower-bound estimate (legacy formula
  // misses item/AA contributions on modern Live). The encoder's floor
  // rule snaps it up to cur when OP_ManaChange arrives with a higher
  // value, which covers the common case of the player being near full.
  m_maxMana = calcMaxMana( m_maxINT, m_maxWIS,
                           m_class, m_level
			 ) + m_plusMana;

  // location of bind point
#if 1
  seqDebug("Player::backfill(bind): Pos (%f/%f/%f) Heading: %f",
	   player.binds[0].x, player.binds[0].y, player.binds[0].z, 
       player.binds[0].heading);
#endif

  // Exp handling — live uses per-level 0-100000 progress, not absolute
  // cumulative exp; see Player ctor for the rationale.
  m_minExp = 0;
  m_maxExp = 100000;
  m_tickExp = 1;

  // Merge in our new skills...
  if (applyProgression) {
    for (int a = 0; a < MAX_KNOWN_SKILLS; a++)
    {
      m_playerSkills[a] = player.skills[a];
      emit addSkill (a, m_playerSkills[a]);
    }
  }

  // copy in the spell book
  memcpy (&m_spellBookSlots[0], &player.sSpellBook[0], sizeof(m_spellBookSlots));

  if (applyProgression) {
    m_currentAApts = player.aa_spent;
    m_currentAAUnspent = player.aa_unspent;
    m_purchasedAA.clear();
    for (int i = 0; i < MAX_AA; ++i) {
      if (player.aa_array[i].AA == 0 || player.aa_array[i].value == 0) continue;
      m_purchasedAA.push_back({player.aa_array[i].AA, player.aa_array[i].value});
    }
  }

  // Buffs: modern spellBuff is 110 bytes (same total as legacy) but the
  // field layout shifted (-12 bytes) — see everquest.h. The buff array
  // is overlaid from the wire at the locator-derived offset by
  // ZoneMgr::zonePlayer (the netstream parser's position drifts on the
  // post-patch wire), so reading via the struct field offsets here is
  // safe and produces the player's actual zone-in buff list.
  for (int buffnumber = 0; buffnumber < MAX_BUFFS; buffnumber++)
  {
    if (player.buffs[buffnumber].spellid > 0 &&
        player.buffs[buffnumber].spellid != int32_t(0xffffffff) &&
        player.buffs[buffnumber].duration > 0)
    {
      emit buffLoad(&player.buffs[buffnumber]);
    }
  }
}

void Player::player(const charProfileStruct* player)
{
  QString messag;

  if (m_name != player->name)
    emit newPlayer();

  // fill in base Spawn class
  // set the characteristics that probably haven't changed.
  setNPC(SPAWN_SELF);

  // save the raw name
  setTypeflag(1);

  Spawn::setName(player->name);
  setRealName(player->name);

  // if it's got a last name add it
  setLastName(player->lastName);

  // Load the profile
  loadProfile(player->profile);

  // Guild
  setGuildID(player->guildID);
  setGuildServerID(player->guildServerID);
  setGuildTag(m_guildMgr->guildIdToName(guildID(), guildServerID()));
  emit guildChanged();

  // Position
  setPos((int16_t)lrintf(player->x), 
         (int16_t)lrintf(player->y), 
         (int16_t)lrintf(player->z),
	 showeq_params->walkpathrecord,
	 showeq_params->walkpathlength
        );
  setDeltas(0,0,0);

  // Initial location when landing in new zone
#if 1
  seqDebug("Player::backfill(): Pos (%f/%f/%f) Heading: %f",
	   player->x, player->y, player->z, player->heading);
  setHeading((int8_t)lrintf(player->heading), 0);
  m_headingDegrees = 360 - ((((int8_t)lrintf(player->heading)) * 360) >> 11);
  m_validPos = true;
  emit headingChanged(m_headingDegrees);
  emit posChanged(x(), y(), z(), 
		  deltaX(), deltaY(), deltaZ(), m_headingDegrees);
#endif

  // Merge in our new languages...
  for (int a = 0; a < MAX_KNOWN_LANGS; a++)
  {
    m_playerLanguages[a] = player->languages[a];
    
    emit addLanguage (a, m_playerLanguages[a]);
  }

  const bool applyProgression =
      !m_progressionMutationGuard || m_progressionMutationGuard();
  if (applyProgression) {
    m_currentExp = player->exp;
    m_currentAltExp = player->expAA;
    m_validExp = true;
    emit expChangedInt (m_currentExp, m_minExp, m_maxExp);
    emit expAltChangedInt(m_currentAltExp, 0, 100000);
    emit setAltExp(m_currentAltExp, 100000, 100000/100, m_currentAApts);
    if (showeq_params->savePlayerState) savePlayerState();
  }

  emit changeItem(this, tSpawnChangedALL);
}

void Player::applyProfileSupplement(const charProfileStruct* player)
{
  if (!player) return;

  // Fields the Profile event doesn't carry stay host-applied.
  setGender(player->profile.gender);
  const bool applyProgression =
      !m_progressionMutationGuard || m_progressionMutationGuard();
  if (applyProgression) {
    for (int i = 0; i < MAX_KNOWN_SKILLS; ++i)
      m_playerSkills[i] = player->profile.skills[i];
    m_currentAAUnspent = player->profile.aa_unspent;
  }
  m_maxMana = calcMaxMana(m_maxINT, m_maxWIS, m_class, m_level) + m_plusMana;
  memcpy(&m_spellBookSlots[0], &player->profile.sSpellBook[0],
         sizeof(m_spellBookSlots));
  for (int i = 0; i < MAX_BUFFS; ++i) {
    const spellBuff& buff = player->profile.buffs[i];
    if (buff.spellid > 0 && buff.spellid != int32_t(0xffffffff) &&
        buff.duration > 0)
      emit buffLoad(&buff);
  }

  setGuildID(player->guildID);
  setGuildServerID(player->guildServerID);
  setGuildTag(m_guildMgr->guildIdToName(guildID(), guildServerID()));
  emit guildChanged();

  setPos((int16_t)lrintf(player->x), (int16_t)lrintf(player->y),
         (int16_t)lrintf(player->z), showeq_params->walkpathrecord,
         showeq_params->walkpathlength);
  setDeltas(0, 0, 0);
  setHeading((int8_t)lrintf(player->heading), 0);
  m_headingDegrees = 360 - ((((int8_t)lrintf(player->heading)) * 360) >> 11);
  m_validPos = true;
  emit headingChanged(m_headingDegrees);
  emit posChanged(x(), y(), z(), deltaX(), deltaY(), deltaZ(),
                  m_headingDegrees);

  for (int i = 0; i < MAX_KNOWN_LANGS; ++i) {
    m_playerLanguages[i] = player->languages[i];
    emit addLanguage(i, m_playerLanguages[i]);
  }
  if (applyProgression) {
    m_currentExp = player->exp;
    m_currentAltExp = player->expAA;
    m_validExp = true;
    emit expChangedInt(m_currentExp, m_minExp, m_maxExp);
    emit expAltChangedInt(m_currentAltExp, 0, 100000);
    emit setAltExp(m_currentAltExp, 100000, 1000, m_currentAApts);
    emit changeSkill(MAX_KNOWN_SKILLS - 1,
                     m_playerSkills[MAX_KNOWN_SKILLS - 1]);
    if (showeq_params->savePlayerState) savePlayerState();
  }
  emit changeItem(this, tSpawnChangedALL);
}

#if 0 // ZBTEMP
void Player::wearItem(const playerItemStruct* itemp)
{
  const itemItemStruct* item = &itemp->item;

  if (item->equipSlot < 22)
  {
    bool manaAdjust = false;

    if (item->STR != 0)
    {
      m_maxSTR += item->STR;
      emit statChanged (LIST_STR, m_maxSTR, m_maxSTR);
    }
    
    if (item->STA != 0)
    {
      m_maxSTA += item->STA;
      emit statChanged (LIST_STA, m_maxSTA, m_maxSTA);
    }

    if (item->CHA != 0)
    {
      m_maxCHA += item->CHA;
      emit statChanged (LIST_CHA, m_maxCHA, m_maxCHA);
    }

    if (item->DEX != 0)
    {
      m_maxDEX += item->DEX;
      emit statChanged (LIST_DEX, m_maxDEX, m_maxDEX);
    }

    if (item->INT != 0)
    {
      m_maxINT += item->INT;
      emit statChanged (LIST_INT, m_maxINT, m_maxINT);
      manaAdjust = true;
    }

    if (item->AGI != 0)
    {
      m_maxAGI += item->AGI;
      emit statChanged (LIST_AGI, m_maxAGI, m_maxAGI);
    }

    if (item->WIS != 0)
    {
      m_maxWIS += item->WIS;
      emit statChanged (LIST_WIS, m_maxWIS, m_maxWIS);
      manaAdjust = true;
    }

    m_plusHP += item->HP;

    if (item->MANA != 0)
    {
      m_plusMana += item->MANA;
      manaAdjust = true;
    }

    if (manaAdjust)
    {
      m_maxMana = calcMaxMana( m_maxINT,
			       m_maxWIS,
			       classVal(),
			       level()
			       )  +  m_plusMana;
      
      emit manaChanged(m_mana, m_maxMana);  /* Needs max mana */

      m_validMana = true;
    }

    if (showeq_params->savePlayerState)
      savePlayerState();
  }
}

void Player::removeItem(const itemItemStruct* item)
{
  if (item->equipSlot < 22)
  {
    bool manaAdjust = false;

    if (item->STR != 0)
    {
      m_maxSTR -= item->STR;
      emit statChanged (LIST_STR, m_maxSTR, m_maxSTR);
    }
    
    if (item->STA != 0)
    {
      m_maxSTA -= item->STA;
      emit statChanged (LIST_STA, m_maxSTA, m_maxSTA);
    }

    if (item->CHA != 0)
    {
      m_maxCHA -= item->CHA;
      emit statChanged (LIST_CHA, m_maxCHA, m_maxCHA);
    }

    if (item->DEX != 0)
    {
      m_maxDEX -= item->DEX;
      emit statChanged (LIST_DEX, m_maxDEX, m_maxDEX);
    }

    if (item->INT != 0)
    {
      m_maxINT -= item->INT;
      emit statChanged (LIST_INT, m_maxINT, m_maxINT);
      manaAdjust = true;
    }

    if (item->AGI != 0)
    {
      m_maxAGI -= item->AGI;
      emit statChanged (LIST_AGI, m_maxAGI, m_maxAGI);
    }

    if (item->WIS != 0)
    {
      m_maxWIS -= item->WIS;
      emit statChanged (LIST_WIS, m_maxWIS, m_maxWIS);
      manaAdjust = true;
    }

    m_plusHP -= item->HP;

    if (item->MANA != 0)
    {
      m_plusMana -= item->MANA;
      manaAdjust = true;
    }

    if (manaAdjust)
    {
      m_maxMana = calcMaxMana( m_maxINT,
			     m_maxWIS,
			     classVal(),
			     level()
			     )  +  m_plusMana;
    
      emit manaChanged(m_mana, m_maxMana);  /* Needs max mana */

      m_validMana = true;
    }

    if (showeq_params->savePlayerState)
      savePlayerState();
  }
}
#endif // ZBTEMP

void Player::increaseSkill(const uint8_t* data)
{
  auto out = seq::rust::decode_skill_update(
      rust::Slice<const uint8_t>{data, sizeof(skillIncStruct)});
  if (!out.ok) return;
  // save the new skill value
  m_playerSkills[out.skill_id] = out.value;

  // notify others of the new value
  emit changeSkill (out.skill_id, out.value);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::manaChange(const uint8_t* data)
{
  auto out = seq::rust::decode_mana_change(
      rust::Slice<const uint8_t>{data, sizeof(manaDecrementStruct)});
  if (!out.ok) return;
  // update the players mana
  m_mana = out.new_mana;
  // EQ never sends max mana; the peak of current mana is the exact max once
  // the player regens to full.
  if (m_mana > m_observedMaxMana) m_observedMaxMana = m_mana;

  m_validMana = true;

  // notify others of this change
  emit manaChanged(m_mana, m_maxMana);  // Needs max mana

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::setVitals(const Vitals& v)
{
  // Neutral primitive: apply one wire packet's self vitals from already-decoded
  // cur/max values. Used by the eql 0x2735 stat-sync channel, which multiplexes
  // HP/mana/endurance into a single packet.
  //
  // Deliberately does NOT emit changeItem(tSpawnChangedHP): the player's vitals
  // reach the client via player_stats, and the self spawn record can't express
  // them anyway (SpawnUpdated carries hp_cur but no hp_max, and the self record
  // is added with no HP at all — so a spawn-sourced ratio is meaningless).
  if (!v.haveHP && !v.haveMana && !v.haveEnd)
    return;

  if (v.haveHP)
  {
    m_curHP = v.hpCur;
    m_maxHP = v.hpMax;
    m_validHP = true;
    updateLastChanged();
  }

  if (v.haveMana)
  {
    m_mana = v.manaCur;
    m_maxMana = v.manaMax;
    m_wireMaxMana = v.manaMax;   // authoritative eql wire max — calcMaxMana can't clobber it
    m_validMana = true;
  }

  if (v.haveEnd)
  {
    m_enduranceCur = v.endCur;
    m_enduranceMax = v.endMax;
  }

  emit vitalsChanged();

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::updateAltExp(const uint8_t* data)
{
  const altExpUpdateStruct* altexp = (const altExpUpdateStruct*)data;

  // Confirmed via aa_progress.vpk + aa_point.vpk + aa_slider50.vpk:
  //   altexp->altexp    — 0..100000 per-AA-point scale (sub-percent
  //                       precision; in-game display % is altexp/1000).
  //   altexp->aapoints  — UNSPENT count. Legacy code wrote this into
  //                       m_currentAApts (= "spent" tracker), corrupting
  //                       it on every tick. Route to m_currentAAUnspent
  //                       so spent/unspent stay independent.
  //   altexp->percent   — the player's "% of exp to AA" slider (uint8
  //                       0..100). Fires its own OP_AAExpUpdate every
  //                       time the slider moves; otherwise constant.
  uint32_t scaledAltExp = altexp->altexp;
  uint32_t expIncrement = (scaledAltExp > m_currentAltExp)
                            ? scaledAltExp - m_currentAltExp
                            : 0;

  // OP_AAExpUpdate carries unspent only — aa_spent waits for the next
  // OP_PlayerProfile (zone-in). Mirror the in-client UI by deriving a
  // spend-delta from any decrease in unspent: each click in the AA
  // window decrements aapoints and bumps the spent counter by the same
  // amount. Multi-rank buys (e.g. Mend Pet rank 5 in a single click)
  // collapse into one tick with the full delta — handled here too.
  // Increases in unspent (AA-point ding via altexp wrap, or /resetaas
  // refund) are intentionally NOT subtracted from spent: charProfile
  // is the ground truth that reconciles those on the next zone-in.
  if (altexp->aapoints < m_currentAAUnspent) {
    m_currentAApts += m_currentAAUnspent - altexp->aapoints;
  }
  m_currentAAUnspent = altexp->aapoints;
  m_currentAltExp = scaledAltExp;

  emit expAltChangedInt(m_currentAltExp, 0, 100000);

  emit newAltExp(expIncrement, m_currentAltExp, altexp->altexp,
		 100000, 100000/100, m_currentAApts);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::updateExp(const uint8_t* data)
{
  auto out = seq::rust::decode_exp_update(
      rust::Slice<const uint8_t>{data, sizeof(expUpdateStruct)});
  if (!out.ok) return;

  uint32_t realExp = (m_tickExp * out.exp) + m_minExp;
  uint32_t expIncrement = (realExp > m_currentExp) ? realExp - m_currentExp : 0;

  // Save whether exp was already seeded before marking valid — defense-in-depth
  // for the group-kill path below, in case a type=0 slips through.
  bool hadValidExp = m_validExp;
  m_currentExp = realExp;
  m_validExp = true;

  // signal the new experience
  emit newExp(expIncrement, realExp, out.exp,
	      m_minExp, m_maxExp, m_tickExp);

  emit expChangedInt (realExp, m_minExp, m_maxExp);

  // type=0 is a zone-in baseline delivery ("set"), not an actual exp gain.
  // Update state and display signals above, but do not log it as experience earned.
  if (out.kind == 0)
  {
    emit setExp(m_currentExp, out.exp, m_minExp, m_maxExp, m_tickExp);
    if (showeq_params->savePlayerState)
      savePlayerState();
    return;
  }

  if(m_freshKill)
  {
     emit expGained( m_lastSpawnKilledName,
                     m_lastSpawnKilledLevel,
                     expIncrement,
                     m_zoneMgr->longZoneName());

     // have gained experience for the kill, it's no longer fresh
     m_freshKill = false;
  }
  else
  {
     emit setExp(m_currentExp, out.exp, m_minExp, m_maxExp, m_tickExp);
     // Group kill — another member landed the killing blow so m_freshKill
     // was never set, but XP still arrived. Emit with empty attribution
     // so the exp log records every gain. hadValidExp guard keeps any
     // residual pre-profile delivery from leaking through.
     if (expIncrement > 0 && hadValidExp)
        emit expGained( QString(),
                        0,
                        expIncrement,
                        m_zoneMgr->longZoneName());
  }
//     emit expGained( "Unknown", // Randomly blessed with xp?
//                     0, // don't know what gave it so, level 0
//		     expIncrement,
//		     m_zoneMgr->longZoneName());

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::updateLevel(const uint8_t* data)
{
  auto out = seq::rust::decode_level_update(
      rust::Slice<const uint8_t>{data, sizeof(levelUpUpdateStruct)});
  if (!out.ok) return;

  // cache previous experience for later calculations
  uint32_t prevExp = m_currentExp;

  // save the new level information
  m_level  = out.level;

  // Per-level 0-100000 scale, identical at every level.
  m_minExp = 0;
  m_maxExp = 100000;
  m_tickExp = 1;
  m_currentExp = out.exp;
  m_validExp = true;

  // calculate the increment in experience between the current experience and
  // the previous experience
  uint32_t expIncrement =  m_currentExp - prevExp;

  emit newExp(expIncrement, m_currentExp, out.exp,
	      m_minExp, m_maxExp, m_tickExp);

  if(m_freshKill)
  {
     emit expGained( m_lastSpawnKilledName,
                     m_lastSpawnKilledLevel,
                     expIncrement,
                     m_zoneMgr->longZoneName());
      
     // have gained experience for the kill, it's no longer fresh
     m_freshKill = false;
  }
  else
     emit expGained( "Unknown", // Randomly blessed with xp?
                     0, // don't know what gave it so, level 0
		     expIncrement,
		     m_zoneMgr->longZoneName());

  emit expChangedInt( m_currentExp, m_minExp, m_maxExp);

  if (showeq_params->savePlayerState)
    savePlayerState();

  updateLastChanged();

  // signal that the level changed
  emit levelChanged(m_level);
  emit changeItem(this, tSpawnChangedLevel);
}

void Player::applyLevel(uint8_t newLevel)
{
  // Neutral level-set for backends without a discrete level packet (eql derives
  // a ding from the exp bar wrapping — see EqlDispatch::expUpdate). No exp
  // bookkeeping here: the caller still runs the normal exp path (updateExp), so
  // this only moves the level and mirrors updateLevel()'s notification tail.
  if (newLevel == 0 || newLevel == m_level)
    return;

  m_level = newLevel;

  updateLastChanged();

  // signal that the level changed
  emit levelChanged(m_level);
  emit changeItem(this, tSpawnChangedLevel);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::updateNpcHP(const uint8_t* data)
{
  auto out = seq::rust::decode_hp_update(
      rust::Slice<const uint8_t>{data, sizeof(hpNpcUpdateStruct)});
  if (!out.ok) return;

  if (out.spawn_id != id())
    return;

  m_curHP = out.cur_hp;
  m_maxHP = out.max_hp;

  m_validHP = true;

  updateLastChanged();

  emit changeItem(this, tSpawnChangedHP);

  emit hpChanged(m_curHP, m_maxHP);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::updateSpawnInfo(const uint8_t* data)
{
  auto out = seq::rust::decode_wear_change(
      rust::Slice<const uint8_t>{data, sizeof(SpawnUpdateStruct)});
  if (!out.ok) return;
  if (out.spawn_id != id())
    return;

  if (out.subcommand != 17)
    return;

  m_curHP = out.arg1;

  m_validHP = true;

  updateLastChanged();

  emit changeItem(this, tSpawnChangedHP);

  emit hpChanged(m_curHP, m_maxHP);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::updateStamina(const uint8_t* data)
{
  auto out = seq::rust::decode_stamina(
      rust::Slice<const uint8_t>{data, sizeof(staminaStruct)});
  if (!out.ok) return;
  m_food = out.food;
  m_water = out.water;
  m_validStam = true;

  emit stamChanged(m_food, 127, m_water, 127);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::setMoneyCoins(uint32_t plat, uint32_t gold, uint32_t silver,
                                 uint32_t copper)
{
  // Ceiling is ~4.29M platinum (u32 copper); the proto field is u32 too, so
  // widening the whole chain buys nothing until that field grows.
  const uint64_t total = (uint64_t)plat * 1000 + (uint64_t)gold * 100 +
                         (uint64_t)silver * 10 + copper;
  const uint32_t clamped =
      total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
  if (clamped == m_money)
    return;
  m_money = clamped;
  emit moneyChanged(m_money);
}

void Player::adjustMoney(int64_t deltaCopper)
{
  if (deltaCopper == 0)
    return;
  const int64_t next = (int64_t)m_money + deltaCopper;
  const uint32_t clamped =
      next <= 0 ? 0
                : (next > (int64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)next);
  if (clamped == m_money)
    return;
  m_money = clamped;
  emit moneyChanged(m_money);
}

void Player::updateEndurance(const uint8_t* data)
{
  auto out = seq::rust::decode_end_update(
      rust::Slice<const uint8_t>{data, sizeof(endUpdateStruct)});
  if (!out.ok) return;
  m_enduranceCur = out.cur;
  m_enduranceMax = out.max;

  emit endChanged(m_enduranceCur, m_enduranceMax);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::setLastKill(const QString& name, int level)
{
  // note the last spawn this player killed
  m_lastSpawnKilledName = name;
  m_lastSpawnKilledLevel = level;
  m_freshKill = true;
}

void Player::zoneChanged()
{
  reset();
  clear();
}

// spawnStruct.curHp is a 0-100 percentage and Spawn::update hardcodes
// maxHP=100; for the local PC, HP/maxHP are authoritative from
// OP_HPUpdate (raw values). Snapshot and restore so a spawnStruct (e.g.
// the duplicate zoneEntry packets fired after zoning) can't overwrite
// a real raw HP read with a percentage.
void Player::update(const spawnStruct* s)
{
  const int32_t savedCurHP = HP();
  const int32_t savedMaxHP = maxHP();
  Spawn::update(s);
  setHP(savedCurHP);
  setMaxHP(savedMaxHP);
}

void Player::playerUpdateSelf(const uint8_t* data, size_t len, uint8_t dir)
{
  if (len != sizeof(playerSelfPosStruct)) return;
  auto out = seq::rust::decode_player_self_pos(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;

  if ((dir != DIR_Client) && (out.spawn_id != id()))
    return;

  if (dir == DIR_Client && id() == 0)
      setPlayerID(out.spawn_id);
  // When casting Eye of Zomm, using a row boat, or doing something else
  // where you're controlling another spawn, the client will send multiple
  // update packets, one for with your player spawn ID, and the other
  // with the ID of the other object.  So we can't assume that if the
  // id changes, we can automatically update the player's ID like we used to.
  // Instead, we'll pass the other ID to spawnshell for handling
  if (out.spawn_id != id())
  {
      emit playerUpdate(data, len, dir);
      return;
  }

  int16_t py = int16_t(out.y);
  int16_t px = int16_t(out.x);
  int16_t pz = int16_t(out.z);
  int16_t pdeltaX = int16_t(out.delta_x);
  int16_t pdeltaY = int16_t(out.delta_y);
  int16_t pdeltaZ = int16_t(out.delta_z);

#if 0
  // Dump position updates for debugging client update changes
  for (int i=0; i<sizeof(playerSelfPosStruct); i++)
  {
      printf("%.2x", data[i]);

      if ((i+1) % 8 == 0)
      {
          printf("    ");
      }
      else
      {
          printf(" ");
      }

  }
  printf("\n");
#endif

#if 0
    // Debug positioning without having to recompile everything...
#pragma pack(1)
struct pos
{
	/*0000*/ uint16_t unknown0000;                   // ***Placeholder (update time counter?)
	/*0002*/ uint16_t spawnId;                       // Player's spawn id
	/*0004*/ uint16_t unknown0001;                   // ***Placeholder
	/*0006*/ unsigned pitch:12;                      // pitch (up/down heading)
	 	 unsigned padding01:20;
	/*0010*/ float deltaX;                           // Change in x
	/*0014*/ float deltaZ;                           // Change in z
	/*0018*/ float x;                                // x coord (1st loc value)
	/*0022*/ float y;                                // y coord (2nd loc value)
	/*0026*/ signed animation:10;                    // velocity
		 unsigned padding02:22;
	/*0030*/ float z;                                // z coord (3rd loc value)
	/*0034*/ float deltaY;                           // Change in y
	/*0038*/ signed deltaHeading:10;                 // change in heading
                 unsigned heading:12;                    // Heading
		 unsigned padding03:10;
	/*0042*/ 
};
#endif

#if 0
#pragma pack(0)
    struct pos *p = (struct pos *)data;
    printf("[%.2x](%f, %f, %f), dx %f dy %f dz %f head %d dhead %d anim %d pitch %d (%x, %x, %x, %x, %x)\n",
            p->spawnId, p->x, p->y, p->z,
            p->deltaX, p->deltaY, p->deltaZ,
            p->heading, p->deltaHeading,
            p->animation, p->pitch,
            p->padding01, p->padding02, p->padding03 );
#endif

  setPos(px, py, pz, showeq_params->walkpathrecord, showeq_params->walkpathlength);
  setDeltas(pdeltaX, pdeltaY, pdeltaZ);
  setHeading(out.heading, out.delta_heading);
  m_validPos = true;
  updateLast();

  m_headingDegrees = 360 - ((out.heading * 360) >> 11);
  emit headingChanged(m_headingDegrees);

  emit posChanged(x(), y(), z(),
		  deltaX(), deltaY(), deltaZ(), m_headingDegrees);

  updateLastChanged();
  emit changeItem(this, tSpawnChangedPosition);

  emit newSpeed(hypot( hypot( (out.delta_x*80),
					 (out.delta_y*80)),
                              (out.delta_z*80))/119.46664);

  static uint8_t count = 0;

  // only save player position every 64 updates
  if (showeq_params->savePlayerState)
  {
    count++;
    if (count % 64)
      savePlayerState();
  }
}

// Target-neutral primitives for the eql backend (EqlDispatch). These own the
// private-state writes + signal emissions that a backend-external QObject can't
// perform. No Legends wire types appear here — the caller decodes those.

void Player::setIdentity(uint16_t race, uint8_t classVal, uint8_t level)
{
  setNPC(SPAWN_SELF);
  // A real identity just arrived — stop returning default race/class/level.
  // reset() (fired on zone-change) sets useDefaults=true; without clearing it
  // here the accessors keep returning m_defaultRace/Class/Level (symptom: the
  // player shows default race1/class1/level1 despite a decoded profile).
  setUseDefaults(false);
  setRace(race);
  setClassVal(classVal);
  setLevel(level);

  emit levelChanged(m_level);
  updateLastChanged();
  emit changeItem(this, tSpawnChangedALL);
}

void Player::applyLifecycleIdentity(const QString& name,
                                    const QString& lastName,
                                    uint16_t race, uint8_t classVal,
                                    uint8_t level, uint16_t deity,
                                    uint32_t classMask)
{
  if (!name.isEmpty()) setPlayerName(name);
  setLastName(lastName);
  setDeity(deity);
  setIdentity(race, classVal, level);
  setClassMask(classMask);
}

void Player::applyPlayerIdentity(std::optional<uint32_t> spawnId,
                                 const QString& name,
                                 const QString& lastName, uint16_t race,
                                 uint8_t classVal, uint16_t deity,
                                 uint8_t level, uint32_t classMask)
{
  if (!name.isEmpty()) setPlayerName(name);
  setLastName(lastName);
  setDeity(deity);
  setIdentity(race, classVal, level);
  setClassMask(classMask);
  if (spawnId && *spawnId <= UINT16_MAX)
    setPlayerID(uint16_t(*spawnId));
}

void Player::applyPlayerMovement(std::optional<uint32_t> spawnId,
                                 int32_t px, int32_t py, int32_t pz,
                                 uint16_t headingDegrees)
{
  if (spawnId && *spawnId <= UINT16_MAX && id() != uint16_t(*spawnId))
    setPlayerID(uint16_t(*spawnId));
  setPos(int16_t(std::clamp(px, int32_t(INT16_MIN), int32_t(INT16_MAX))),
         int16_t(std::clamp(py, int32_t(INT16_MIN), int32_t(INT16_MAX))),
         int16_t(std::clamp(pz, int32_t(INT16_MIN), int32_t(INT16_MAX))),
         showeq_params->walkpathrecord, showeq_params->walkpathlength);
  setDeltas(0, 0, 0);
  const uint16_t normalized = headingDegrees % 360;
  setHeading(int8_t(((360 - normalized) % 360) * 256 / 360), 0);
  m_headingDegrees = normalized;
  m_validPos = true;
  updateLast();
  emit headingChanged(m_headingDegrees);
  emit posChanged(x(), y(), z(), deltaX(), deltaY(), deltaZ(),
                  m_headingDegrees);
  updateLastChanged();
  emit changeItem(this, tSpawnChangedPosition);
}

void Player::applyPlayerVitals(
    bool hasHealth, int32_t healthCurrent,
    std::optional<int32_t> healthMaximum, bool hasMana, int32_t manaCurrent,
    std::optional<int32_t> manaMaximum, bool hasEndurance,
    int32_t enduranceCurrent, std::optional<int32_t> enduranceMaximum)
{
  if (!hasHealth && !hasMana && !hasEndurance) return;
  if (hasHealth) {
    m_curHP = uint32_t(std::max(healthCurrent, 0));
    if (healthMaximum)
      m_maxHP = uint32_t(std::max(*healthMaximum, 0));
    m_validHP = true;
    updateLastChanged();
  }
  if (hasMana) {
    m_mana = uint16_t(std::clamp(manaCurrent, 0, int32_t(UINT16_MAX)));
    if (manaMaximum) {
      m_maxMana = uint16_t(std::clamp(*manaMaximum, 0,
                                      int32_t(UINT16_MAX)));
      m_wireMaxMana = m_maxMana;
    }
    if (m_mana > m_observedMaxMana) m_observedMaxMana = m_mana;
    m_validMana = true;
  }
  if (hasEndurance) {
    m_enduranceCur = uint32_t(std::max(enduranceCurrent, 0));
    if (enduranceMaximum)
      m_enduranceMax = uint32_t(std::max(*enduranceMaximum, 0));
  }
  emit vitalsChanged();
  if (showeq_params->savePlayerState) savePlayerState();
}

void Player::applyPlayerAppearance(std::optional<uint32_t> race,
                                   std::optional<uint8_t> gender,
                                   std::optional<uint32_t> animation)
{
  if (race) setRace(uint16_t(*race));
  if (gender) setGender(*gender);
  if (animation) setAnimation(uint8_t(*animation));
  updateLastChanged();
  emit levelChanged(m_level);
  emit changeItem(this, animation ? tSpawnChangedPosition
                                  : tSpawnChangedALL);
}

void Player::applyPlayerDeath()
{
  setPlayerID(0);
}

void Player::setPlayerName(const QString& name)
{
  // eql: the authoritative character name off OP_PlayerProfile (located by the
  // Rust parser's anchor-scan of the name/surname block, not a fixed offset).
  // Live/test set the name through the charProfileStruct path; this neutral
  // primitive lets the eql backend name the player without a Legends type in
  // core. It only stores the name — the caller's setIdentity() emits the
  // changeItem(tSpawnChangedALL) that carries it to the frontend — and signals
  // DaemonApp to promote/merge the box under this name in the picker.
  setName(name);
  // Also set realName: SpawnShell::zoneEntry adopts the self-spawn id by matching
  // spawn->name against m_player->realName(). On Live that field is populated from
  // the charProfileStruct path (setRealName(player->name)); eql only reaches this
  // neutral primitive, so without this the name-match never fires and the self-id
  // (hence the self marker + C>S self-position) is never adopted.
  setRealName(name);
  emit identityNameResolved(name);
}

void Player::seedSkills(const std::vector<uint32_t>& skills)
{
  // eql: the player's whole skill array arrives in OP_PlayerProfile (walked in
  // seq-backend-eql). Seed it in bulk so the Skills window populates at zone-in
  // rather than one skill at a time via OP_SkillUpdate. Write silently — do NOT
  // emit per-skill (that would fire onPlayerStatsChanged ~100x, re-serializing
  // partial snapshots, the exact spam SessionAdapter warns about) — then emit a
  // SINGLE changeSkill so the full set rides one coalesced PlayerStats snapshot.
  // The web's skill-up log ignores first-appearances (old==undefined||0), so a
  // whole-array seed synthesizes no bogus "0->N" entries.
  const size_t n = std::min(skills.size(), (size_t)MAX_KNOWN_SKILLS);
  for (size_t i = 0; i < n; ++i)
    m_playerSkills[i] = skills[i];

  if (n)
    emit changeSkill((int)(n - 1), (int)m_playerSkills[n - 1]);

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::seedPurchasedAA(const std::vector<uint32_t>& ids,
                             const std::vector<uint32_t>& values, uint32_t spent)
{
  // eql: the purchased-AA list arrives as parallel id/value arrays in
  // OP_PlayerProfile (walked in seq-backend-eql), mirroring Live's aa_array.
  // value is the points spent on the ability; filter empty slots (id==0) and
  // auto-grants (value==0), exactly like loadProfile. Names come over
  // OP_SendAATable (unresolved) — clients show #<id> until then. No signal
  // here: EqlDispatch::profile calls this before setIdentity, whose
  // levelChanged drives the one coalesced PlayerStats snapshot that carries
  // purchased_aa + aa_points.
  m_currentAApts = (uint16_t)spent;
  m_purchasedAA.clear();
  const size_t n = std::min(ids.size(), values.size());
  for (size_t i = 0; i < n; ++i) {
    if (ids[i] == 0 || values[i] == 0) continue;
    m_purchasedAA.push_back({ids[i], values[i]});
  }

  if (showeq_params->savePlayerState)
    savePlayerState();
}

void Player::replaceSkills(
    const std::vector<std::pair<uint32_t, uint32_t>>& skills)
{
  std::fill(std::begin(m_playerSkills), std::end(m_playerSkills), 0xFFFFFFFFu);
  int last = -1;
  for (const auto& [id, value] : skills) {
    if (id >= MAX_KNOWN_SKILLS) continue;
    m_playerSkills[id] = value;
    last = std::max(last, int(id));
  }
  if (last >= 0) emit changeSkill(last, int(m_playerSkills[last]));
}

void Player::applySkillValue(uint32_t skillId, uint32_t value)
{
  if (skillId >= MAX_KNOWN_SKILLS || m_playerSkills[skillId] == value) return;
  m_playerSkills[skillId] = value;
  emit changeSkill(int(skillId), int(value));
}

void Player::applyExperienceProgress(
    uint32_t experience, std::optional<uint32_t> newLevel,
    std::optional<uint32_t> /*previousLevel*/)
{
  const uint8_t levelValue = newLevel
      ? uint8_t(std::min<uint32_t>(*newLevel, UINT8_MAX)) : m_level;
  const bool levelChangedValue = newLevel && levelValue != m_level;
  m_level = levelValue;
  m_minExp = 0;
  m_maxExp = 100000;
  m_tickExp = 1;
  m_currentExp = experience;
  m_validExp = true;
  emit expChangedInt(m_currentExp, m_minExp, m_maxExp);
  if (levelChangedValue) {
    updateLastChanged();
    emit levelChanged(m_level);
    emit changeItem(this, tSpawnChangedLevel);
  }
}

void Player::replaceAlternateAdvancement(
    const std::vector<std::pair<uint32_t, uint32_t>>& purchased,
    std::optional<uint32_t> spent, uint32_t unspent, uint32_t experience)
{
  m_purchasedAA.clear();
  for (const auto& [abilityId, rank] : purchased) {
    if (abilityId != 0 && rank != 0)
      m_purchasedAA.push_back({abilityId, rank});
  }
  if (spent)
    m_currentAApts = uint16_t(std::min<uint32_t>(*spent, UINT16_MAX));
  m_currentAAUnspent = unspent;
  m_currentAltExp = experience;
  emit expAltChangedInt(m_currentAltExp, 0, 100000);
}

void Player::applyAlternateAdvancement(uint32_t experience, uint32_t unspent)
{
  m_currentAltExp = experience;
  m_currentAAUnspent = unspent;
  emit expAltChangedInt(m_currentAltExp, 0, 100000);
}

void Player::seedBaseStats(uint16_t str, uint16_t sta, uint16_t cha,
                           uint16_t dex, uint16_t intel, uint16_t agi,
                           uint16_t wis, bool persist)
{
  // Assignment, not +=: the profile re-fires per zone-in and would stack the
  // roll. No signal — EqlDispatch::profile calls this before setIdentity,
  // whose levelChanged drives the one coalesced snapshot (cf. seedPurchasedAA).
  m_maxSTR = str;
  m_maxSTA = sta;
  m_maxCHA = cha;
  m_maxDEX = dex;
  m_maxINT = intel;
  m_maxAGI = agi;
  m_maxWIS = wis;
  m_validAttributes = true;

  if (persist && showeq_params->savePlayerState)
    savePlayerState();
}

void Player::applySelfPosition(int16_t px, int16_t py, int16_t pz,
                               int16_t pdeltaX, int16_t pdeltaY, int16_t pdeltaZ,
                               uint16_t heading, float speed)
{
  setPos(px, py, pz, showeq_params->walkpathrecord, showeq_params->walkpathlength);
  setDeltas(pdeltaX, pdeltaY, pdeltaZ);
  m_validPos = true;
  updateLast();

  // heading is the 11-bit wire facing (2048 per circle); m_heading is an 8-bit
  // compass, so scale down (>>3). This said >>5 until 2026-07-29, left over from
  // when the field was read as 13 bits — it squeezed a full turn into the low
  // quarter of the 8-bit range. Harmless in practice (protoencoder takes the
  // Player's headingDegrees below, never this), but the two must agree.
  setHeading((int8_t)((heading >> 3) & 0xFF), 0);
  // 11-bit facing (2048 per circle), inverted like every other heading —
  // matching upstream legends' `360 - ((h2048 * 360) >> 11)`. Uninverted it
  // mirrors: a left turn rotates the marker right.
  m_headingDegrees = (360 - ((heading * 360) >> 11)) % 360;
  emit headingChanged(m_headingDegrees);

  emit posChanged(x(), y(), z(),
		  deltaX(), deltaY(), deltaZ(), m_headingDegrees);

  updateLastChanged();
  emit changeItem(this, tSpawnChangedPosition);

  emit newSpeed(speed);

  static uint8_t count = 0;

  // only save player position every 64 updates
  if (showeq_params->savePlayerState)
  {
    count++;
    if (count % 64)
      savePlayerState();
  }
}

void Player::consMessage(const uint8_t* data, size_t, uint8_t dir)
{
  if (dir == DIR_Client)
    return;

  const considerStruct * con = (const considerStruct*)data;

  if (con->playerid == con->targetid) 
    setPlayerID(con->playerid);
}

void Player::tradeSpellBookSlots(const uint8_t* data, size_t, uint8_t dir)
{
  const tradeSpellBookSlotsStruct* tsb = (const tradeSpellBookSlotsStruct*)data;

  seqDebug("tradeSpellBookSlots(dir=%d): Swapping %d (%04x) with %d (%04x)",
	  dir,
	  tsb->slot1, m_spellBookSlots[tsb->slot1],
	  tsb->slot2, m_spellBookSlots[tsb->slot2]);

  if (dir != DIR_Server)
    return;

  uint32_t spell1 = m_spellBookSlots[tsb->slot1];
  m_spellBookSlots[tsb->slot1] = m_spellBookSlots[tsb->slot2];
  m_spellBookSlots[tsb->slot2] = spell1;
}

// setPlayer* only updates player*.  If you want to change and use the defaults you
// must change showeq_params->default* and emit an checkDefaults signal.
void Player::setPlayerID(uint16_t playerID)
{
  if (id() != playerID)
  {
     uint16_t old_id = id();
     seqInfo("Your player's id is %i", playerID);
     setID(playerID);
     emit changedID(old_id, id());
     updateLastChanged();
     emit changeItem(this, tSpawnChangedALL);
  }
}

bool Player::getStatValue(uint8_t stat,
			    uint32_t& curValue, 
			    uint32_t& maxValue)
{
  bool valid = false;
  curValue = 0;
  maxValue = 0;

  switch (stat)
  {
  case LIST_HP:
    curValue = m_curHP;
    maxValue = m_maxHP;
    valid = m_validHP;
    break;
  case LIST_MANA:
    curValue = m_mana;
    maxValue = m_maxMana;
    valid = m_validMana;
    break;
  case LIST_STAM:
    curValue = 100 - m_fatigue;
    maxValue = 100;
    valid = m_validStam;
    break;
  case LIST_EXP:
    curValue = m_currentExp;
    maxValue = m_maxExp;
    valid = m_validExp;
    break;
  case LIST_FOOD:
    curValue = m_food;
    maxValue = 127;
    valid = m_validStam;
    break;
  case LIST_WATR:
    curValue = m_water;
    maxValue = 127;
    valid = m_validStam;
    break;
  case LIST_STR:
    curValue = m_maxSTR;
    maxValue = m_maxSTR;
    valid = m_validAttributes;
    break;
  case LIST_STA:
    curValue = m_maxSTA;
    maxValue = m_maxSTA;
    valid = m_validAttributes;
    break;
  case LIST_CHA:
    curValue = m_maxCHA;
    maxValue = m_maxCHA;
    valid = m_validAttributes;
    break;
  case LIST_DEX:
    curValue = m_maxDEX;
    maxValue = m_maxDEX;
    valid = m_validAttributes;
    break;
  case LIST_INT:
    curValue = m_maxINT;
    maxValue = m_maxINT;
    valid = m_validAttributes;
    break;
  case LIST_AGI:
    curValue = m_maxAGI;
    maxValue = m_maxAGI;
    valid = m_validAttributes;
    break;
  case LIST_WIS:
    curValue = m_maxWIS;
    maxValue = m_maxWIS;
    valid = m_validAttributes;
    break;
  case LIST_MR:
  case LIST_FR:
  case LIST_CR:
  case LIST_DR:
  case LIST_PR:
    // don't know how to get these values
    break;
  default:
    break;
  };

  return valid;
}


QString Player::name() const
{
  return (!m_useAutoDetectedSettings || m_useDefaults ?
	m_defaultName : m_name);
}

QString Player::lastName() const
{
  return (!m_useAutoDetectedSettings || m_useDefaults ?
	m_defaultLastName : m_lastName);
}

uint16_t Player::deity() const 
{ 
  return ((!m_useAutoDetectedSettings || m_useDefaults) ? 
	  m_defaultDeity : m_deity); 
}

int Player::level() const
{ 
  return (!m_useAutoDetectedSettings || m_useDefaults ? 
	  m_defaultLevel : m_level);
}

uint16_t Player::race() const
{
  return ((!m_useAutoDetectedSettings || m_useDefaults) ? 
	  m_defaultRace : m_race);
}

uint8_t Player::classVal() const
{
  return ((!m_useAutoDetectedSettings || m_useDefaults) ? 
	  m_defaultClass : m_class);
}

void Player::savePlayerState(void)
{
  QFile keyFile(showeq_params->saveRestoreBaseFilename + "Player.dat");
  if (keyFile.open(QIODevice::WriteOnly))
  {
    QDataStream d(&keyFile);

    int i;

    // write the magic string
    d << *magic;

    // write a test value at the top of the file for a validity check
    uint32_t testVal = sizeof(charProfileStruct);
    d << testVal;
    d << MAX_KNOWN_SKILLS;
    d << MAX_KNOWN_LANGS;

    d << m_zoneMgr->shortZoneName().toLower();

    // write out the rest
    d << m_name;
    d << m_lastName;
    d << m_level;
    d << m_class;
    d << m_deity;
    d << m_ID;
    d << m_x;
    d << m_y;
    d << m_z;
    d << m_deltaX;
    d << m_deltaY;
    d << m_deltaZ;
    d << m_heading;
    d << m_headingDegrees;

    for (i = 0; i < MAX_KNOWN_SKILLS; ++i)
      d << m_playerSkills[i];

    for (i = 0; i < MAX_KNOWN_LANGS; ++i)
      d << m_playerLanguages[i];

    d << m_plusMana;
    d << m_plusHP;
    d << m_curHP;

    d << m_maxMana;
    d << m_maxSTR;
    d << m_maxSTA;
    d << m_maxCHA;
    d << m_maxDEX;
    d << m_maxINT;
    d << m_maxAGI;
    d << m_maxWIS;
    d << m_maxHP;

    d << m_food;
    d << m_water;
    d << m_fatigue;

    d << m_currentAltExp;
    d << m_currentAApts;
    d << m_currentExp;
    d << m_maxExp;

    uint8_t flags = 0;
    if (m_validStam)
      flags |= 0x01;
    if (m_validMana)
      flags |= 0x02;
    if (m_validHP)
      flags |= 0x04;
    if (m_validExp)
      flags |= 0x08;
    if (m_validAttributes)
      flags |= 0x10;
    if (m_useDefaults)
      flags |= 0x20;

    d << flags;
  }
}

void Player::restorePlayerState(void)
{
  QString fileName = showeq_params->saveRestoreBaseFilename + "Player.dat";
  QFile keyFile(fileName);
  if (keyFile.open(QIODevice::ReadOnly))
  {
    int i;
    uint32_t testVal;
    QDataStream d(&keyFile);

    // check the magic string
    uint32_t magicTest;
    d >> magicTest;

    if (magicTest != *magic)
    {
      seqWarn("Failure loading %s: Bad magic string!",
              fileName.toLatin1().data());
      reset();
      clear();
      return;
    }

    // check the test value at the top of the file
    d >> testVal;
    if (testVal != sizeof(charProfileStruct))
    {
      seqWarn("Failure loading %s: Bad player size!",
              fileName.toLatin1().data());
      reset();
      clear();
      return;
    }

    d >> testVal;
    if (testVal != MAX_KNOWN_SKILLS)
    {
      seqWarn("Failure loading %s: Bad known skills!",
              fileName.toLatin1().data());
      reset();
      clear();
      return;
    }

    d >> testVal;
    if (testVal != MAX_KNOWN_LANGS)
    {
      seqWarn("Failure loading %s: Bad known langs!",
              fileName.toLatin1().data());
      reset();
      clear();
      return;
    }

    // attempt to validate that the info is from the current zone
    QString zoneShortName;
    d >> zoneShortName;
    if (zoneShortName != m_zoneMgr->shortZoneName().toLower())
    {
      seqWarn("\aWARNING: Restoring player state for potentially incorrect zone (%s != %s)!",
              zoneShortName.toLatin1().data(),
              m_zoneMgr->shortZoneName().toLower().toLatin1().data());
    }

    // read in the rest
    d >> m_name;
    d >> m_lastName;
    d >> m_level;
    d >> m_class;
    d >> m_deity;
    d >> m_ID;
    d >> m_x;
    d >> m_y;
    d >> m_z;
    d >> m_deltaX;
    d >> m_deltaY;
    d >> m_deltaZ;
    d >> m_heading;
    d >> m_headingDegrees;

    for (i = 0; i < MAX_KNOWN_SKILLS; ++i)
      d >> m_playerSkills[i];

    for (i = 0; i < MAX_KNOWN_LANGS; ++i)
      d >> m_playerLanguages[i];

    d >> m_plusMana;
    d >> m_plusHP;
    d >> m_curHP;

    d >> m_maxMana;
    d >> m_maxSTR;
    d >> m_maxSTA;
    d >> m_maxCHA;
    d >> m_maxDEX;
    d >> m_maxINT;
    d >> m_maxAGI;
    d >> m_maxWIS;
    d >> m_maxHP;

    d >> m_food;
    d >> m_water;
    d >> m_fatigue;

    d >> m_currentAltExp;
    d >> m_currentAApts;
    d >> m_currentExp;
    d >> m_maxExp;

    uint8_t flags;
    d >> flags;

    if (flags & 0x01)
      m_validStam = true;
    if (flags & 0x02)
      m_validMana = true;
    if (flags & 0x04)
      m_validHP = true;
    if (flags & 0x08)
      m_validExp = true;
    if (flags & 0x10)
      m_validAttributes = true;
    if (flags & 0x20)
      m_useDefaults = true;
    else 
      m_useDefaults = false;

    seqInfo("Restored PLAYER: %s (%s)!",
            m_name.toLatin1().data(),
            m_lastName.toLatin1().data());
  }
  else
  {
    seqWarn("Failure loading %s: Unable to open!",
            fileName.toLatin1().data());
    reset();
    clear();
  }
}
