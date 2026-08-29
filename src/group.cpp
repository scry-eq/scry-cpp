/*
 *  group.cpp
 *  Copyright 2001-2008, 2014, 2019 by the respective ShowEQ Developers
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

#include "group.h"
#include "seq-bridge-cxx/lib.h"
#include "player.h"
#include "spawn.h"
#include "spawnshell.h"
#include "everquest.h"
#include "diagnosticmessages.h"
#include "netstream.h"

#include <cstring>
#include <array>

#include <QSet>
#include <QTextStream>

GroupMgr::GroupMgr(SpawnShell* spawnShell,
        Player* player,
        QObject* parent, const char* name)
  : QObject(parent),
    m_spawnShell(spawnShell),
    m_player(player),
    m_memberCount(0),
    m_membersInZoneCount(0)
{
  setObjectName(name);
  for (int i=0; i<MAX_GROUP_PEERS; i++)
  {
    m_members[i] = new GroupMember();
  }

  // clear the array of members
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
    m_members[i]->m_spawn = 0;
}

GroupMgr::~GroupMgr()
{
  for (int i=0; i<MAX_GROUP_PEERS; i++)
  {
    delete m_members[i];
  }
}

void GroupMgr::player(const charProfileStruct* player)
{
  // The original logic cleared the roster here on the assumption that
  // the post-PlayerProfile OP_GroupUpdate would repopulate it from a
  // full-roster blob. Modern EQ's OP_GroupUpdate (0x6890) carries no
  // peer identity, and OP_GroupFollow (0x1bcd) only fires for new
  // joins — neither re-announces existing members on zone-in. So we
  // keep names. SpawnShell will fire delItem on the old zone's spawns
  // (clearing stale Spawn pointers in m_members) and addItem on the
  // new zone's spawns (re-resolving them by name), keeping in_zone
  // counts accurate without any work here.

// 9/3/2008 - Not used. Group data is no longer sent in charProfile.  We still
//            need to reset the data as done above.
#if 0
  // initialize the array of members with information from the player profile
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
     m_members[i]->m_name = player->groupMembers[i];

    if (!m_members[i]->m_name.isEmpty())
      m_memberCount++;

    if (m_members[i]->m_name != player->name)
      m_members[i]->m_spawn = 0;
    else
    {
      m_members[i]->m_spawn = (const Spawn*)m_player;

      m_membersInZoneCount++;
    }

    emit added(m_members[i]->m_name, m_members[i]->m_spawn);
  }
#endif
}

void GroupMgr::groupUpdate(const uint8_t* /*data*/, size_t /*size*/)
{
  // Fixed-168B status push with no roster (count is 0); membership rides
  // OP_GroupFollow/OP_GroupDisband, so both hosts ignore this form.
}

void GroupMgr::applyRoster(const std::vector<GroupRosterEntry>& members,
                           bool complete)
{
  std::array<std::optional<GroupRosterEntry>, MAX_GROUP_PEERS> incoming;
  for (const auto& member : members) {
    if (member.slot >= MAX_GROUP_PEERS || member.name.isEmpty()) continue;
    incoming[member.slot] = member;
  }

  for (int slot = 0; slot < MAX_GROUP_PEERS; ++slot) {
    GroupMember* current = m_members[slot];
    const auto& next = incoming[size_t(slot)];
    if (!next) {
      if (!current->m_name.isEmpty()) {
        emit removed(current->m_name, current->m_spawn);
        if (current->m_spawn) --m_membersInZoneCount;
        --m_memberCount;
      }
      current->m_name.clear();
      current->m_spawn = nullptr;
      current->m_level.reset();
      continue;
    }

    if (current->m_name != next->name) {
      if (!current->m_name.isEmpty()) {
        emit removed(current->m_name, current->m_spawn);
        if (current->m_spawn) --m_membersInZoneCount;
        --m_memberCount;
      }
      current->m_name = next->name;
      current->m_spawn = m_spawnShell->findPlayerByDisplayName(next->name);
      current->m_level = next->level;
      ++m_memberCount;
      if (current->m_spawn) ++m_membersInZoneCount;
      emit added(current->m_name, current->m_spawn);
    } else {
      current->m_level = next->level;
    }
  }
  m_rosterComplete = complete;
  emit rosterUpdated();
}

