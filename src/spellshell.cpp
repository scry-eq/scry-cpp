/*
 *  spellshell.cpp
 *  Copyright 2001 Crazy Joe Divola (cjd1@users.sourceforge.net)
 *  Portions Copyright 2003-2007 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2001-2007, 2019 by the respective ShowEQ Developers
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

#include "spellshell.h"

#include <climits>
#include "seq-bridge-cxx/lib.h"
#include "util.h"
#include "player.h"
#include "spawnshell.h"
#include "spells.h"
#include "packetcommon.h"

#include "spawn.h"
#include "diagnosticmessages.h"
#include <QList>

//#define DIAG_SPELLSHELL 1 

SpellItem::SpellItem()
  : m_duration(0),
    m_castTime{0, 0},
    m_spellId(0),
    m_casterId(0),
    m_targetId(0),
    m_buffSlot(-1),
    m_isSong(false),
    m_beneficial(true),
    m_permanent(false)
{
    // m_cast (startCastStruct) is a wire struct populated by update();
    // not read until then. m_spellName/m_casterName/m_targetName default
    // to empty QStrings — fine.
}

void SpellItem::updateCastTime()
{
  struct timezone tz;
  gettimeofday(&m_castTime,&tz);
}

QString SpellItem::castTimeStr() const
{
   QString text;
   // using system_spawntime for now...
   if (showeq_params->systime_spawntime)
      text = QString("%1").arg(castTime());
   else {
      /* Friendlier format courtesy of Daisy */
      struct tm *CreationLocalTime = localtime( &(m_castTime.tv_sec) );
      /* tzname should be set by localtime() but this doesn't seem to
         work.  cpphack */
      char buff[256];
      sprintf(buff, "%02d:%02d:%02d", CreationLocalTime->tm_hour,
              CreationLocalTime->tm_min, CreationLocalTime->tm_sec);
      text = QString(buff);
   }
   return text;
}

QString SpellItem::durationStr() const
{
   QString text;
   int d = m_duration;
   if (d < 0)
      d = 0;
   int h = d / 3600;
   d %= 3600;

   text = QString::asprintf("%02d:%02d:%02d", h, d / 60, d % 60);
   return text;
}

void SpellItem::update(uint32_t spellId, const Spell* spell, int duration,
		       uint16_t casterId, const QString& casterName,
		       uint16_t targetId, const QString& targetName)
{
     setSpellId(spellId);

     setDuration(duration);

     if (spell)
     {
       setSpellName(spell->name());
       m_isSong = spell->isSong();
       m_beneficial = spell->beneficial();
       m_icon = spell->icon();

       if (spell->targetType() != 0x06)
	 setTargetId(targetId);
     }
     else
     {
       setSpellName(spell_name(spellId));
       m_isSong = false;
       m_beneficial = true;   // unknown spell — don't suppress as a debuff
       setTargetId(targetId);
     }

     setCasterId(casterId);

     if (!casterName.isEmpty())
       setCasterName(casterName);
     else
       setCasterName(QString("N/A"));

     if (!targetName.isEmpty())
       setTargetName(targetName);
     else
       setTargetName(QString("N/A"));

     updateCastTime();
}


SpellShell::SpellShell(Player* player, SpawnShell* spawnshell, Spells* spells)
: QObject(NULL),
  m_player(player),
  m_spawnShell(spawnshell),
  m_spells(spells),
  m_lastPlayerSpell(0)
{
   setObjectName("spellshell");
   m_timer = new QTimer(this);
   m_spellList.clear();
   connect(m_timer, SIGNAL(timeout()), SLOT(timeout()));
}

SpellShell::~SpellShell()
{
  clear();
}

SpellItem* SpellShell::findSpell(uint32_t spellId,
				 uint16_t targetId, const QString& targetName)
{
  for(QList<SpellItem*>::Iterator it = m_spellList.begin();
      it != m_spellList.end(); 
      it++) 
  {
    SpellItem *i = *it;
    if (i->spellId() == spellId)
    {
      if ((i->targetId() == targetId) || 
	  ((i->targetId() == 0) && (i->targetName() == targetName)))
	return i;
    }
  }

  return NULL;
}

