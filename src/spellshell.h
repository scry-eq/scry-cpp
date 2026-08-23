/*
 *  spellshell.h
 *  Portions Copyright 2003 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2001-2005, 2019 by the respective ShowEQ Developers
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

/*
 * Orig Author - Crazy Joe Divola
 * Date - 9/7/00
 */

#ifndef SPELLSHELL_H
#define SPELLSHELL_H

#include <QTimer>
#include <QList>
#include <ctime>
#include <cstdio>
#include <optional>
#include <sys/time.h>
#include "everquest.h"

class Player;
class SpawnShell;
class Spells;
class Spell;
class Item;

/* 
 * SpellItem
 *
 * SpellItem is class intended to store information about an EverQuest
 * spell.
 *
 */
 
/* classes from everquest.h
 * spellBuff, used in playerProfileStruct
 * castOnStruct, source and target IDs
 * castStruct, when you start to cast a spell
 * beginCastStruct, spell action struct
 * interruptCastStruct, interrupt casting
 */
class SpellItem
{
 public:
  // A permanent buff: duration never counts down and is never swept; the proto
  // forwards this so the web renders "Permanent" with no timer.
  static constexpr int PERMANENT_DURATION = -1;

  SpellItem();

  // get accessors
  uint32_t spellId() const;
  uint16_t targetId() const;
  uint16_t casterId() const;
  time_t castTime() const;
  QString castTimeStr() const;
  int duration() const;
  QString durationStr() const;
  // A permanent buff (remainingTicks <= 0 on the wire): the timer never
  // decrements or sweeps it; duration() stays PERMANENT_DURATION so the proto
  // forwards a permanent marker to the web.
  bool isPermanent() const { return m_permanent; }
  void setPermanent(bool p) { m_permanent = p; }
  bool isSong() const { return m_isSong; }
  // Beneficial (buff/heal) vs detrimental (debuff/DoT), from the spell DB.
  // Unknown spells default to beneficial so real buffs are never suppressed.
  bool beneficial() const { return m_beneficial; }
  uint16_t icon() const { return m_icon; }
  void setIcon(uint16_t icon) { m_icon = icon; }
  const QString spellName() const;
  const QString targetName() const;
  const QString casterName() const;
  int buffSlot() const;
  void setDuration(int);
  void setBuffSlot(int slot);

  // set accessors
  void setSpellId(uint32_t spellid);
  void setTargetId(uint16_t target);
  void setCasterId(uint16_t caster);
  void setSpellName(const QString& name);
  void setCasterName(const QString& name);
  void setTargetName(const QString& name);
  void updateCastTime();

  void update(uint32_t spellId, const Spell* spell, int duration,
	      uint16_t casterId, const QString& casterName,
	      uint16_t targetId, const QString& targetName);
  
 private:
  QString m_spellName;
  QString m_casterName;
  QString m_targetName;
  int m_duration;
  timeval m_castTime;

  uint32_t m_spellId;
  uint16_t m_casterId;
  uint16_t m_targetId;
  int m_buffSlot;
  bool m_isSong;
  bool m_beneficial;
  uint16_t m_icon = 0;
  bool m_permanent;

  struct startCastStruct m_cast; // Needed?
};


inline uint32_t SpellItem::spellId() const
{
  return m_spellId;
}

inline uint16_t SpellItem::targetId() const
{
  return m_targetId;
}

inline uint16_t SpellItem::casterId() const
{
  return m_casterId;
}

inline time_t SpellItem::castTime() const
{
  return m_castTime.tv_sec;
}

inline int SpellItem::duration() const
{
  return m_duration;
}

inline void SpellItem::setDuration(int d)
{
  m_duration = d;
}

inline int SpellItem::buffSlot() const
{
  return m_buffSlot;
}

inline void SpellItem::setBuffSlot(int slot)
{
  m_buffSlot = slot;
}

inline const QString SpellItem::spellName() const
{
  return m_spellName;
}

inline const QString SpellItem::targetName() const
{
  return m_targetName;
}