void GroupMgr::addGroupMember(const uint8_t* data, size_t size)
{
  // OP_GroupFollow: one member joined — add to the first empty peer slot. Level
  // comes from the Spawn when in zone (fillGroupUpdate); 0 otherwise.
  auto out = seq::rust::decode_group_follow(
      rust::Slice<const uint8_t>{data, size});
  if (!out.ok) return;
  const QString name = QString::fromLatin1(out.name.data(), int(out.name.size()));
  if (name.isEmpty()) return;
  if (m_player && name == m_player->name()) return;

  for (int i = 0; i < MAX_GROUP_PEERS; i++)
    if (m_members[i]->m_name == name) return;  // already tracked

  bool addedMember = false;
  for (int i = 0; i < MAX_GROUP_PEERS; i++) {
    if (m_members[i]->m_name.isEmpty()) {
      m_members[i]->m_name = name;
      m_members[i]->m_spawn = m_spawnShell->findPlayerByDisplayName(name);
      m_members[i]->m_level.reset();
      m_memberCount++;
      if (m_members[i]->m_spawn) m_membersInZoneCount++;
      emit added(name, m_members[i]->m_spawn);
      addedMember = true;
      break;
    }
  }
  if (addedMember) emit rosterUpdated();
}

void GroupMgr::groupMemberList(const uint8_t* data, size_t size)
{
   // OP_GroupMemberList (0x312a) — the modern roster broadcast. The Rust decoder
   // (seq-decode/group_member_list.rs) scans the variable-width record for
   // member-name runs; the *diff* against m_members (so subscribers see one
   // added/removed on a mid-zone join/leave, not a remove-all -> re-add churn)
   // stays here — it needs the Player + SpawnShell.
   auto out = seq::rust::decode_group_member_list(
       rust::Slice<const uint8_t>{data, size});
   if (!out.ok)
      return;
   if (out.member_count == 0 || out.member_count > MAX_GROUP_MEMBERS)
      return;

   const QString self = m_player ? m_player->name() : QString();
   QSet<QString> incoming;
   incoming.reserve(int(out.member_count));
   const QString joined =
       QString::fromLatin1(out.names.data(), int(out.names.size()));
   const QStringList scanned = joined.split('\n', Qt::SkipEmptyParts);
   for (const QString& name : scanned) {
      if (name == self) continue;  // peer set excludes self
      incoming.insert(name);
   }

   // Remove members no longer present.
   for (int i = 0; i < MAX_GROUP_PEERS; ++i) {
      const QString& n = m_members[i]->m_name;
      if (!n.isEmpty() && !incoming.contains(n)) {
         emit removed(n, m_members[i]->m_spawn);
         if (m_members[i]->m_spawn) m_membersInZoneCount--;
         m_members[i]->m_name.clear();
         m_members[i]->m_spawn = nullptr;
         m_memberCount--;
      }
   }

   // Add members not yet tracked. Existing entries keep their slot index so
   // subscribers that key on slot don't re-order on every mutation.
   QSet<QString> existing;
   for (int i = 0; i < MAX_GROUP_PEERS; ++i) {
      if (!m_members[i]->m_name.isEmpty())
         existing.insert(m_members[i]->m_name);
   }
   for (const QString& name : incoming) {
      if (existing.contains(name)) continue;
      for (int slot = 0; slot < MAX_GROUP_PEERS; ++slot) {
         if (m_members[slot]->m_name.isEmpty()) {
            m_members[slot]->m_name = name;
            m_members[slot]->m_spawn =
                m_spawnShell->findPlayerByDisplayName(name);
            m_memberCount++;
            if (m_members[slot]->m_spawn) m_membersInZoneCount++;
            emit added(name, m_members[slot]->m_spawn);
            break;
         }
      }
   }
   m_rosterComplete = true;
   emit rosterUpdated();
}

