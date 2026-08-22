/*
 *  spawnshell.h
 *  Copyright 2001 Crazy Joe Divola (cjd1@users.sourceforge.net)
 *  Portions Copyright 2001-2003 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2003-2008, 2019 by the respective ShowEQ Developers
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
 * Adapted from spawnlist.h - Crazy Joe Divola (cjd1@users.sourceforge.net)
 * Date   - 7/31/2001
 */

// Major rework of SpawnShell{} classes - Zaphod (dohpaz@users.sourceforge.net)
//   Based on stuff from CJD's SpawnShell{} and stuff from SpawnList and
//   from Map.  Some optimization ideas adapted from SINS.

#ifndef SPAWNSHELL_H
#define SPAWNSHELL_H

#ifdef __FreeBSD__
#include <sys/types.h>
#else
#include <cstdint>
#endif
#include <cstdio>
#include <cmath>
#include <optional>
#include <vector>

#include <QHash>
#include <QTimer>
#include <QTextStream>

#include "everquest.h"
#include "spawn.h"

//----------------------------------------------------------------------
// forward declarations
class Player;
class ZoneMgr;
class FilterMgr;
class SpawnShell;
class EQItemDB;
class GuildMgr;

//----------------------------------------------------------------------
// constants

// The maximum number of ID's of deleted spawns to track
const int MAX_DEAD_SPAWNIDS = 50;

//----------------------------------------------------------------------
// enumerated types

//----------------------------------------------------------------------
// type definitions
typedef QHash<int, Item*> ItemMap;
typedef QHashIterator<int, Item*> ItemIterator;
typedef QHashIterator<int, Item*> ItemConstIterator;

struct EntityDoorState {
   uint32_t id = 0;
   QString name;
   float x = 0;
   float y = 0;
   float z = 0;
   float heading = 0;
   uint32_t incline = 0;
   uint32_t size = 0;
   uint8_t openType = 0;
   uint8_t state = 0;
   uint8_t invertState = 0;
   std::optional<uint32_t> zonePointId;
};

//----------------------------------------------------------------------
// SpawnShell
class SpawnShell : public QObject
{
   Q_OBJECT
public:
   SpawnShell(FilterMgr& filterMgr, 
	      ZoneMgr* zoneMgr, 
	      Player* player,
              GuildMgr* guildMgr);

   ~SpawnShell();

   const Item* findID(spawnItemType type, int idSpawn);
   
   const Item* findClosestItem(spawnItemType type, 
			       int16_t x,
			       int16_t y, 
			       double& minDistance);
   Spawn* findSpawnByName(const QString& name);
   // Like findSpawnByName but compares against Spawn::transformedName()
   // (the display form, e.g. "Foo") instead of the raw m_name (which
   // can carry the server's "Foo00"-style suffix). Player spawns only.
   Spawn* findPlayerByDisplayName(const QString& name);

   void dumpSpawns(spawnItemType type, QTextStream& out);
   FilterMgr* filterMgr(void) { return &m_filterMgr; }
   const ItemMap& getConstMap(spawnItemType type) const;
   const ItemMap& spawns(void) const;
   const ItemMap& drops(void) const;
   const ItemMap& doors(void) const;

   // Target-neutral primitives for the eql backend (EqlDispatch): create-or-
   // update a spawn from already-decoded fields, and move an existing spawn.
   // They own the m_spawns / filter-flag / signal work an external QObject
   // can't reach. No Legends wire types here; unused on live/test.
   void upsertSpawn(uint16_t id, const QString& name, const QString& lastName,
                    int16_t x, int16_t y, int16_t z, uint16_t heading,
                    uint8_t level, uint8_t curHpPct, uint8_t maxHpPct,
                    uint16_t race, uint8_t classVal, uint16_t deity,
                    uint16_t guildID, uint16_t guildServerID, uint8_t npc,
                    uint32_t classMask = 0);
   void moveSpawn(uint16_t id, int16_t x, int16_t y, int16_t z);
   void applyEntitySpawn(uint32_t id, const QString& name,
                         const QString& lastName, uint32_t race,
                         uint32_t classVal, uint32_t deity, uint8_t level,
                         uint8_t npc, uint32_t curHp,
                         std::optional<uint32_t> maxHp, uint32_t guildID,
                         uint32_t guildServerID, uint32_t classMask,
                         std::optional<int32_t> x,
                         std::optional<int32_t> y,
                         std::optional<int32_t> z,
                         std::optional<uint16_t> headingDegrees);
   void applyEntityMove(uint32_t id, int32_t x, int32_t y, int32_t z,
                        uint16_t headingDegrees);
   void applyEntityRemove(uint32_t id);
   void applyEntityRename(std::optional<uint32_t> id,
                          const QString& oldName, const QString& newName);
   void applyEntityDoors(const std::vector<EntityDoorState>& doors);
   void applyEntityGroundItem(uint32_t id, const QString& actorDefinition,
                              float x, float y, float z,
                              std::optional<float> heading);
   void applyEntityGroundItemRemoved(uint32_t id);
   void applyEntityCorpseLocation(uint32_t id, float x, float y, float z);
   void updateSpawnHP(uint16_t id, int32_t curHp, int32_t maxHp);
   void updateSpawnIdentity(uint16_t id, uint8_t level, uint8_t classVal);
   // Stand/sit/duck pose only, for backends that broadcast it independently of
   // a position update (the stock position path sets animation alongside coords).
   void updateSpawnAnimation(uint16_t id, uint8_t animation);
signals:
   // Fired when the player's own ZoneSpawns entry reveals the character name
   // during the playerChangedID reconcile (eql: the player id arrives late via
   // self-pos, so the name is only knowable from the own-spawn adoption; live
   // learns names earlier via OP_EnterWorld / the profile). DaemonApp uses it
   // to name the box in BoxRegistry when nothing else has.
   void playerNameResolved(const QString& name);
   void addItem(const Item* item);
   void delItem(const Item* item);
   void changeItem(const Item* item, uint32_t changeType);
   void killSpawn(const Item* deceased, const Item* killer, uint16_t killerId);
   void selectSpawn(const Item* item);
   void spawnConsidered(const Item* item);
   // Emitted when a clientTarget packet is parsed. spawnId is the new
   // target's spawn id (0 if the player cleared their target).
   void targetSpawn(uint32_t spawnId);
   void clearItems();
   void numSpawns(int);

public slots: 
   void clear();

