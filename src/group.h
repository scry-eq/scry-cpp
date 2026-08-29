/*
 *  group.h
 *  Copyright 2001-2005, 2008, 2019 by the respective ShowEQ Developers
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

#ifndef _GROUP_H_
#define _GROUP_H_

#include <cstdint>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>
#include <QTextStream>

#include "everquest.h"


//----------------------------------------------------------------------
// forward declarations
class Player;
class SpawnShell;
class Item;
class Spawn;

struct GroupRosterEntry
{
  uint8_t slot = 0;
  QString name;
  std::optional<uint32_t> level;
};

class GroupMgr: public QObject
{
  Q_OBJECT
 public:
  GroupMgr(SpawnShell* spawnShell, 
	   Player* player,  
	   QObject* parent = 0, const char* name = 0);
  virtual ~GroupMgr();
 
  const Spawn* memberByID( uint16_t id );
  const Spawn* memberByName( const QString& name );
  const Spawn* memberBySlot( uint16_t slot );
  QString memberNameBySlot( uint16_t slot ) const;
  uint32_t memberLevelBySlot(uint16_t slot) const;
  void applyRoster(const std::vector<GroupRosterEntry>& members,
                   bool complete);
  bool rosterComplete() const { return m_rosterComplete; }

  size_t groupSize() { return m_memberCount; }
  size_t groupMemberCount() { return m_memberCount; }
  size_t groupMembersInZoneCount() { return m_membersInZoneCount; }
  float groupBonus();
  
  unsigned long totalLevels();

 public slots:
  void player(const charProfileStruct* player); 
  void groupUpdate(const uint8_t* data, size_t size);
  void addGroupMember(const uint8_t* data, size_t size);
  void removeGroupMember(const uint8_t* data);
  // OP_GroupMemberList (0x312a) carries the full roster (the only opcode
  // that does post-patch). Variable-length per group size. Always replaces
  // existing membership — this is a full refresh, not an incremental, so
  // the per-member OP_GroupFollow handlers are intentionally NOT wired
  // alongside this one (they'd race and double-emit).
  void groupMemberList(const uint8_t* data, size_t size);
  void addItem(const Item* item);
  void delItem(const Item* item);
  void killSpawn(const Item* item);
  // SpawnShell::clear() (zone change) frees every Spawn via qDeleteAll but
  // only emits clearItems() — not per-spawn delItem/killSpawn. Without this
  // slot wired to clearItems(), our m_spawn pointers dangle past the zone and
  // the next group-member walk (e.g. fillGroupUpdate) is a use-after-free.
  // Keeps membership (names); members re-attach via addItem as they re-enter.
  void clear();

  // dump debug info
  void dumpInfo(QTextStream& out);

 signals:
  void added(const QString& name, const Spawn* mem);
  void removed(const QString& name, const Spawn* mem);
  void cleared();
  void rosterUpdated();
  
 protected:
  SpawnShell* m_spawnShell;
  Player* m_player;
  struct GroupMember
  {
    QString m_name;
    const Spawn* m_spawn;
    std::optional<uint32_t> m_level;
  }* m_members[MAX_GROUP_PEERS];
  size_t m_memberCount;
  size_t m_membersInZoneCount;
  bool m_rosterComplete = false;
};

#endif // _GROUP_H_