inline const QString SpellItem::casterName() const
{
  return m_casterName;
}

inline void SpellItem::setSpellId(uint32_t spellid)
{
  m_spellId = spellid;
}

inline void SpellItem::setTargetId(uint16_t target)
{
  m_targetId = target;
}

inline void SpellItem::setCasterId(uint16_t caster)
{
  m_casterId = caster;
}

inline void SpellItem::setSpellName(const QString& name)
{
  m_spellName = name;
}

inline void SpellItem::setCasterName(const QString& name)
{
  m_casterName = name;
}

inline void SpellItem::setTargetName(const QString& name)
{ 
  m_targetName = name;
}

class SpellShell : public QObject
{
  Q_OBJECT
 public:
  SpellShell(Player* player, SpawnShell* spawnshell, Spells* spells);
  virtual ~SpellShell();
  void deleteSpell(const SpellItem*);
  const QList<SpellItem*>& spellList() const { return m_spellList; }
  // Effects the player has cast ON mobs (DoTs/debuffs/snares), attributed to
  // the target spawn. Kept SEPARATE from m_spellList so they never reach the
  // personal buff panel.
  const QList<SpellItem*>& targetEffects() const { return m_targetEffects; }

  void applySpellAction(std::optional<uint32_t> sourceId,
                        std::optional<uint32_t> targetId, uint32_t spellId,
                        std::optional<uint8_t> casterLevel, uint32_t kind);
  void applyActiveBuff(std::optional<uint32_t> ownerId, uint32_t spellId,
                       std::optional<int32_t> remainingTicks,
                       std::optional<uint32_t> slot,
                       std::optional<uint32_t> casterId,
                       const std::optional<QString>& casterName);
  void removeActiveBuff(std::optional<uint32_t> ownerId, uint32_t spellId,
                        std::optional<uint32_t> slot);

 signals:
  void addSpell(const SpellItem *); // done
  void delSpell(const SpellItem *); // done
  void changeSpell(const SpellItem *); // done
  void clearSpells(); // done
  // Same, for the mob-effect list (target != player).
  void addEffect(const SpellItem *);
  void delEffect(const SpellItem *);
  void changeEffect(const SpellItem *);
  void clearEffects();
  void spellActionResolved();

 public slots:
  void clear();

  // slots received from EQPacket...
  void buffLoad(const spellBuff*);
  void buff(const uint8_t*, size_t, uint8_t);
  // EQ Legends OP_BuffList (0x77ae): the authoritative per-spawn active-buff
  // list with real remaining durations. Player-only here — refreshes the local
  // player's spell-timer entries (add/update per record); mob lists are ignored.
  void buffList(const uint8_t*, size_t, uint8_t);
  void action(const uint8_t*, size_t, uint8_t);
  void simpleMessage(const uint8_t* cmsg, size_t, uint8_t);
  void spellMessage(QString&);
  void zoneChanged(void);
  void killSpawn(const Item* deceased);
  // Despawn (OP_DeleteSpawn / out-of-range): drop any mob effects on that
  // spawn. Wired to SpawnShell::delItem — killSpawn only covers deaths.
  void delSpawn(const Item* despawned);
  void timeout();

 protected:
  void deleteSpell(SpellItem *);
  SpellItem* findSpell(uint32_t spellId,
		       uint16_t targetId, const QString& targetName);
  // Locate an existing mob effect by (spell, target) in m_targetEffects.
  SpellItem* findEffect(uint32_t spellId, uint16_t targetId);
  SpellItem* findSpell(uint32_t spell_id);
  SpellItem* FindSpell(uint32_t spell_id, int target_id);
  SpellItem* findSpellBySlot(int slot);
  
 private:
  Player* m_player;
  SpawnShell* m_spawnShell;
  Spells* m_spells;
  QList<SpellItem *> m_spellList;
  QList<SpellItem *> m_targetEffects;
  SpellItem* m_lastPlayerSpell;
  QTimer *m_timer;
};

#endif