   // slots to receive from EQPacket...
   void newGroundItem(const uint8_t*, size_t, uint8_t);
   void removeGroundItem(const uint8_t*, size_t, uint8_t);
   void newDoorSpawns(const uint8_t*, size_t, uint8_t);
   void newDoorSpawn(const doorStruct&, size_t, uint8_t);
   void zoneSpawns(const uint8_t* zspawns, size_t len);
   void zoneEntry(const uint8_t* spawn, size_t len);
   void newSpawn(const uint8_t* spawn);
   void newSpawn(const spawnStruct& s);
   void playerUpdate2(const uint8_t*pupdate, size_t, uint8_t);
   void playerUpdate(const uint8_t*pupdate, size_t, uint8_t);
   void npcMoveUpdate(const uint8_t*npcupdate, size_t, uint8_t);
   void updateSpawn(uint16_t id, 
		    int16_t x, int16_t y, int16_t z,
		    int16_t xVel, int16_t yVel, int16_t zVel,
		    int8_t heading, int8_t deltaHeading,
		    uint8_t animation);
   void updateSpawns(const uint8_t* updates);
   void updateSpawnInfo(const uint8_t* spawnupdate);
   void updateSpawnLock(const uint8_t* spawnappearance2);
   void renameSpawn(const uint8_t* renameupdate);
   void illusionSpawn(const uint8_t* illusionupdate);
   void updateSpawnAppearance(const uint8_t* appearanceupdate);
   void shroudSpawn(const uint8_t* spawnupdate, size_t, uint8_t);
   void updateNpcHP(const uint8_t* hpupdate);
   void updateMobHealth(const uint8_t* mobhp);
   void spawnWearingUpdate(const uint8_t* wearing);
   void consMessage(const uint8_t* con, size_t, uint8_t);
   void clientTarget(const uint8_t* cts);
   void removeSpawn(const uint8_t* rmSpawn, size_t len, uint8_t dir);
   void deleteSpawn(const uint8_t* delSpawn);
   void killSpawn(const uint8_t* deadspawn);
   void respawnFromHover(const uint8_t* respawn, size_t len, uint8_t dir);
   void corpseLoc(const uint8_t* corpseLoc);

   void playerChangedID(uint16_t oldPlayerID, uint16_t newPlayerID);
   void refilterSpawns();
   void refilterSpawnsRuntime();
   void saveSpawns(void);
   void restoreSpawns(void);

   void updateGuildTag(uint32_t guildId);

 protected:
   void refilterSpawns(spawnItemType type);
   void refilterSpawnsRuntime(spawnItemType type);
   void deleteItem(spawnItemType type, int id);
   bool updateFilterFlags(Item* item);
   bool updateRuntimeFilterFlags(Item* item);

   ItemMap& getMap(spawnItemType type);

 private:
   ZoneMgr* m_zoneMgr;
   Player* m_player;
   FilterMgr& m_filterMgr;
   GuildMgr* m_guildMgr;

   // track recently killed spawns
   uint16_t m_deadSpawnID[MAX_DEAD_SPAWNIDS];
   uint8_t m_cntDeadSpawnIDs;
   uint8_t m_posDeadSpawnIDs;

   // maps to keep track of the different types of spawns
   ItemMap m_spawns;
   ItemMap m_drops;
   ItemMap m_doors;

   ItemMap m_players;

   // timer for saving spawns
   QTimer* m_timer;
};

inline
const ItemMap& SpawnShell::getConstMap(spawnItemType type) const
{ 
  switch (type)
  {
  case tSpawn:
    return m_spawns;
  case tDrop:
    return m_drops;
  case tDoors:
    return m_doors;
  case tPlayer:
    return m_players;
  default:
    return m_spawns;
  }
}

inline
ItemMap& SpawnShell::getMap(spawnItemType type)
{ 
  switch (type)
  {
  case tSpawn:
    return m_spawns;
  case tDrop:
    return m_drops;
  case tDoors:
    return m_doors;
  case tPlayer:
    return m_players;
  default:
    return m_spawns;
  }
}

inline
const ItemMap& SpawnShell::spawns(void) const
{
  return m_spawns;
}

inline
const ItemMap& SpawnShell::drops(void) const
{
  return m_drops;
}

inline
const ItemMap& SpawnShell::doors(void) const
{
  return m_doors; 
}

//--------------------------------------------------

#endif // SPAWNSHELL_H