SpellItem* SpellShell::findSpell(uint32_t spell_id)
{
  for(QList<SpellItem*>::Iterator it = m_spellList.begin();
      it != m_spellList.end();
      it++)
  {
    SpellItem *si = *it;

    if (si->spellId() == spell_id)
      return si;
  }

  return NULL;
}

SpellItem* SpellShell::findSpellBySlot(int slot)
{
  for (SpellItem* si : m_spellList)
    if (si->buffSlot() == slot)
      return si;
  return nullptr;
}

SpellItem* SpellShell::findEffect(uint32_t spellId, uint16_t targetId)
{
  for (SpellItem* si : m_targetEffects)
    if (si->spellId() == spellId && si->targetId() == targetId)
      return si;
  return nullptr;
}

void SpellShell::clear()
{
   emit clearSpells();

   m_lastPlayerSpell = 0;
   for(QList<SpellItem*>::Iterator it = m_spellList.begin();
         it != m_spellList.end(); it++)
      delete (*it);

   m_spellList.clear();

   // Only signal an effects-clear if there were effects — an unconditional
   // empty send fires on every login (newPlayer -> clear), and its wallclock
   // captured_ms makes every replay golden flap.
   if (!m_targetEffects.isEmpty())
   {
     emit clearEffects();
     for(QList<SpellItem*>::Iterator it = m_targetEffects.begin();
           it != m_targetEffects.end(); it++)
        delete (*it);
     m_targetEffects.clear();
   }

   m_timer->stop();
}

// this is just the public way of doing this
void SpellShell::deleteSpell(const SpellItem* item)
{
  deleteSpell((SpellItem*)item);
}

void SpellShell::deleteSpell(SpellItem *item)
{
   if (item)
   {
      if (m_lastPlayerSpell == item)
          m_lastPlayerSpell = 0;
      m_spellList.removeAt(m_spellList.indexOf(item));
      if (m_spellList.count() == 0)
         m_timer->stop();
      emit delSpell(item);
      delete item;
   }
}

// slots