void GroupMgr::removeGroupMember(const uint8_t* data)
{
   auto out = seq::rust::decode_group_disband(
       rust::Slice<const uint8_t>{data, sizeof(groupDisbandStruct)});
   if (!out.ok) return;
   const QString memberName =
       QString::fromLatin1(out.membername.data(), out.membername.size());

   // If we're disbanding, reset counters and clear member slots
   if(memberName == m_player->name())
   {
      m_memberCount = 0;
      m_membersInZoneCount = 0;

      for(int i = 0; i < MAX_GROUP_PEERS; i++)
      {
         m_members[i]->m_name = "";
         m_members[i]->m_spawn = 0;
         m_members[i]->m_level.reset();
      }

      emit cleared();
   }
   else
   {
      for(int i = 0; i < MAX_GROUP_PEERS; i++)
      {
         // is this the member?
         if(m_members[i]->m_name == memberName)
         {
            // yes, announce its removal
            emit removed(m_members[i]->m_name, m_members[i]->m_spawn);

            // decrement member count
            m_memberCount--;

            // if the member is in zone decrement zone count
            m_members[i]->m_spawn = m_spawnShell->findPlayerByDisplayName(m_members[i]->m_name);
            if(m_members[i]->m_spawn)
               m_membersInZoneCount--;

            // clear it
            m_members[i]->m_name = "";
            m_members[i]->m_spawn = 0;
            m_members[i]->m_level.reset();
            break;
         }
      }
   }
   emit rosterUpdated();
}

void GroupMgr::addItem(const Item* item)
{
  // only care about Spawn class
  if ((item->type() != tSpawn) && (item->type() != tPlayer))
    return;

  const Spawn* spawn = (const Spawn*)item;

  // only care about players
  if (!spawn->isPlayer())
    return;

  // Compare against both raw name() and transformedName() — see
  // findPlayerByDisplayName for why the OP_GroupFollow payload form
  // isn't guaranteed.
  const QString raw = spawn->name();
  const QString display = spawn->transformedName();
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    if (m_members[i]->m_name.isEmpty()) continue;
    if (m_members[i]->m_name == raw || m_members[i]->m_name == display)
    {
      m_members[i]->m_spawn = spawn;
      m_membersInZoneCount++;
      break;
    }
  }
}

void GroupMgr::delItem(const Item* item)
{
  if ((item->type() != tSpawn) && (item->type() != tPlayer))
    return;

  const Spawn* spawn = (const Spawn*)item;

  // only care about players
  if (!spawn->isPlayer())
    return;

  // Match against either form (see addItem)
  const QString raw = spawn->name();
  const QString display = spawn->transformedName();
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    if (m_members[i]->m_name.isEmpty()) continue;
    if (m_members[i]->m_name == raw || m_members[i]->m_name == display)
    {
      m_members[i]->m_spawn = 0;
      m_membersInZoneCount--;
      break;
    }
  }
}

void GroupMgr::killSpawn(const Item* item)
{
  if ((item->type() != tSpawn) && (item->type() != tPlayer))
    return;

  const Spawn* spawn = (const Spawn*)item;

  // only care about players
  if (!spawn->isPlayer())
    return;

  // Match against either form (see addItem)
  const QString raw = spawn->name();
  const QString display = spawn->transformedName();
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    if (m_members[i]->m_name.isEmpty()) continue;
    if (m_members[i]->m_name == raw || m_members[i]->m_name == display)
    {
      m_members[i]->m_spawn = 0;
      m_membersInZoneCount--;
      break;
    }
  }
}