//slot for loading buffs when main char struct is loaded
void SpellShell::buffLoad(const spellBuff* c)
{
#ifdef DIAG_SPELLSHELL
  seqDebug("Loading buff - id=%d.",c->spellid);
#endif // DIAG_SPELLSHELL

  const Spell* spell = m_spells->spell(c->spellid);
  int duration = c->duration * 6;
  SpellItem *item = findSpell(c->spellid, m_player->id(), m_player->name());
  if (item) 
  { // exists
    item->update(c->spellid, spell, duration, 
		 0, "Buff", m_player->id(), m_player->name());
    emit changeSpell(item);
  } 
  else 
  { // new spell
    item = new SpellItem();
    item->update(c->spellid, spell, duration, 
		 0, "Buff", m_player->id(), m_player->name());
    m_spellList.append(item);
    if ((m_spellList.count() > 0) && (!m_timer->isActive()))
      m_timer->start(1000 *
		     pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
    emit addSpell(item);
  }
}

void SpellShell::buff(const uint8_t* data, size_t size, uint8_t dir)
{
  // Server-side broadcasts only — DIR_Client fires are echo/noise.
  if (dir == DIR_Client)
    return;

  // OP_Buff (0x18b4) is variable-size (post-2026-05-22; the legacy buffStruct
  // is dead). The Rust decoder (seq-decode/buff.rs) extracts the wire fields by
  // form; the spell-DB duration for the initial-sync form + SpellItem
  // management stay here (they need the Player + Spells DB).
  auto out = seq::rust::decode_buff(rust::Slice<const uint8_t>{data, size});
  if (!out.ok)   // too short, or an unrecognized size form
    return;

  // form 3 (24b compact) puts a buff SLOT at offset 0, not a spawn id, and
  // always describes the local player's own buff window — the spawn filter
  // below would discard every one of them.
  const bool compact = (out.form == 3);
  if (!compact && m_player->id() != 0 && out.spawn_id != m_player->id())
    return;   // groupmate / nearby-player broadcast noise

  // 0xffffffff is a legacy no-op marker; 0 is not a real spell.
  if (out.spell_id == 0xffffffff || out.spell_id == 0)
    return;

  const uint16_t owner = compact ? m_player->id() : (uint16_t)out.spawn_id;
  const Spell* spell = m_spells->spell(out.spell_id);
  SpellItem* item = findSpell(out.spell_id, owner, QString());

  // form: 0=fade (13b), 1=initial-sync (30b), 2=live-update (34+b),
  // 3=compact (24b).
  int duration = 0;
  uint8_t slot = 0xff;
  if (compact) {
    // DECODE-ONLY, deliberately. This record is not buff state: spell 665
    // "Drifting Death" arrives here as {slot 1, changeType 4} while the chat log
    // for the same capture reads "a greater kobold has taken 196 damage from
    // your Drifting Death" — a DoT cast ON A MOB, which cannot occupy the
    // player's buff window. Field 0 therefore looks like the spell GEM slot cast
    // from, not a buff slot, and the record looks like cast activity.
    //
    // Acting on it either way is unsafe: adding would show spells cast on others
    // as buffs on the player, and treating changeType 1 as a fade would delete a
    // genuine buff of the same spell id. Legacy showeq's buffChange does exactly
    // this (unconditional m_player attribution, add on 4 / delete on 1) and has
    // the same flaw. OP_BuffList remains the authoritative buff source and is
    // unaffected.
    //
    // Identifying it needs a targeted capture: cast a known spell from a known
    // gem and check whether field 0 tracks the gem, and whether a buff received
    // from someone else ever appears here at all.
    return;
  } else if (out.form == 0) {
    if (item) deleteSpell(item);
    return;
  } else if (out.form == 1) {
    // Initial sync carries no on-wire duration — fall back to the spell DB's
    // level-scaled value. Skip rather than emit a 0-second flicker.
    slot = out.slot;
    if (spell) duration = spell->calcDuration(m_player->level()) * 6;
    if (duration <= 0) return;
  } else {
    // Live update: block-1 tick count is the player's remaining time.
    duration = int(out.dur_ticks) * 6;
    if (duration <= 0) return;
  }

  if (item) {
    item->setDuration(duration);
    if (slot != 0xff) item->setBuffSlot(slot);
    emit changeSpell(item);
  } else {
    item = new SpellItem();
    item->update(out.spell_id, spell, duration,
                 0, QString(), owner, QString());
    if (slot != 0xff) item->setBuffSlot(slot);
    m_spellList.append(item);
    if (!m_timer->isActive())
      m_timer->start(1000 *
                     pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
    emit addSpell(item);
  }
}

void SpellShell::buffList(const uint8_t* data, size_t size, uint8_t dir)
{
  // EQ Legends OP_BuffList (0x713a): authoritative per-spawn buff list, resent
  // at zone-in and on every buff change. Player buffs go to the buff panel
  // (m_spellList); a mob's own buffs go to the target-effects list keyed by its
  // spawn id (below), for the target window.
  if (dir == DIR_Client)
    return;
  auto entries =
      seq::rust::decode_buff_list(rust::Slice<const uint8_t>{data, size});
  if (entries.empty())   // decode failed / empty list
    return;

  const uint32_t spawnId = entries.front().spawn_id;
  // Owner resolution (Xerxes's OP_BuffList handler): the player when the id
  // matches OR the spawn can't be found (stale-id self during zone-in). A
  // findable non-self spawn is a mob, whose buffs go to target effects.
  const uint16_t mobId = uint16_t(spawnId);
  const Item* mob = spawnId == m_player->id()
                        ? nullptr
                        : m_spawnShell->findID(tSpawn, mobId);
  if (mob)
  {
    // Mob's own buffs -> target effects keyed by its spawn id. The list mixes
    // them with the ones WE cast on it; only the per-record caster separates
    // the two, so it decides the attribution here. Player-cast records keep
    // casterId == the player so the reap below leaves them to action()/the
    // timer, while still picking up the server's authoritative duration.
    const QString mobName = mob->name();
    const QString selfName = m_player->name();
    bool addedEff = false;
    for (const auto& e : entries)
    {
      if (e.spell_id == 0 || e.spell_id == 0xffffffff)
        continue;
      const bool perm = e.remaining_ticks <= 0;
      const int dur = perm ? SpellItem::PERMANENT_DURATION : e.remaining_ticks * 6;
      const QString caster =
          QString::fromLatin1(e.caster.data(), int(e.caster.size()));
      const uint16_t casterId =
          (!caster.isEmpty() && caster == selfName) ? uint16_t(m_player->id()) : 0;
      SpellItem* eff = findEffect(e.spell_id, mobId);
      if (eff)
      {
        eff->setDuration(dur);
        eff->setPermanent(perm);
        eff->setBuffSlot(e.slot);
        emit changeEffect(eff);
      }
      else
      {
        eff = new SpellItem();
        eff->update(e.spell_id, m_spells->spell(e.spell_id), dur, casterId,
                    caster.isEmpty() ? QStringLiteral("Buff") : caster,
                    mobId, mobName);
        eff->setPermanent(perm);
        eff->setBuffSlot(e.slot);
        m_targetEffects.append(eff);
        emit addEffect(eff);
        addedEff = true;
      }
    }
    // Authoritative for the mob's own buffs: drop ones no longer listed.
    QList<SpellItem*> gone;
    for (SpellItem* si : m_targetEffects)
    {
      if (!si || si->targetId() != mobId || si->casterId() == m_player->id())
        continue;
      bool present = false;
      for (const auto& e : entries)
        if (e.spell_id == si->spellId()) { present = true; break; }
      if (!present)
        gone.append(si);
    }
    for (SpellItem* si : gone)
    {
      m_targetEffects.removeOne(si);
      emit delEffect(si);
      delete si;
    }
    if (addedEff && !m_timer->isActive())
      m_timer->start(1000 * pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
    return;
  }

  // eql re-adopts a FRESH self-id every zone and OP_BuffList re-sends the
  // player's full buff set keyed to the new id (applied under m_player->id()
  // below). Drop any player-owned buff still carrying a PRIOR zone's self-id
  // FIRST — before re-adding — so buffs don't pile up across zones under stale
  // ids AND an intermediate BuffsUpdate emitted mid-apply never mixes the old
  // and new self-ids (one snapshot was seen carrying 168 buffs across 11 dead
  // self-ids). deleteSpell() emits delSpell so the frontend prunes in step.
  const QString playerName = m_player->name();
  if (!playerName.isEmpty())
  {
    QList<SpellItem*> orphans;
    for (SpellItem* si : m_spellList)
      if (si && si->targetName() == playerName &&
          si->targetId() != m_player->id())
        orphans.append(si);
    for (SpellItem* si : orphans)
      deleteSpell(si);
  }

  // remainingTicks <= 0 is a permanent buff. Encode it as duration -1: the
  // timer sweep skips negative durations (never counts down, never expires) and
  // the proto forwards -1 so the web panel renders it as "Permanent" with no
  // countdown. Real timers are remainingTicks * 6 seconds.
  bool added = false;
  for (const auto& e : entries)
  {
    if (e.spell_id == 0 || e.spell_id == 0xffffffff)
      continue;
    const bool permanent = e.remaining_ticks <= 0;
    const int duration =
        permanent ? SpellItem::PERMANENT_DURATION : e.remaining_ticks * 6;
    const Spell* spell = m_spells->spell(e.spell_id);
    // The player's OWN list shows every buff regardless of who cast it, so no
    // filter here — the caster is only carried through for display.
    const QString caster =
        QString::fromLatin1(e.caster.data(), int(e.caster.size()));
    const uint16_t casterId =
        (!caster.isEmpty() && caster == playerName) ? uint16_t(m_player->id()) : 0;
    SpellItem* item = findSpell(e.spell_id, m_player->id(), m_player->name());
    if (item)
    {
      item->setPermanent(permanent);
      item->setDuration(duration);
      item->setBuffSlot(e.slot);
      emit changeSpell(item);
    }
    else
    {
      item = new SpellItem();
      item->update(e.spell_id, spell, duration, casterId,
                   caster.isEmpty() ? QStringLiteral("Buff") : caster,
                   m_player->id(), m_player->name());
      item->setPermanent(permanent);
      item->setBuffSlot(e.slot);
      m_spellList.append(item);
      emit addSpell(item);
      added = true;
    }
  }

  // OP_BuffList is authoritative: a player buff no longer present has dropped
  // (EQL has no per-buff fade opcode, so nothing else removes it). Prune stale
  // entries — otherwise dropped buffs (e.g. Invis once you attack) and the
  // instant spells action() parks here linger forever. Lists are tiny, so the
  // linear scan is cheap.
  QList<SpellItem*> stale;
  for (SpellItem* si : m_spellList)
  {
    if (!si) continue;
    bool present = false;
    for (const auto& e : entries)
      if (e.spell_id == si->spellId()) { present = true; break; }
    if (!present)
      stale.append(si);
  }
  for (SpellItem* si : stale)
    deleteSpell(si);

  if (added && !m_timer->isActive())
    m_timer->start(1000 *
                   pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
}

void SpellShell::action(const uint8_t* data, size_t len, uint8_t)
{
  if (len != sizeof(actionStruct) && len != sizeof(actionAltStruct)) return;
  rust::Slice<const uint8_t> slice{data, len};
  auto out = (len == sizeof(actionAltStruct))
               ? seq::rust::decode_action_alt(slice)
               : seq::rust::decode_action(slice);
  if (!out.ok) return;
  applySpellAction(out.source ? std::optional<uint32_t>(out.source) : std::nullopt,
                   out.target ? std::optional<uint32_t>(out.target) : std::nullopt,
                   out.spell, out.level, out.kind);
}

void SpellShell::applySpellAction(std::optional<uint32_t> sourceId,
                                  std::optional<uint32_t> targetId,
                                  uint32_t spellId,
                                  std::optional<uint8_t> casterLevel,
                                  uint32_t kind)
{
  if (kind != 0xe7 || spellId == 0 || spellId == UINT32_MAX)
    return;
  if ((sourceId && *sourceId > UINT16_MAX) ||
      (targetId && *targetId > UINT16_MAX))
    return;

  const uint16_t source = sourceId ? uint16_t(*sourceId) : 0;
  const uint16_t target = targetId ? uint16_t(*targetId) : 0;
  emit spellActionResolved();

  const Item* s;
  QString targetName;

  if (target && ((s = m_spawnShell->findID(tSpawn, target))))
    targetName = s->name();

  SpellItem *item = targetId ? findSpell(spellId, target, targetName) : nullptr;
  const bool targetIsPlayer = targetId && m_player->id() != 0 &&
                              target == m_player->id();

  if (item || targetIsPlayer)
  {
    int duration = 0;
    const Spell* spell = m_spells->spell(spellId);
    if (spell && casterLevel)
      duration = spell->calcDuration(*casterLevel) * 6;
    
    QString casterName;
    if (source && ((s = m_spawnShell->findID(tSpawn, source))))
      casterName = s->name();

    if (item)
    {
#ifdef DIAG_SPELLSHELL
      seqDebug("action - found - source=%u cast id=%u on target=%u",
               source, spellId, target);
#endif // DIAG_SPELLSHELL
      
      item->update(spellId, spell, duration,
		   source, casterName, target, targetName);
      emit changeSpell(item);
    }
    else if (duration > 0)
    {
      // Only track spells with a lingering buff/debuff duration. Instant
      // spells (nukes, lifetaps, direct heals) a mob lands on the player have
      // duration 0 and would otherwise sit in the buff list forever (dur<=0 is
      // treated as permanent, so the sweep never removes them).
      // otherwise check for spells cast on us
#ifdef DIAG_SPELLSHELL
      seqDebug("action - new - source=%u cast id=%u on target=%u",
               source, spellId, target);
#endif // DIAG_SPELLSHELL
      
      // only way to get here is if there wasn't an existing spell, so...
      item = new SpellItem();
      item->update(spellId, spell, duration,
		   source, casterName, target, targetName);
      m_spellList.append(item);
      if ((m_spellList.count() > 0) && (!m_timer->isActive()))
	m_timer->start(1000 *
		       pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
      emit addSpell(item);
    }
  }
  else if (sourceId && targetId && m_player->id() != 0 &&
           source == m_player->id() && target != 0)
  {
    // The PLAYER cast a spell ON a mob (source == us, target != us). Track it
    // as a target effect (DoT/debuff/snare) attributed to the mob spawn, in
    // the SEPARATE m_targetEffects list so it never reaches the buff panel.
    int duration = 0;
    const Spell* spell = m_spells->spell(spellId);
    if (spell && casterLevel)
      duration = spell->calcDuration(*casterLevel) * 6;
    // A pure-damage nuke (no buff duration) leaves nothing lingering to show.
    if (duration <= 0)
      return;

    SpellItem* eff = findEffect(spellId, target);
    if (eff)
    {
      eff->update(spellId, spell, duration,
		  source, m_player->name(), target, targetName);
      emit changeEffect(eff);
    }
    else
    {
      eff = new SpellItem();
      eff->update(spellId, spell, duration,
		  source, m_player->name(), target, targetName);
      m_targetEffects.append(eff);
      if (!m_timer->isActive())
	m_timer->start(1000 *
		       pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
      emit addEffect(eff);
    }
  }
}

void SpellShell::applyActiveBuff(std::optional<uint32_t> ownerId,
                                 uint32_t spellId,
                                 std::optional<int32_t> remainingTicks,
                                 std::optional<uint32_t> slot,
                                 std::optional<uint32_t> casterId,
                                 const std::optional<QString>& casterName)
{
  if (!ownerId || *ownerId > UINT16_MAX || spellId == 0 ||
      spellId == UINT32_MAX)
    return;
  const uint16_t owner = uint16_t(*ownerId);
  const uint16_t caster = casterId && *casterId <= UINT16_MAX
      ? uint16_t(*casterId) : 0;
  const Spell* spell = m_spells ? m_spells->spell(spellId) : nullptr;
  int duration = 0;
  bool permanent = false;
  if (remainingTicks) {
    permanent = *remainingTicks <= 0;
    const int64_t seconds = int64_t(*remainingTicks) * 6;
    duration = permanent ? SpellItem::PERMANENT_DURATION
                         : seconds > INT_MAX ? INT_MAX : int(seconds);
  } else if (spell) {
    duration = spell->calcDuration(m_player->level()) * 6;
    if (duration <= 0) {
      permanent = true;
      duration = SpellItem::PERMANENT_DURATION;
    }
  } else {
    // Initial-sync packets omit duration. Keep an unknown positive spell id
    // visible even when the local spell database has no duration metadata.
    permanent = true;
    duration = SpellItem::PERMANENT_DURATION;
  }

  auto nameFor = [this](uint16_t id) {
    if (!id) return QString();
    const Item* item = m_spawnShell->findID(tSpawn, id);
    if (!item) item = m_spawnShell->findID(tPlayer, id);
    return item ? item->name() : QString();
  };
  const QString targetName = owner == m_player->id()
      ? m_player->name() : nameFor(owner);
  const QString resolvedCaster = casterName && !casterName->isEmpty()
      ? *casterName : nameFor(caster);
  const bool playerBuff = owner == m_player->id();
  SpellItem* item = playerBuff ? findSpell(spellId, owner, targetName)
                               : findEffect(spellId, owner);
  if (item) {
    item->update(spellId, spell, duration, caster, resolvedCaster,
                 owner, targetName);
    if (!spell) {
      item->setSpellName(QString());
      item->setIcon(0);
    }
    item->setPermanent(permanent);
    if (slot) item->setBuffSlot(int(*slot));
    if (playerBuff) emit changeSpell(item);
    else emit changeEffect(item);
    return;
  }

  // An update without prior host state can happen after a cold attach. Rust's
  // semantic event is authoritative, so materialize it instead of dropping it.
  item = new SpellItem();
  item->update(spellId, spell, duration, caster, resolvedCaster,
               owner, targetName);
  if (!spell) {
    item->setSpellName(QString());
    item->setIcon(0);
  }
  item->setPermanent(permanent);
  if (slot) item->setBuffSlot(int(*slot));
  if (playerBuff) {
    m_spellList.append(item);
    emit addSpell(item);
  } else {
    m_targetEffects.append(item);
    emit addEffect(item);
  }
  if (!m_timer->isActive() && duration > 0)
    m_timer->start(1000 *
                   pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6));
}

void SpellShell::removeActiveBuff(std::optional<uint32_t> ownerId,
                                  uint32_t spellId,
                                  std::optional<uint32_t> slot)
{
  if (!ownerId || *ownerId > UINT16_MAX) return;
  const uint16_t owner = uint16_t(*ownerId);
  if (owner == m_player->id()) {
    SpellItem* found = nullptr;
    for (SpellItem* item : m_spellList) {
      if (item && item->spellId() == spellId && item->targetId() == owner &&
          (!slot || item->buffSlot() == int(*slot))) {
        found = item;
        break;
      }
    }
    if (found) deleteSpell(found);
    return;
  }

  for (auto it = m_targetEffects.begin(); it != m_targetEffects.end(); ++it) {
    SpellItem* item = *it;
    if (!item || item->spellId() != spellId || item->targetId() != owner ||
        (slot && item->buffSlot() != int(*slot)))
      continue;
    m_targetEffects.erase(it);
    emit delEffect(item);
    delete item;
    if (m_spellList.isEmpty() && m_targetEffects.isEmpty()) m_timer->stop();
    return;
  }
}

void SpellShell::simpleMessage(const uint8_t* data, size_t, uint8_t)
{
  // if no spell cast by the player recently, then nothing to do.
  if (!m_lastPlayerSpell)
    return;

  const simpleMessageStruct* smsg = (const simpleMessageStruct*)data;
  switch(smsg->messageFormat)
  {
  case 191: // Your target has no mana to affect
  case 239: // Your target cannot be mesmerized.
  case 240: // Your target cannot be mesmerized (with this spell).
  case 242: // Your target is immune to changes in its attack speed.
  case 243: // Your target is immune to fear spells.
  case 244: // Your target is immune to changes in its run speed.
  case 245: // You are unable to change form here.
  case 248: // Your target is too high of a level for your charm spell.
  case 251: // That spell can not affect this target NPC.
  case 253: // This pet may not be made invisible.
  case 255: // You do not have a pet.
  case 263: // Your spell did not take hold.
  case 264: // Your target has resisted your attempt to mesmerize it.
  case 268: // Your target looks unaffected.
  case 269: // Stick to singing until you learn to play this instrument.
  case 271: // Your spell would not have taken hold on your target.
  case 272: // You are missing some required spell components.
  case 439: // Your spell is interrupted.
  case 3285: // Your target is too powerful to be Castigated in this manner.
  case 9035: // Your target is too high of a level for your fear spell.
  case 9036: // This spell only works in the Planes of Power.
    // delete the last player spell
    deleteSpell(m_lastPlayerSpell);
    m_lastPlayerSpell = 0;
    break;
  default:
    break;
  }
}


void SpellShell::spellMessage(QString &str)
{
   QString spell = str.right(str.length() - 7); // drop 'Spell: '
   bool b = false;
   // Your xxx has worn off.
   // Your target resisted the xxx spell.
   // Your spell fizzles.
   seqInfo("*** spellMessage *** %s", spell.toLatin1().data());
   if (spell.left(25) == QString("Your target resisted the ")) {
      spell = spell.right(spell.length() - 25);
      spell = spell.left(spell.length() - 7);
      seqInfo("RESIST: '%s'", spell.toLatin1().data());
      b = true;
   } else if (spell.right(20) == QString(" spell has worn off.")) {
      spell = spell.right(spell.length() - 5);
      spell = spell.left(spell.length() - 20);
      seqInfo("WORE OFF: '%s'", spell.toLatin1().data());
      b = true;
   }

   if (b) {
      // Can't really tell which spell/target, so just delete the last one
      for(QList<SpellItem*>::Iterator it = m_spellList.begin();
         it != m_spellList.end(); it++) {
         if ((*it)->spellName() == spell) {
            (*it)->setDuration(0);
            break;
         }
      }
   }
}

void SpellShell::zoneChanged(void)
{
  m_lastPlayerSpell = 0;
  SpellItem* spell;
  for(QList<SpellItem*>::Iterator it = m_spellList.begin();
      it != m_spellList.end(); it++) 
  {
    spell = *it;

    // clear all the invalidated spawn ids
    spell->setTargetId(0);
    spell->setCasterId(0);
  }

  // Mob effects don't survive a zone change — the mobs are gone. Drop them.
  if (!m_targetEffects.isEmpty())
  {
    emit clearEffects();
    for (SpellItem* e : m_targetEffects)
      delete e;
    m_targetEffects.clear();
    if (m_spellList.isEmpty())
      m_timer->stop();
  }
}

void SpellShell::killSpawn(const Item* deceased)
{
    uint16_t id = deceased->id();

    if (id == m_player->id())
    {
        // We're dead. No more buffs for us.
        clear();
    }
    else
    {
        SpellItem* spell;

        if (m_lastPlayerSpell && (m_lastPlayerSpell->targetId() == id))
        {
            m_lastPlayerSpell = 0;
        }

        QList<SpellItem*>::Iterator it = m_spellList.begin();
        while(it != m_spellList.end())
        {
            spell = *it;
            if (spell->targetId() == id)
            {
                it = m_spellList.erase(it);
                emit delSpell(spell);
                delete spell;
            }
            else
            {
                ++it;
            }
        }

        // The mob died — drop the player's effects on it too.
        it = m_targetEffects.begin();
        while(it != m_targetEffects.end())
        {
            spell = *it;
            if (spell->targetId() == id)
            {
                it = m_targetEffects.erase(it);
                emit delEffect(spell);
                delete spell;
            }
            else
            {
                ++it;
            }
        }

        if (m_spellList.isEmpty() && m_targetEffects.isEmpty())
        {
            m_timer->stop();
        }
    }

}

void SpellShell::delSpawn(const Item* despawned)
{
  if (!despawned) return;
  uint16_t id = despawned->id();
  bool changed = false;
  QList<SpellItem*>::Iterator it = m_targetEffects.begin();
  while (it != m_targetEffects.end())
  {
    SpellItem* eff = *it;
    if (eff->targetId() == id)
    {
      it = m_targetEffects.erase(it);
      emit delEffect(eff);
      delete eff;
      changed = true;
    }
    else
      ++it;
  }
  if (changed && m_spellList.isEmpty() && m_targetEffects.isEmpty())
    m_timer->stop();
}

void SpellShell::timeout()
{
  SpellItem* spell;

  QList<SpellItem*>::Iterator it = m_spellList.begin();
  while (it != m_spellList.end())
  {
    spell = *it;

    // Permanent buffs never count down and are never swept. (A sign check would
    // be wrong: the stock sweep transiently parks a normal buff at -5..-1 before
    // removing it, so gate on the explicit flag, not duration < 0.)
    if (spell->isPermanent())
    {
      it++;
      continue;
    }

    int d = spell->duration() -
      pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6);
    if (d > -6)
    {
      spell->setDuration(d);
      emit changeSpell(spell);
      it++;
    }
    else
    {
      seqInfo("SpellItem '%s' finished.", (*it)->spellName().toLatin1().data());
      if (m_lastPlayerSpell == spell)
          m_lastPlayerSpell = 0;
      it = m_spellList.erase(it);
      emit delSpell(spell);
      delete spell;
    }
   }

  // Same countdown sweep for mob effects (the player's DoTs/debuffs on mobs).
  it = m_targetEffects.begin();
  while (it != m_targetEffects.end())
  {
    spell = *it;
    if (spell->isPermanent())
    {
      it++;
      continue;
    }
    int d = spell->duration() -
      pSEQPrefs->getPrefInt("SpellTimer", "SpellList", 6);
    if (d > -6)
    {
      spell->setDuration(d);
      emit changeEffect(spell);
      it++;
    }
    else
    {
      it = m_targetEffects.erase(it);
      emit delEffect(spell);
      delete spell;
    }
  }

  if (m_spellList.isEmpty() && m_targetEffects.isEmpty())
    m_timer->stop();
}