void GroupMgr::clear()
{
  // Zone change: SpawnShell just freed every Spawn. Drop the now-dangling
  // spawn pointers but keep the roster (names) — group membership survives
  // zoning. Members re-attach via addItem() as their spawns re-enter zone,
  // which re-increments m_membersInZoneCount from this zeroed baseline.
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
    m_members[i]->m_spawn = 0;
  m_membersInZoneCount = 0;
}

void GroupMgr::dumpInfo(QTextStream& out)
{
  // dump general group manager information
  out << "[GroupMgr]" << Qt::endl;
  out << "Members: " << m_memberCount << Qt::endl;
  out << "MembersInZone: " << m_membersInZoneCount << Qt::endl;
  out << "Player: " << m_player->name() << Qt::endl;
  out << "GroupBonus: " << groupBonus() << Qt::endl;
  out << "GroupTotalLevels: " << totalLevels() << Qt::endl;
  out << "GroupAverageLevel: ";
  if (m_membersInZoneCount)
    out << totalLevels()/m_membersInZoneCount << Qt::endl;
  else
    out << totalLevels() << Qt::endl;

  // iterate over the group members
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    if (m_members[i]->m_name.isEmpty())
      continue;

    out << "Member (" << i << "): " << m_members[i]->m_name;

    if (m_members[i]->m_spawn)
      out << " level " << m_members[i]->m_spawn->level()
	  << " " << m_members[i]->m_spawn->raceString()
	  << " " << m_members[i]->m_spawn->classString();

    out << Qt::endl;
  }  
}

float GroupMgr::groupBonus()
{
  switch (groupSize())
  {
  case 2:	return 1.02;
  case 3:	return 1.06;
  case 4:	return 1.10;
  case 5:	return 1.14;
  case 6:	return 1.20;
  default:	return 1.00;
  }
}

unsigned long GroupMgr::totalLevels()
{
  // if the player isn't in a group, just return their level
  if (m_memberCount == 0) 
    return m_player->level();

  unsigned long total = 0;

  // iterate over the group members
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    // add up the group member levels
    if (m_members[i]->m_spawn)
      total += m_members[i]->m_spawn->level();
  }

  // shouldn't happen, but just in-case
  if (total == 0)
    total = m_player->level();

  return total;
}

const Spawn* GroupMgr::memberByID(uint16_t id)
{
  // iterate over the members until a matching spawn id is found
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    // if this member is in zone, and the spawnid matches, return it
    if (m_members[i]->m_spawn && (m_members[i]->m_spawn->id() == id))
      return m_members[i]->m_spawn;
  }

  // not found
  return 0;
}

const Spawn* GroupMgr::memberByName(const QString& name)
{
  // iterate over the members until a matching spawn name is found
  for (int i = 0; i < MAX_GROUP_PEERS; i++)
  {
    // if this member has the name, return its spawn
    if (m_members[i]->m_name == name)
      return m_members[i]->m_spawn;
  }
  
  // not found
  return 0;
}

const Spawn* GroupMgr::memberBySlot(uint16_t slot )
{
  // validate slot value
  if (slot >= MAX_GROUP_PEERS)
    return 0;

  // guard the slot pointer too (cf. memberNameBySlot) — don't deref a
  // null GroupMember before reaching m_spawn
  if (!m_members[slot])
    return 0;

  // return the spawn object associated with the group slot, if any
  return m_members[slot]->m_spawn;
}

QString GroupMgr::memberNameBySlot(uint16_t slot) const
{
  if (slot >= MAX_GROUP_PEERS) return QString();
  if (!m_members[slot]) return QString();
  return m_members[slot]->m_name;
}

uint32_t GroupMgr::memberLevelBySlot(uint16_t slot) const
{
  if (slot >= MAX_GROUP_PEERS || !m_members[slot]) return 0;
  return m_members[slot]->m_level.value_or(0);
}
