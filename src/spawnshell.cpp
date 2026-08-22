/*
 *  spawnshell.cpp
 *  Copyright 2001 Crazy Joe Divola (cjd1@users.sourceforge.net)
 *  Portions Copyright 2001-2003,2007 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2001-2019 by the respective ShowEQ Developers
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
 * Adapted from spawnlist.cpp - Crazy Joe Divola (cjd1@users.sourceforge.net)
 * Date   - 7/31/2001
 */

#include "spawnshell.h"
#include "seq-bridge-cxx/lib.h"
#include "filtermgr.h"
#include "zonemgr.h"
#include "player.h"
#include "util.h"
#include "guild.h"
#include "packetcommon.h"
#include "diagnosticmessages.h"
#include "netstream.h"

#include <cstring>   // std::memcpy / std::memset (transitive via Qt6 but not Qt5)
#include <algorithm> // std::sort — deterministic refilter emission order
#include <vector>

#include <QFile>
#include <QDataStream>
#include <QTextStream>

#ifdef __FreeBSD__
#include <sys/types.h>
#endif
#include <climits>
#include <cmath>


//----------------------------------------------------------------------
// useful macro definitions

// define this to have the spawnshell print diagnostics
//#define SPAWNSHELL_DIAG

// define this to diagnose structures passed in to SpawnShell
//#define SPAWNSHELL_DIAG_STRUCTS

// define this to have the spawnshell validate names to help spot errors
//#define SPAWNSHELL_NAME_VALIDATE

//----------------------------------------------------------------------
// constants
static const char magicStr[5] = "spn5"; // magic is the size of uint32_t + a null
                                        // (bumped spn4->spn5: Spawn gained the
                                        // persisted m_classMask, changing layout)
static const uint32_t* magic = (uint32_t*)magicStr;
static const char * Spawn_Corpse_Designator = "'s corpse";

//----------------------------------------------------------------------
// Handy utility function
#ifdef SPAWNSHELL_NAME_VALIDATE
static bool isValidName(const char* name, size_t len)
{
  int i = 0;

  // loop over the string until the maximum length is reached
  while (i < len)
  {
    // if the terminating NULL has been found, we're done
    if ( name[i] == 0 )
      break;

    // if the current character is outside the normal range, fail the name
    if ( (name[i] < ' ') || (name[i] > '~') ) 
      return false;

    // keep going until done
    i++;
  }

  // if we finished with i being the buffer length, fail the name
  // because it's not NULL terminated
  if (i == len)
    return false;

  // it's a real name, return success
  return true;
}
#endif

//----------------------------------------------------------------------
// SpawnShell
SpawnShell::SpawnShell(FilterMgr& filterMgr, 
		       ZoneMgr* zoneMgr, 
		       Player* player,
                       GuildMgr* guildMgr)
  : QObject(NULL),
    m_zoneMgr(zoneMgr),
    m_player(player),
    m_filterMgr(filterMgr),
    m_guildMgr(guildMgr),
    m_spawns(),
    m_drops(),
    m_doors(),
    m_players()
{
   setObjectName("spawnshell");
   m_cntDeadSpawnIDs = 0;
   m_posDeadSpawnIDs = 0;
   for (int i = 0; i < MAX_DEAD_SPAWNIDS; i++)
     m_deadSpawnID[i] = 0;

   // bogus list
   m_players.insert(0, m_player);

   // connect the FilterMgr's signals to SpawnShells slots
   connect(&m_filterMgr, SIGNAL(filtersChanged()),
	   this, SLOT(refilterSpawns()));
   connect(&m_filterMgr, SIGNAL(runtimeFiltersChanged(uint8_t)),
	   this, SLOT(refilterSpawnsRuntime()));

   // connect SpawnShell slots to ZoneMgr signals
   connect(m_zoneMgr, SIGNAL(zoneBegin(const QString&)),
	   this, SLOT(clear(void)));
   connect(m_zoneMgr, SIGNAL(zoneChanged(const QString&)),
	   this, SLOT(clear(void)));

   // connect Player signals to SpawnShell signals
   connect(m_player, SIGNAL(changeItem(const Item*, uint32_t)),
	   this, SIGNAL(changeItem(const Item*, uint32_t)));
   connect(m_player, SIGNAL(playerUpdate(const uint8_t*, size_t, uint8_t)),
           this, SLOT(playerUpdate2(const uint8_t*, size_t, uint8_t)));

   // connect Player signals to SpawnShell slots
   connect(m_player, SIGNAL(changedID(uint16_t, uint16_t)),
	   this, SLOT(playerChangedID(uint16_t, uint16_t)));

   // connect to guildmgr to receive notifications of guild tag updates
   connect(m_guildMgr, SIGNAL(guildTagUpdated(uint32_t)),
           this, SLOT(updateGuildTag(uint32_t)));

   // restore the spawn list if necessary
   if (showeq_params->restoreSpawns)
     restoreSpawns();

   // create the timer
   m_timer = new QTimer(this);

   // connect the timer
   connect(m_timer, SIGNAL(timeout()),
	   this, SLOT(saveSpawns(void)));

   // start the timer (changed to oneshot to help prevent a backlog on slower
   // machines)
   if (showeq_params->saveSpawns)
   {
     m_timer->setSingleShot(true);
     m_timer->start(showeq_params->saveSpawnsFrequency);
   }
}

SpawnShell::~SpawnShell()
{
    clear();
}

void SpawnShell::clear(void)
{
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::clear()");
#endif

   emit clearItems();

   qDeleteAll(m_spawns);
   m_spawns.clear();

   qDeleteAll(m_doors);
   m_doors.clear();

   qDeleteAll(m_drops);
   m_drops.clear();

   // clear the players list, reinsert the player
   m_players.clear();
   m_players.insert(0, m_player);

   // emit an changeItem for the player
   emit changeItem(m_player, tSpawnChangedALL);

   m_cntDeadSpawnIDs = 0;
   m_posDeadSpawnIDs = 0;
   for (int i = 0; i < MAX_DEAD_SPAWNIDS; i++)
     m_deadSpawnID[i] = 0;
} // end clear

const Item* SpawnShell::findID(spawnItemType type, int id)
{
  const Item* item = NULL;
  
  if ((type == tSpawn) && (id == m_player->id()))
    return (const Item*)m_player;

  if (type != tPlayer)
    item = getMap(type).value(id, nullptr);

  return item;
}

const Item* SpawnShell::findClosestItem(spawnItemType type, 
					int16_t x, int16_t y,
					double& minDistance)
{
   ItemMap& theMap = getMap(type);
   ItemIterator it(theMap);
   double distance;
   Item* item;
   Item* closest = NULL;

   // find closest spawn

   // iterate over all the items in the map
   while (it.hasNext())
   {
     it.next();

     // get the item
     item = it.value();
     if (!item)
         break;

     // calculate the distance from the specified point
     distance = item->calcDist(x, y);

     // is this distance closer?
     if (distance < minDistance)
     {
       // yes, note it
       minDistance = distance;
       closest = item;
     }
   }

   // return the closest item.
   return closest;
}

void SpawnShell::updateGuildTag(uint32_t guildId)
{
    ItemIterator it(m_spawns);
    Spawn* spawn;

    while (it.hasNext())
    {
        it.next();

        spawn = (Spawn*)it.value();
        if (!spawn)
            break;

        if (guildId == spawn->guildID())
        {
            spawn->setGuildTag(m_guildMgr->guildIdToName(spawn->guildID(), spawn->guildServerID()));
            spawn->updateLastChanged();
            emit changeItem(spawn, tSpawnChangedALL);
        }
    }

    ItemIterator pl(m_players);

    while (pl.hasNext())
    {
        pl.next();

        spawn = (Spawn*)pl.value();
        if (!spawn)
            break;

        if (guildId == spawn->guildID())
        {
            spawn->setGuildTag(m_guildMgr->guildIdToName(spawn->guildID(), spawn->guildServerID()));
            spawn->updateLastChanged();
            emit changeItem(spawn, tSpawnChangedALL);
        }
    }
}


Spawn* SpawnShell::findSpawnByName(const QString& name)
{
  ItemIterator it(m_spawns);
  Spawn* spawn;

  while (it.hasNext())
  {
    it.next();

    // the item and coerce it to the Spawn type
    spawn = (Spawn*)it.value();
    if (!spawn)
        break;

    if (name == spawn->name())
      return spawn;
  }

  if (name == m_player->name())
    return m_player;

  return NULL;
}

Spawn* SpawnShell::findPlayerByDisplayName(const QString& name)
{
  // Match against both raw name() and transformedName() because the
  // OP_GroupFollow payload may deliver either the padded form ("Foo00")
  // or the cleaned form ("Foo") depending on the EQ build, and we
  // don't want to depend on which.
  ItemIterator it(m_spawns);
  while (it.hasNext())
  {
    it.next();
    Spawn* spawn = (Spawn*)it.value();
    if (!spawn)
      break;
    if (!spawn->isPlayer())
      continue;
    if (name == spawn->name() || name == spawn->transformedName())
      return spawn;
  }

  if (m_player &&
      (name == m_player->name() || name == m_player->transformedName()))
    return m_player;

  return nullptr;
}

void SpawnShell::deleteItem(spawnItemType type, int id)
{
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::deleteItem()");
#endif
   ItemMap& theMap = getMap(type);

   Item* item = theMap.value(id, nullptr);

   if (item != NULL)
   {
     emit delItem(item);
     theMap.remove(id);

     // send notifcation of new spawn count
     emit numSpawns(m_spawns.count());
   }
}

bool SpawnShell::updateFilterFlags(Item* item)
{
  uint8_t level = 0;

  if (item->type() == tSpawn)
    level = ((Spawn*)item)->level();

  // get the filter flags
  uint32_t flags = m_filterMgr.filterMask(item->filterString(), level);

  // see if the new filter flags are different from the old ones
  if (flags != item->filterFlags())
  {
    // yes, set the new filter flags
    item->setFilterFlags(flags);

    // return true to indicate that the flags have changed
    return true;
  }

  // flags haven't changed
  return false;
}

bool SpawnShell::updateRuntimeFilterFlags(Item* item)
{
  uint8_t level = 0;

  if (item->type() == tSpawn)
    level = ((Spawn*)item)->level();

  // get the filter flags
  uint32_t flags = m_filterMgr.runtimeFilterMask(item->filterString(), level);

  // see if the new filter flags are different from the old ones
  if (flags != item->runtimeFilterFlags())
  {
    // yes, set the new filter flags
    item->setRuntimeFilterFlags(flags);

    // return true to indicate that the flags have changed
    return true;
  }

  // flags haven't changed
  return false;
}

void SpawnShell::dumpSpawns(spawnItemType type, QTextStream& out)
{
   ItemIterator it(getMap(type));

   while (it.hasNext())
   {
       it.next();
       if (!it.value())
           break;

       out << it.value()->dumpString() << Qt::endl;
   }
}

// same-name slots, connecting to Packet signals
// this packet is variable in length.  everything is dwords except the "idFile" field
// which can be variable
void SpawnShell::newGroundItem(const uint8_t* data, size_t len, uint8_t dir)
{
   if (m_zoneMgr->isZoning())
      return;

   if (dir != DIR_Server)
      return;

   if (!data)
      return;

   auto out = seq::rust::decode_ground_spawn(
       rust::Slice<const uint8_t>{data, len});
   if (!out.ok) return;
   makeDropStruct ds{};
   QString name;
   ds.dropId  = out.drop_id;
   ds.heading = out.heading;
   ds.y = out.y;
   ds.x = out.x;
   ds.z = out.z;
   // ds is brace-zero-init'd above, so the byte after the copy is the
   // NUL terminator for the legacy char[30] idFile buffer.
   std::memcpy(ds.idFile, out.id_file.data(),
               std::min(out.id_file.size(), sizeof(ds.idFile) - 1));

#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::newGroundItem(makeDropStruct *)");
#endif

  Drop* item = (Drop*)m_drops.value(ds.dropId, nullptr);
  if (item != NULL)
  {
    item->update(&ds, name);
    updateFilterFlags(item);
    item->updateLastChanged();
    emit changeItem(item, tSpawnChangedALL);
  }
  else
  {
    item = new Drop(&ds, name);
    updateFilterFlags(item);
    m_drops.insert(ds.dropId, item);
    emit addItem(item);
  }
}

void SpawnShell::removeGroundItem(const uint8_t* data, size_t, uint8_t dir)
{
#ifdef SPAWNSHELL_DIAG
  seqDebug("SpawnShell::removeGroundItem(remDropStruct *)");
#endif

  // if zoning, then don't do anything
  if (m_zoneMgr->isZoning())
    return;

  if (dir != DIR_Server)
    return;

  auto out = seq::rust::decode_click_object(
      rust::Slice<const uint8_t>{data, sizeof(remDropStruct)});
  if (!out.ok) return;
  deleteItem(tDrop, out.drop_id);
}

void SpawnShell::newDoorSpawns(const uint8_t* data, size_t len, uint8_t dir)
{
  // Row stride is backend-owned (136 on live/test = sizeof(doorStruct); 132
  // on eql) — striding by the compiled Live sizeof would shear a diverged
  // backend's array after the first row.
  const size_t stride = seq::rust::door_stride();
  const int nDoors = len / stride;
  doorStruct tmp;
  for (int i = 0; i < nDoors; i++) {
    const uint8_t* p = data + i * stride;
    auto out = seq::rust::decode_door(
        rust::Slice<const uint8_t>{p, stride});
    if (!out.ok) continue;
    std::memset(&tmp, 0, sizeof(tmp));
    std::memcpy(tmp.name, out.name.data(),
                std::min(out.name.size(), sizeof(tmp.name) - 1));
    tmp.y = out.y;
    tmp.x = out.x;
    tmp.z = out.z;
    tmp.heading = out.heading;
    tmp.incline = out.incline;
    tmp.size = out.size;
    tmp.doorId = out.door_id;
    tmp.opentype = out.opentype;
    tmp.spawnstate = out.spawnstate;
    tmp.invertstate = out.invertstate;
    tmp.zonePoint = out.zone_point;
    newDoorSpawn(tmp, sizeof(doorStruct), dir);
  }
}

void SpawnShell::newDoorSpawn(const doorStruct& d, size_t len, uint8_t dir)
{
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::newDoorSpawn(doorStruct*)");
#endif
   Item* item = m_doors.value(d.doorId, nullptr);
   if (item != NULL)
   {
     Door* door = (Door*)item;
     door->update(&d);
     updateFilterFlags(door);
     item->updateLastChanged();
     emit changeItem(door, tSpawnChangedALL);
   }
   else
   {
     item = (Item*)new Door(&d);
     updateFilterFlags(item);
     m_doors.insert(d.doorId, item);
     emit addItem(item);
   }
}

void SpawnShell::zoneSpawns(const uint8_t* data, size_t len)
{
  int spawndatasize = len / sizeof(spawnStruct);

  const spawnStruct* zspawns = (const spawnStruct*)data;

  for (int i = 0; i < spawndatasize; i++)
  {
#if 0
  // Dump position updates for debugging spawn struct position changes
  for (int j=54; j<70; i++)
  {
      printf("%.2x", zspawns[i][j]);

      if ((j+1) % 8 == 0)
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
/*0004*/ unsigned pitch:12;
	 signed   animation:10;                    // velocity 
	 signed   deltaHeading:10;                 // change in heading 
/*0008*/ signed   z:19;                            // z coord (3rd loc value)
	 signed   deltaZ:13;                       // change in z
/*0012*/ signed   deltaY:13;                       // change in y
	 unsigned heading:12;                      // heading 
         unsigned padding01:7;
/*0016*/ signed   y:19;                            // y coord (2nd loc value)
	 signed   deltaX:13;                       // change in x
/*0020*/ signed   x:19;                            // x coord (1st loc value)
	 unsigned padding02:13;
/*0024*/ signed   unknown0001;                     // ***Placeholder
/*0028*/	         
};
#endif

#if 0
#pragma pack(0)
    struct pos *p = (struct pos *)(data + i*sizeof(spawnStruct) + 151);
    printf("[%.2x](%f, %f, %f), dx %f dy %f dz %f head %f dhead %f anim %d (%x, %x, %x, %x)\n",
            zspawns[i].spawnId, 
            float(p->x)/8.0, float(p->y/8.0), float(p->z)/8.0, 
            float(p->deltaX)/4.0, float(p->deltaY)/4.0, 
            float(p->deltaZ)/4.0, 
            float(p->heading), float(p->deltaHeading),
            p->animation, p->padding0000, 
            p->padding0005, p->padding0006, p->padding0014);
#endif
    newSpawn(zspawns[i]);
  }
}

static void applySpawn(spawnStruct* spawn,
                          const seq::rust::Spawn& out)
{
    auto copyStr = [](char* dst, size_t cap, const rust::String& src) {
        const size_t n = std::min(src.size(), cap - 1);
        std::memcpy(dst, src.data(), n);
        dst[n] = '\0';
    };
    copyStr(spawn->name,     sizeof(spawn->name),     out.name);
    copyStr(spawn->lastName, sizeof(spawn->lastName), out.last_name);
    copyStr(spawn->title,    sizeof(spawn->title),    out.title);
    copyStr(spawn->suffix,   sizeof(spawn->suffix),   out.suffix);

    spawn->spawnId        = out.spawn_id;
    spawn->miscData       = out.misc_data;
    spawn->bodytype       = out.body_type;
    spawn->race           = out.race;
    spawn->deity          = out.deity;
    spawn->guildID        = out.guild_id;
    spawn->guildServerID  = out.guild_server_id;
    spawn->guildstatus    = 0;  // disappeared 2018-11-14
    spawn->class_         = out.class_;
    spawn->petOwnerId     = out.pet_owner_id;
    spawn->level          = out.level;
    spawn->NPC            = out.npc;
    spawn->otherData      = out.other_data;
    spawn->charProperties = out.char_properties;
    spawn->curHp          = out.cur_hp;
    spawn->holding        = out.holding;
    spawn->state          = out.state;
    spawn->light          = out.light;
    spawn->isMercenary    = out.is_mercenary;

    // equip_data arrives in wire order [itemId, equip3, equip2, equip1, equip0]
    // per slot. Assign by field name — EquipStruct's memory order differs
    // (itemId is at offset 12, not 0), so a raw memcpy would mismap.
    static_assert(sizeof(spawn->equipment) == 45 * sizeof(uint32_t),
                  "EquipStruct array size drift");
    for (int i = 0; i < 9; ++i)
    {
        const uint32_t* e = &out.equip_data[i * 5];
        spawn->equipment[i].itemId = e[0];
        spawn->equipment[i].equip3 = e[1];
        spawn->equipment[i].equip2 = e[2];
        spawn->equipment[i].equip1 = e[3];
        spawn->equipment[i].equip0 = e[4];
    }
    // posData is a raw bitfield union — a byte copy is correct.
    static_assert(sizeof(spawn->posData) == 5 * sizeof(uint32_t),
                  "posData layout drift");
    std::memcpy(&spawn->posData[0], out.pos_data.data(),
                sizeof(spawn->posData));
}

void SpawnShell::zoneEntry(const uint8_t* data, size_t len)
{
  // Zone Entry. Sent when players are added to the zone.

  auto out = seq::rust::decode_spawn(rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;
  spawnStruct *spawn = new spawnStruct;
  memset(spawn,0,sizeof(spawnStruct));
  applySpawn(spawn, out);

 #ifdef SPAWNSHELL_DIAG
  seqDebug("SpawnShell::zoneEntry(spawnStruct *(name='%s'))", spawn->name);
 #endif

  Item *item;

  if(!strcmp(spawn->name,m_player->realName().toLatin1().data()))
  {
    // Multiple zoneEntry packets are received for your spawn after you zone
    m_player->setPlayerID(spawn->spawnId);
    m_player->update(spawn);
    emit changeItem(m_player, tSpawnChangedALL);
  }
  else
  {
    if((item=m_spawns.value(spawn->spawnId, nullptr)))
    {
        // Update existing spawn
      Spawn *s=(Spawn*)item;
      s->update(spawn);
    }
    else
    {
        // Create a new spawn
      newSpawn(*spawn);
    }
  }
}

void SpawnShell::newSpawn(const uint8_t* data)
{
  // if zoning, then don't do anything
  if (m_zoneMgr->isZoning())
    return;

  const spawnStruct* spawn = (const spawnStruct*)data;

  newSpawn(*spawn);
}

void SpawnShell::newSpawn(const spawnStruct& s)
{
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::newSpawn(spawnStruct *(name='%s'))", s.name);
#endif
   // if this is the SPAWN_SELF it's the player
   if (s.NPC == SPAWN_SELF)
     return;

   // Mounts are NPC children of the riding character; skip them.
   if (Spawn::calcIsMount(s.race, s.level))
     return;

   // not the player, so check if it's a recently deleted spawn
   for (int i =0; i < m_cntDeadSpawnIDs; i++)
   {
     if ((m_deadSpawnID[i] != 0) && (m_deadSpawnID[i] == s.spawnId))
     {
       // found a match, remove it from the deleted spawn list
       m_deadSpawnID[i] = 0;

       /* Commented this out because it wasn't adding shrouded spawns.
          Shrouded spawns get deleted from the zone first then added
          as a new spawn.  leaving this here in case another work-around
          needs to be found. (ieatacid - 6-8-2008)

       // let the user know what's going on
       seqInfo("%s(%d) has already been removed from the zone before we processed it.", 
	      s.name, s.spawnId);

       // and stop the attempt to add the spawn.
       return;
       */
     }
   }

   Item* item = m_spawns.value(s.spawnId, nullptr);
   if (item != NULL)
   {
     Spawn* spawn = (Spawn*)item;
     spawn->update(&s);
     updateFilterFlags(spawn);
     updateRuntimeFilterFlags(spawn);
     item->updateLastChanged();

     spawn->setGuildTag(m_guildMgr->guildIdToName(spawn->guildID(), spawn->guildServerID()));

     emit changeItem(item, tSpawnChangedALL);
   }
   else
   {
     item = new Spawn(&s);
     Spawn* spawn = (Spawn*)item;
     updateFilterFlags(spawn);
     updateRuntimeFilterFlags(spawn);
     m_spawns.insert(s.spawnId, item);

     spawn->setGuildTag(m_guildMgr->guildIdToName(spawn->guildID(), spawn->guildServerID()));

     emit addItem(item);

     // send notification of new spawn count
     emit numSpawns(m_spawns.count());
   }
}

// Target-neutral primitives for the eql backend (EqlDispatch). The caller has
// already decoded the wire struct (name + fixed-point position); these own the
// m_spawns / filter-flag / signal work. The player's own spawn is owned by
// Player (OP_ClientUpdate), so it is skipped here.
void SpawnShell::upsertSpawn(uint16_t id, const QString& name, const QString& lastName,
                             int16_t x, int16_t y, int16_t z, uint16_t heading,
                             uint8_t level, uint8_t curHpPct, uint8_t maxHpPct,
                             uint16_t race, uint8_t classVal, uint16_t deity,
                             uint16_t guildID, uint16_t guildServerID, uint8_t npc,
                             uint32_t classMask)
{
  // The player's own spawn is owned by Player, never a spawns[] entry. (On eql the
  // self-id is adopted in EqlDispatch::consumeSelfSpawn before this is reached; on
  // live it comes from the profile — either way this guard stays target-neutral.)
  if (m_player && id == m_player->id())
    return;

  // heading arrives as h2048 (0..2047); Spawn stores an 8-bit heading (256
  // directions), so scale down by 8. It's a starting facing only — OP_MobUpdate
  // / OP_NpcMoveUpdate re-establish it on first movement.
  const int8_t heading8 = (int8_t)((heading >> 3) & 0xFF);

  // Identity fields the spawn packet carries. SPAWN_PLAYER/SPAWN_NPC map 1:1 to
  // the wire's npc flag (0/1) — this replaces the old all-NPC default so PCs
  // filter/con correctly.
  auto applyIdentity = [&](Spawn* spawn) {
    if (!lastName.isEmpty())
      spawn->setLastName(lastName.toLatin1().constData());
    spawn->setHeading(heading8, 0);
    spawn->setLevel(level);
    spawn->setHP(curHpPct);
    spawn->setMaxHP(maxHpPct);
    spawn->setRace(race);
    spawn->setClassVal(classVal);
    spawn->setClassMask(classMask);   // EQL multiclass (bit N = class N); 0 on live
    spawn->setDeity(deity);
    spawn->setGuildID(guildID);
    spawn->setGuildServerID(guildServerID);
    // Resolves to "" until the guild id→name map learns this guild; the
    // GuildMgr::guildTagUpdated → updateGuildTag slot back-fills spawns that
    // were added before their OP_NewGuildInZone arrived.
    spawn->setGuildTag(m_guildMgr->guildIdToName(guildID, guildServerID));
    spawn->setNPC(npc);
  };

  Item* item = m_spawns.value(id, nullptr);
  if (item != NULL)
  {
    Spawn* spawn = (Spawn*)item;
    spawn->setPos(x, y, z);
    applyIdentity(spawn);
    item->updateLastChanged();
    emit changeItem(item, tSpawnChangedALL);
  }
  else
  {
    Spawn* spawn = new Spawn(id, x, y, z, 0, 0, 0, 0, 0, 0);
    spawn->setName(name.toLatin1().constData());
    applyIdentity(spawn);
    updateFilterFlags(spawn);
    updateRuntimeFilterFlags(spawn);
    m_spawns.insert(id, spawn);
    emit addItem(spawn);
    emit numSpawns(m_spawns.count());
  }
}

void SpawnShell::moveSpawn(uint16_t id, int16_t x, int16_t y, int16_t z)
{
  if (m_player && id == m_player->id())
    return;

  // only move spawns we already know about (created via upsertSpawn)
  Item* item = m_spawns.value(id, nullptr);
  if (item == NULL)
    return;

  Spawn* spawn = (Spawn*)item;
  spawn->setPos(x, y, z);
  spawn->updateLastChanged();
  emit changeItem(item, tSpawnChangedPosition);
}

namespace {
int8_t entityHeading(uint16_t degrees)
{
  const uint32_t normalized = degrees % 360;
  return int8_t(((360 - normalized) % 360) * 256 / 360);
}
}

void SpawnShell::applyEntitySpawn(
    uint32_t id, const QString& name, const QString& lastName, uint32_t race,
    uint32_t classVal, uint32_t deity, uint8_t level, uint8_t npc,
    uint32_t curHp, std::optional<uint32_t> maxHp, uint32_t guildID,
    uint32_t guildServerID, uint32_t classMask, std::optional<int32_t> x,
    std::optional<int32_t> y, std::optional<int32_t> z,
    std::optional<uint16_t> headingDegrees)
{
  if (id > UINT16_MAX) return;
  const int16_t px = int16_t(std::clamp(x.value_or(0),
                                        int32_t(INT16_MIN), int32_t(INT16_MAX)));
  const int16_t py = int16_t(std::clamp(y.value_or(0),
                                        int32_t(INT16_MIN), int32_t(INT16_MAX)));
  const int16_t pz = int16_t(std::clamp(z.value_or(0),
                                        int32_t(INT16_MIN), int32_t(INT16_MAX)));
  if (m_player && id == m_player->id()) return;
  Item* item = m_spawns.value(int(id), nullptr);
  auto* spawn = item ? static_cast<Spawn*>(item)
                     : new Spawn(uint16_t(id), px, py, pz, 0, 0, 0, 0, 0, 0);
  spawn->setName(name.toLatin1().constData());
  spawn->setLastName(lastName.toLatin1().constData());
  spawn->setRace(uint16_t(race));
  spawn->setClassVal(uint8_t(classVal));
  spawn->setDeity(uint16_t(deity));
  spawn->setLevel(level);
  spawn->setNPC(npc);
  spawn->setHP(int32_t(curHp));
  if (maxHp) spawn->setMaxHP(int32_t(*maxHp));
  spawn->setGuildID(uint16_t(guildID));
  spawn->setGuildServerID(uint16_t(guildServerID));
  spawn->setClassMask(classMask);
  spawn->setGuildTag(
      m_guildMgr->guildIdToName(uint16_t(guildID), uint16_t(guildServerID)));
  if (x && y && z) spawn->setPos(px, py, pz);
  if (headingDegrees) spawn->setHeading(entityHeading(*headingDegrees), 0);
  updateFilterFlags(spawn);
  updateRuntimeFilterFlags(spawn);
  spawn->updateLastChanged();
  if (item) {
    emit changeItem(spawn, tSpawnChangedALL);
  } else {
    m_spawns.insert(int(id), spawn);
    emit addItem(spawn);
    emit numSpawns(m_spawns.count());
  }
}

void SpawnShell::applyEntityMove(uint32_t id, int32_t x, int32_t y,
                                 int32_t z, uint16_t headingDegrees)
{
  if (id > UINT16_MAX) return;
  Item* item = id == m_player->id()
      ? static_cast<Item*>(m_player)
      : m_spawns.value(int(id), nullptr);
  if (!item) return;
  auto* spawn = static_cast<Spawn*>(item);
  spawn->setPos(int16_t(std::clamp(x, int32_t(INT16_MIN), int32_t(INT16_MAX))),
                int16_t(std::clamp(y, int32_t(INT16_MIN), int32_t(INT16_MAX))),
                int16_t(std::clamp(z, int32_t(INT16_MIN), int32_t(INT16_MAX))));
  spawn->setHeading(entityHeading(headingDegrees), 0);
  spawn->updateLast();
  item->updateLastChanged();
  emit changeItem(item, tSpawnChangedPosition);
}

void SpawnShell::applyEntityRemove(uint32_t id)
{
  if (id <= UINT16_MAX) deleteItem(tSpawn, int(id));
}

void SpawnShell::applyEntityRename(std::optional<uint32_t> id,
                                   const QString& oldName,
                                   const QString& newName)
{
  Spawn* spawn = nullptr;
  if (id && *id <= UINT16_MAX)
    spawn = spawnType(m_spawns.value(int(*id), nullptr));
  if (!spawn)
    spawn = findSpawnByName(oldName.toLatin1().constData());
  if (!spawn) return;
  spawn->setName(newName.toLatin1().constData());
  uint32_t change = tSpawnChangedName;
  if (updateFilterFlags(spawn)) change |= tSpawnChangedFilter;
  if (updateRuntimeFilterFlags(spawn))
    change |= tSpawnChangedRuntimeFilter;
  spawn->updateLastChanged();
  emit changeItem(spawn, change);
}

void SpawnShell::applyEntityDoors(const std::vector<EntityDoorState>& doors)
{
  QSet<int> retained;
  for (const auto& value : doors) {
    if (value.id > UINT16_MAX) continue;
    retained.insert(int(value.id));
    if (Item* old = m_doors.value(int(value.id), nullptr)) {
      m_doors.remove(int(value.id));
      delete old;
    }
    auto* door = new Door(value.id, value.name, value.x, value.y, value.z,
                          value.heading, value.incline, value.size,
                          value.openType, value.state, value.invertState,
                          value.zonePointId);
    updateFilterFlags(door);
    m_doors.insert(int(value.id), door);
    emit addItem(door);
  }
  const QList<int> existing = m_doors.keys();
  for (int id : existing) {
    if (!retained.contains(id)) deleteItem(tDoors, id);
  }
}

void SpawnShell::applyEntityGroundItem(
    uint32_t id, const QString& actorDefinition, float x, float y, float z,
    std::optional<float> heading)
{
  if (id > UINT16_MAX) return;
  if (Item* old = m_drops.value(int(id), nullptr)) {
    m_drops.remove(int(id));
    delete old;
  }
  auto* drop = new Drop(id, actorDefinition, x, y, z, heading);
  updateFilterFlags(drop);
  m_drops.insert(int(id), drop);
  emit addItem(drop);
}

void SpawnShell::applyEntityGroundItemRemoved(uint32_t id)
{
  if (id <= UINT16_MAX) deleteItem(tDrop, int(id));
}

void SpawnShell::applyEntityCorpseLocation(uint32_t id, float x, float y,
                                           float z)
{
  if (id > UINT16_MAX) return;
  Item* item = m_spawns.value(int(id), nullptr);
  if (!item) return;
  auto* spawn = static_cast<Spawn*>(item);
  spawn->setPos(int16_t(std::lrint(x)), int16_t(std::lrint(y)),
                int16_t(std::lrint(z)));
  spawn->setDeltas(0, 0, 0);
  spawn->killSpawn();
  spawn->updateLast();
  item->updateLastChanged();
  emit killSpawn(item, nullptr, 0);
}

// Neutral HP-apply primitive (eql OP_HPUpdate). Only touches spawns already
// known (created via upsertSpawn) — the player's own spawn is not tracked here.
void SpawnShell::updateSpawnHP(uint16_t id, int32_t curHp, int32_t maxHp)
{
  Item* item = m_spawns.value(id, nullptr);
  if (item == NULL)
    return;
  Spawn* spawn = (Spawn*)item;
  // A dying spawn reports negative current HP on the wire (-1, -2, -9 observed
  // on eql's stat-sync channel). Spawn stores int32 but proto hp_cur is uint32,
  // so passing it through publishes ~4.29 billion to the client. Below 0 just
  // means dead.
  spawn->setHP(curHp < 0 ? 0 : curHp);
  spawn->setMaxHP(maxHp);
  item->updateLastChanged();
  emit changeItem(item, tSpawnChangedHP);
}

// Neutral identity-apply primitive (eql OP_LoadoutSwap). Updates only the
// fields a loadout swap changes on an already-known spawn (level + class);
// position/HP are left to their own streams. The player's own spawn is not
// tracked in m_spawns — the caller refreshes it via Player::setIdentity.
void SpawnShell::updateSpawnIdentity(uint16_t id, uint8_t level, uint8_t classVal)
{
  Item* item = m_spawns.value(id, nullptr);
  if (item == NULL)
    return;
  Spawn* spawn = (Spawn*)item;
  spawn->setLevel(level);
  spawn->setClassVal(classVal);
  item->updateLastChanged();
  emit changeItem(item, tSpawnChangedALL);
}

void SpawnShell::updateSpawnAnimation(uint16_t id, uint8_t animation)
{
  Item* item = m_spawns.value(id, nullptr);
  if (item == NULL)
    return;
  Spawn* spawn = (Spawn*)item;
  if (spawn->animation() == animation)   // don't re-encode an unchanged pose
    return;
  spawn->setAnimation(animation);
  item->updateLastChanged();
  emit changeItem(item, tSpawnChangedPosition);
}

void SpawnShell::playerUpdate2(const uint8_t* data, size_t len, uint8_t dir)
{
  if (m_zoneMgr->isZoning())
    return;

  //This payload is normally handled by Player::playerUpdateSelf,
  //but sometimes it contains an update for a different spawn, such as
  //an Eye of Zomm cast by the player, a rowboat being controlled by the
  //player, etc.  So in that case, we'll handle it.
  if (len != sizeof(playerSelfPosStruct)) return;
  auto out = seq::rust::decode_player_self_pos(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;

  int16_t py = int16_t(out.y);
  int16_t px = int16_t(out.x);
  int16_t pz = int16_t(out.z);
  int16_t pdeltaX = int16_t(out.delta_x);
  int16_t pdeltaY = int16_t(out.delta_y);
  int16_t pdeltaZ = int16_t(out.delta_z);

  updateSpawn(out.spawn_id, px, py, pz, pdeltaX, pdeltaY, pdeltaZ,
          out.heading, out.delta_heading, out.animation);
}

void SpawnShell::playerUpdate(const uint8_t* data, size_t len, uint8_t dir)
{
  // if zoning, then don't do anything
  if (m_zoneMgr->isZoning())
    return;

#if 0
  // Dump position updates for debugging client update changes
  for (int i=0; i<len; i++)
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

  if (len != sizeof(playerSpawnPosStruct)) return;
  auto out = seq::rust::decode_player_spawn_pos(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;

  // Player::playerUpdateSelf handles the self-spawn (playerSelfPosStruct,
  // float x/y/z). Both handlers fire for DIR_Server, so without this guard
  // the player's position is emitted twice per packet with slightly different
  // coords — the smoother sees two positions 0ms apart. Guard it here.
  if (out.spawn_id == m_player->id())
    return;

  if (dir != DIR_Client)
  {
    int16_t y = out.y >> 3;
    int16_t x = out.x >> 3;
    int16_t z = out.z >> 3;

    int16_t dy = out.delta_y >> 2;
    int16_t dx = out.delta_x >> 2;
    int16_t dz = out.delta_z >> 2;
    
#if 0
    // Debug positioning without having to recompile everything...
#pragma pack(1)
    struct pos
{
	/*0000*/ uint16_t spawnId;
	/*0002*/ uint16_t spawnId2;
	/*0004*/ unsigned pitch:12;
                 signed   y:19;                            // y coord (2nd loc value)		 
	         unsigned padding00:1;
	/*0008*/ signed   x:19;                            // x coord (1st loc value)	 
                 signed   deltaX:13;                       // change in x
	/*0012*/ signed   deltaHeading:10;                 // change in heading 
	         signed   z:19;                            // z coord (3rd loc value)
	         unsigned padding01:3;		 
	/*0016*/ unsigned heading:12;                      // heading 
		 signed   animation:10;                    // velocity
	         unsigned padding02:10;		 
	/*0020*/ signed   deltaY:13;                       // change in y
                 signed   deltaZ:13;                       // change in z 
	         unsigned padding03:6;		 
	/*0024*/ 
};
#endif

#if 0
#pragma pack(0)
    struct pos *p = (struct pos *)data;
    if (p->spawnId == 0x49fd)
        printf("[%.2x](%f, %f, %f), dx %f dy %f dz %f\n  head %d dhead %d anim %d pitch %d (%x, %x, %x, %x, %x, %x)\n",
                p->spawnId, float(p->x)/8.0, float(p->y/8.0), float(p->z)/8.0,
                float(p->deltaX)/4.0, float(p->deltaY)/4.0,
                float(p->deltaZ)/4.0,
                p->heading, p->deltaHeading,
                p->animation, p->pitch,
                p->padding00, p->padding01, p->padding02, p->padding03 );
#endif

    updateSpawn(out.spawn_id, x, y, z, dx, dy, dz,
		out.heading, out.delta_heading, out.animation);
  }
}

void SpawnShell::npcMoveUpdate(const uint8_t* data, size_t len, uint8_t dir)
{
    // Variable length movement packet. Sanity check.
	if ((len < 13) || (len > 24))
    {
        // Ignore it.
		seqWarn("Ignoring invalid length %d for movement packet", len);
		return;
	}

    // if zoning, then don't do anything
    if (m_zoneMgr->isZoning())
    {
        return;
    }

    auto out = seq::rust::decode_npc_move_update(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok) return;
    updateSpawn(out.spawn_id,
                out.x, out.y, out.z,
                out.delta_x, out.delta_y, out.delta_z,
                static_cast<int8_t>(out.heading),
                out.delta_heading,
                static_cast<uint8_t>(out.animation));
}

void SpawnShell::updateSpawn(uint16_t id, 
			     int16_t x, int16_t y, int16_t z,
			     int16_t xVel, int16_t yVel, int16_t zVel,
			     int8_t heading, int8_t deltaHeading,
			     uint8_t animation)
{
#ifdef SPAWNSHELL_DIAG
    seqDebug("SpawnShell::updateSpawn(id=%d, x=%d, y=%d, z=%d, xVel=%d, yVel=%d, zVel=%d)", 
        id, x, y, z, xVel, yVel, zVel);
#endif

    Item* item;
   
    if (id == m_player->id())
    {
        item = m_player;
    }
    else
    {
        item = m_spawns.value(id, nullptr);
    }

    if (item != NULL)
    {
        Spawn* spawn = (Spawn*)item;

        spawn->setPos(x, y, z,
		    showeq_params->walkpathrecord,
		    showeq_params->walkpathlength);
        spawn->setAnimation(animation);

        spawn->setDeltas(xVel, yVel, zVel);
        spawn->setHeading(heading, deltaHeading);

        spawn->updateLast();
        item->updateLastChanged();
        emit changeItem(item, tSpawnChangedPosition);
    }
    else if (showeq_params->createUnknownSpawns)
    {
        // not the player, so check if it's a recently deleted spawn
        for (int i =0; i < m_cntDeadSpawnIDs; i++)
        {
            // check dead spawn list for spawnID, if it was deleted, shouldn't
            // see new position updates, so therefore this is probably 
            // for a new spawn (spawn ID being reused)
            if ((m_deadSpawnID[i] != 0) && (m_deadSpawnID[i] == id))
            {
                // found a match, ignore it
                m_deadSpawnID[i] = 0;

                seqInfo("(%d) had been removed from the zone, but saw a position update on it, so assuming bogus update.", 
                    id);

                return;
            }
        }

        item = new Spawn(id, x, y, z, xVel, yVel, zVel, 
            heading, deltaHeading, animation);
        updateFilterFlags(item);
        updateRuntimeFilterFlags(item);
        m_spawns.insert(id, item);
        emit addItem(item);

#ifdef SPAWNSHELL_DIAG
        seqDebug("SpawnShell::updateSpawn created unknown spawn (id=%u)", id);
#endif

        // send notification of new spawn count
        emit numSpawns(m_spawns.count());
    }
}

void SpawnShell::updateSpawns(const uint8_t* data)
{
  // if zoning, then don't do anything
  if (m_zoneMgr->isZoning())
    return;

  auto out = seq::rust::decode_mob_update(
      rust::Slice<const uint8_t>{data, sizeof(spawnPositionUpdate)});
  if (!out.ok) return;
  updateSpawn(out.spawn_id,
              static_cast<int16_t>(out.x),
              static_cast<int16_t>(out.y),
              static_cast<int16_t>(out.z),
              0, 0, 0,
              static_cast<int8_t>(out.heading), 0, 0);
}

void SpawnShell::updateSpawnInfo(const uint8_t* data)
{
   auto out = seq::rust::decode_wear_change(
       rust::Slice<const uint8_t>{data, sizeof(SpawnUpdateStruct)});
   if (!out.ok) return;
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::updateSpawnInfo(id=%d, sub=%d, hp=%d, maxHp=%d)",
	  out.spawn_id, out.subcommand, out.arg1, out.arg2);
#endif

   Item* item = m_spawns.value(out.spawn_id, nullptr);
   if (item != NULL)
   {
     Spawn* spawn = (Spawn*)item;
     switch(out.subcommand) {
     case 17: // current hp update
       spawn->setHP(out.arg1);
       item->updateLastChanged();
       emit changeItem(item, tSpawnChangedHP);
       break;
     }
   }
}

// OP_SpawnAppearance2 (S>C). On lock-ruleset (TLP) servers, type=0x2c carries
// the mob-lock / FTE attackable flag (value 1=locked, 0=attackable). Other
// types are ignored here. Emits a full-spawn re-encode (tSpawnChangedALL) so
// the new locked state reaches clients — SpawnUpdated carries no lock field.
void SpawnShell::updateSpawnLock(const uint8_t* data)
{
   const spawnAppearance2Struct* sa = (const spawnAppearance2Struct*)data;
   if (sa->type != 0x2c)               // 44 = mob-lock / FTE flag
     return;

   Item* item = m_spawns.value(sa->spawnId, nullptr);
   if (item == NULL)
     return;

   Spawn* spawn = (Spawn*)item;
   const bool locked = (sa->value != 0);
   if (spawn->locked() == locked)      // no change — don't spam re-encodes
     return;

   spawn->setLocked(locked);
   item->updateLastChanged();
   emit changeItem(item, tSpawnChangedALL);
}

void SpawnShell::renameSpawn(const uint8_t* data)
{
    auto out = seq::rust::decode_spawn_rename(
        rust::Slice<const uint8_t>{data, sizeof(spawnRenameStruct)});
    if (!out.ok) return;
    const std::string oldName(out.old_name);
    const std::string newName(out.new_name);
#ifdef SPAWNSHELL_DIAG
    seqDebug("SpawnShell::renameSpawn(oldname=%s, newname=%s)",
             oldName.c_str(), newName.c_str());
#endif

    Spawn* renameMe = findSpawnByName(oldName.c_str());

    if (renameMe != NULL)
    {
        renameMe->setName(newName.c_str());

        uint32_t changeType = tSpawnChangedName;

        if (updateFilterFlags(renameMe))
          changeType |= tSpawnChangedFilter;
        if (updateRuntimeFilterFlags(renameMe))
          changeType |= tSpawnChangedRuntimeFilter;

        renameMe->updateLastChanged();
        emit changeItem(renameMe, changeType);
    }
    else
    {
        seqWarn("SpawnShell: tried to rename %s to %s, but the original mob didn't exist in the spawn list",
                oldName.c_str(), newName.c_str());
    }
}

void SpawnShell::illusionSpawn(const uint8_t* data)
{
    auto out = seq::rust::decode_illusion(
        rust::Slice<const uint8_t>{data, sizeof(spawnIllusionStruct)});
    if (!out.ok) return;
#ifdef SPAWNSHELL_DIAG
    seqDebug("SpawnShell::illusionSpawn(id=%d, name=%s, new race=%d)",
             out.spawn_id, std::string(out.name).c_str(), out.race);
#endif

    Item* item = m_spawns.value(out.spawn_id, nullptr);

    if (item != NULL)
    {
        Spawn* spawn = (Spawn*) item;

        // Update what we can
        spawn->setGender(out.gender);
        spawn->setRace(out.race);

        spawn->updateLastChanged();
        emit changeItem(spawn, tSpawnChangedALL);
#ifdef SPAWNSHELL_DIAG
        seqDebug("SpawnShell: Illusioned %s (id=%d) into race %d",
                 std::string(out.name).c_str(), out.spawn_id, out.race);
#endif
    }
    else
    {
        // Someone with an illusion up zoning in will generate an
        // OP_Illusion BEFORE the OP_NewSpawn, so they won't be
        // in the spawn list. Their spawnStruct will have their
        // illusioned race anyways.
    }
}

void SpawnShell::shroudSpawn(const uint8_t* data, size_t len, uint8_t dir)
{
    // Self or other person shrouding. The embedded spawnStruct decodes through
    // the Rust decoder (decode_spawn) like a normal zone-entry spawn; only the
    // 6-byte header framing + the trailing self-profile stay here.
    NetStream netStream(data,len);

    uint32_t spawnID=netStream.readUInt32NC();
    uint16_t spawnStructSize=netStream.readUInt16NC();
    spawnStructSize-=6;

    auto out = seq::rust::decode_spawn(
        rust::Slice<const uint8_t>{netStream.pos(), spawnStructSize});
    if (!out.ok) return;

    if(spawnID!=m_player->id())
    {
        // Shrouding other player
        spawnShroudOther *shroud = new spawnShroudOther;
        memset(&shroud->spawn, 0, sizeof(shroud->spawn));
        applySpawn(&shroud->spawn, out);
        seqInfo("Shrouding %s (id=%d)", shroud->spawn.name, shroud->spawn.spawnId);
        newSpawn(shroud->spawn);
    }
    else
    {
        // Shrouding yourself.
        spawnShroudSelf *shroud = new spawnShroudSelf;

        memset(&shroud->spawn, 0, sizeof(shroud->spawn));
        applySpawn(&shroud->spawn, out);
        netStream.skipBytes(spawnStructSize);
        memcpy(&shroud->profile,netStream.pos(),sizeof(playerProfileStruct));
        seqInfo("Shrouding %s (id=%d)", shroud->spawn.name, shroud->spawn.spawnId);

        m_player->loadProfile(shroud->profile);

        // We just updated a lot of stuff.
        updateFilterFlags(m_player);
        updateRuntimeFilterFlags(m_player);
        m_player->updateLastChanged();
        emit changeItem(m_player, tSpawnChangedALL);
    }
}

void SpawnShell::updateSpawnAppearance(const uint8_t* data)
{
    auto out = seq::rust::decode_spawn_appearance(
        rust::Slice<const uint8_t>{data, sizeof(spawnAppearanceStruct)});
    if (!out.ok) return;
#ifdef SPAWNSHELL_DIAG
    seqDebug("SpawnShell::updateSpawnAppearance(id=%d, sub=%d, parm=%08x)",
             out.spawn_id, out.kind, out.parameter);
#endif

   Item* item = m_spawns.value(out.spawn_id, nullptr);

   if (item != NULL)
   {
       Spawn* spawn = (Spawn*)item;

       switch(out.kind)
       {
           case 1: // level update
               // Live's re-derived 8-byte layout carries NO value field (the
               // bridge reports parameter 0 there), so guard rather than write
               // a level of 0 over a good one. eql still carries a real value.
               if (out.parameter)
               {
                   spawn->setLevel(out.parameter);
                   spawn->updateLastChanged();
                   emit changeItem(spawn, tSpawnChangedLevel);
               }
               break;
       }

      /* Other types for OP_SpawnAppearance (from eqemu guys)
       0  - this causes the client to keel over and zone to bind point
       1  - level, parm = spawn level
       3  - 0 = visible, 1 = invisible
       4  - 0 = blue, 1 = pvp (red)
       5  - light type emitted by player (lightstone, shiny shield)
       14 - anim, 100=standing, 110=sitting, 111=ducking, 115=feigned, 105=looting
       15 - sneak, 0 = normal, 1 = sneaking
       16 - server to client, sets player spawn id
       17 - Client->Server, my HP has changed (like regen tic)
       18 - linkdead, 0 = normal, 1 = linkdead
       19 - lev, 0=off, 1=flymode, 2=levitate
       20 - GM, 0 = normal, 1 = GM - all odd numbers seem to make it GM
       21 - anon, 0 = normal, 1 = anon, 2 = roleplay
       22 - guild id
       23 - guild rank, 0=member, 1=officer, 2=leader
       24 - afk, 0 = normal, 1 = afk
       28 - autosplit, 0 = normal, 1 = autosplit on
       29 - spawn's size
       31 -change PC's name's color to NPC color 0 = normal, 1 = npc name
       */
   }
}

void SpawnShell::updateNpcHP(const uint8_t* data)
{
  auto out = seq::rust::decode_hp_update(
      rust::Slice<const uint8_t>{data, sizeof(hpNpcUpdateStruct)});
  if (!out.ok) return;
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::updateNpcHP(id=%d, maxhp=%d hp=%d)",
	  out.spawn_id, out.max_hp, out.cur_hp);
#endif
   Item* item = m_spawns.value(out.spawn_id, nullptr);
   if (item != NULL)
   {
     Spawn* spawn = (Spawn*)item;
     spawn->setHP(out.cur_hp);
     spawn->setMaxHP(out.max_hp);
     item->updateLastChanged();
     emit changeItem(item, tSpawnChangedHP);
   }
}

void SpawnShell::updateMobHealth(const uint8_t* data)
{
  auto out = seq::rust::decode_mob_health(
      rust::Slice<const uint8_t>{data, sizeof(mobHealthStruct)});
  if (!out.ok) return;

  Item* item = m_spawns.value(out.spawn_id, nullptr);
  if (item == NULL)
    return;
  Spawn* spawn = (Spawn*)item;
  // hpPercent is 0-100; reconstruct curHP using the maxHP cached from a
  // prior OP_HPUpdate / OP_InitialMobHealth. Without maxHP we can't
  // compute a meaningful curHP, so leave it untouched and just refresh
  // the change timestamp so the snapshot tail still flows.
  if (spawn->maxHP() > 0)
  {
    spawn->setHP((spawn->maxHP() * out.hp_percent) / 100);
  }
#ifdef SPAWNSHELL_DIAG
  seqDebug("SpawnShell::updateMobHealth(id=%d, pct=%d, maxHP=%d -> curHP=%d)",
           out.spawn_id, out.hp_percent, spawn->maxHP(), spawn->HP());
#endif
  item->updateLastChanged();
  emit changeItem(item, tSpawnChangedHP);
}

void SpawnShell::spawnWearingUpdate(const uint8_t* data)
{
  const wearChangeStruct *wearing = (const wearChangeStruct *)data;
  Item* item = m_spawns.value(wearing->spawnId, nullptr);
  if (item != NULL)
  {
    // ZBTEMP: Find newItemID
    //Spawn* spawn = (Spawn*)item;
    //    spawn->setEquipment(wearing->wearSlotId, wearing->newItemId);
    uint32_t changeType = tSpawnChangedWearing;
    if (updateFilterFlags(item))
      changeType |= tSpawnChangedFilter;
    if (updateRuntimeFilterFlags(item))
      changeType |= tSpawnChangedRuntimeFilter;
    item->updateLastChanged();
    emit changeItem(item, changeType);
  }
}

void SpawnShell::consMessage(const uint8_t* data, size_t len, uint8_t dir)
{
  // Pass the ACTUAL payload length, not sizeof(considerStruct): the EQL
  // backend's OP_Consider is 24B and its decode_consider validates that exact
  // size. Live's SZC_Match already guarantees len == sizeof(considerStruct).
  auto out = seq::rust::decode_consider(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;

  Item* item;
  Spawn* spawn;

  if (dir == DIR_Client)
  {
    if (out.player_id != out.target_id)
    {
      item = m_spawns.value(out.target_id, nullptr);
      if (item != NULL)
      {
	spawn = (Spawn*)item;

	// note that this spawn has been considered
	spawn->setConsidered(true);

	emit spawnConsidered(item);
      }
    }
    return;
  }

  // is it you that you've conned?
  if (out.player_id != out.target_id)
  {
    // find the spawn if it exists
    item = m_spawns.value(out.target_id, nullptr);

    // has the spawn been seen before?
    if (item != NULL)
    {
      // yes
      Spawn* spawn = (Spawn*)item;

      // note that this spawn has been considered
      spawn->setConsidered(true);

      emit spawnConsidered(item);
    } // end if spawn found
  } // else not yourself
} // end consMessage()

void SpawnShell::clientTarget(const uint8_t* data)
{
  auto out = seq::rust::decode_client_target(
      rust::Slice<const uint8_t>{data, sizeof(clientTargetStruct)});
  if (!out.ok) return;
  emit targetSpawn(out.new_target);
}

void SpawnShell::removeSpawn(const uint8_t* data, size_t len, uint8_t dir)
{
  if(dir==DIR_Client)
    return;
  if(len != sizeof(removeSpawnStruct))
  {
    if ((len+1) != sizeof(removeSpawnStruct))
      seqWarn("OP_RemoveSpawn (dataLen: %d) doesn't match: sizeof(removeSpawnStruct): %d",
              len, sizeof(removeSpawnStruct));
    return;
  }
  auto out = seq::rust::decode_remove_spawn(
      rust::Slice<const uint8_t>{data, sizeof(removeSpawnStruct)});
  if (!out.ok) return;
#ifdef SPAWNSHELL_DIAG
  seqDebug("SpawnShell::removeSpawn(id=%d)", out.spawn_id);
#endif

  Item *item;

  deleteItem(tSpawn, out.spawn_id);

  if(!out.remove_spawn)
  {
    // Remove a spawn from outside the update radius
    if(showeq_params->useUpdateRadius)
    {
      // Remove it
      deleteItem(tSpawn, out.spawn_id);
    }
    else
    {
      // Set flag to change its icon
      if((item=m_spawns.value(out.spawn_id, nullptr)))
      {
        Spawn *s=(Spawn*)item;
        s->setNotUpdated(true);
      }
    }
  }
}

void SpawnShell::deleteSpawn(const uint8_t* data)
{
  auto out = seq::rust::decode_delete_spawn(
      rust::Slice<const uint8_t>{data, sizeof(deleteSpawnStruct)});
  if (!out.ok) return;
  uint32_t spawnId = out.spawn_id;

#ifdef SPAWNSHELL_DIAG
  seqDebug("SpawnShell::deleteSpawn(id=%d)", spawnId);
#endif
  if (m_posDeadSpawnIDs < (MAX_DEAD_SPAWNIDS - 1))
    m_posDeadSpawnIDs++;
  else
    m_posDeadSpawnIDs = 0;

  if (m_cntDeadSpawnIDs < MAX_DEAD_SPAWNIDS)
    m_cntDeadSpawnIDs++;

  m_deadSpawnID[m_posDeadSpawnIDs] = spawnId;

  deleteItem(tSpawn, spawnId);
}

void SpawnShell::killSpawn(const uint8_t* data)
{
  auto out = seq::rust::decode_death(
      rust::Slice<const uint8_t>{data, sizeof(newCorpseStruct)});
  if (!out.ok) return;
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::killSpawn(id=%d, kid=%d)",
	  out.spawn_id, out.killer_id);
#endif
   Item* item;

   if (out.spawn_id != m_player->id())
   {
       item = m_spawns.value(out.spawn_id, nullptr);
   }
   else
   {
       item = m_player;
   }

   if (item != NULL)
   {
     Spawn* spawn = (Spawn*)item;

     // ZBTEMP: This is temporary until we can find a better way
     // set the last kill info on the player (do this before changing name)

     // only call setLastKill if *you* killed the spawn
     if(out.killer_id == m_player->id())
     {
         m_player->setLastKill(spawn->name(), spawn->level());
     }

     spawn->killSpawn();
     updateFilterFlags(item);
     updateRuntimeFilterFlags(item);

     spawn->setName(spawn->realName() + Spawn_Corpse_Designator);

     Item* killer;
     killer = m_spawns.value(out.killer_id, nullptr);
     emit killSpawn(item, killer, out.killer_id);
   }
}

void SpawnShell::respawnFromHover(const uint8_t* data, size_t len, uint8_t dir)
{
   if(dir != DIR_Client)
      return;
#ifdef SPAWNSHELL_DIAG
   seqDebug("SpawnShell::respawnFromHover()");
#endif

    // Our old player is a corpse, but we're rising from the dead. So
    // we need to pop a corpse to represent our deadselves, invalidate
    // the player, and then let the OP_ZoneEntry that is coming for the repop
    // fix the player.
    uint16_t corpseId = m_player->id();

    // invalidate the player by severing it from its Id.
    m_player->setID(0);

    // Pop a corpse
    Spawn* corpse = new Spawn((Spawn*) m_player, corpseId);

    updateFilterFlags(corpse);
    updateRuntimeFilterFlags(corpse);
    m_spawns.insert(corpse->id(), corpse);

    corpse->setGuildTag(m_guildMgr->guildIdToName(corpse->guildID(), corpse->guildServerID()));

    emit addItem(corpse);

    // send notification of new spawn count
    emit numSpawns(m_spawns.count());
}

void SpawnShell::corpseLoc(const uint8_t* data)
{
  auto out = seq::rust::decode_corpse_loc(
      rust::Slice<const uint8_t>{data, sizeof(corpseLocStruct)});
  if (!out.ok) return;
  Item* item = m_spawns.value(out.spawn_id, nullptr);
  if (item != NULL)
  {
    Spawn* spawn = (Spawn*)item;

    // set the corpses location, and make sure it's not moving...
    if ((spawn->NPC() == SPAWN_PLAYER) || (spawn->NPC() == SPAWN_PC_CORPSE))
    {
        spawn->setPos(int16_t(out.y), int16_t(out.x),
                      int16_t(out.z),
                      showeq_params->walkpathrecord,
                      showeq_params->walkpathlength);
    }
    else
    {
        spawn->setPos(int16_t(out.x), int16_t(out.y),
                      int16_t(out.z),
                      showeq_params->walkpathrecord,
                      showeq_params->walkpathlength);
    }
    spawn->killSpawn();
    spawn->updateLast();
    spawn->updateLastChanged();
    
    // signal that the spawn has changed
    emit killSpawn(item, NULL, 0);
  }
}

void SpawnShell::playerChangedID(uint16_t oldPlayerID, uint16_t newPlayerID)
{
  // If the new player id already exists as a spawn, it's the player's OWN
  // spawn — added as a mob before the id was known (EQL establishes the player
  // id from self-pos, after ZoneSpawns, so the player's ZoneSpawns entry lands
  // as a mob). Adopt its name onto the player; the deleteItem(tSpawn, ...)
  // below then drops the duplicate mob.
  Item* ownSpawn = m_spawns.value(newPlayerID, nullptr);
  if (ownSpawn && !ownSpawn->name().isEmpty()) {
    m_player->setName(ownSpawn->name());
    emit playerNameResolved(ownSpawn->name());
  }

  // remove the player from the list (if it had a 0 id)
  deleteItem(tPlayer, 0);

  if (oldPlayerID == newPlayerID)
      return;

  //remove the old player 
  deleteItem(tPlayer, oldPlayerID);


  //if the new ID already exists an unknown spawn
  deleteItem(tSpawn, newPlayerID);

  // re-insert the player into the list
  m_players.insert(newPlayerID, m_player);

  emit changeItem(m_player, tSpawnChangedALL);
}

void SpawnShell::refilterSpawns()
{
  refilterSpawns(tSpawn);
  refilterSpawns(tDrop);
  refilterSpawns(tDoors);
}

void SpawnShell::refilterSpawns(spawnItemType type)
{
   // Collect the items whose filter flags actually changed, then emit
   // changeItem in a deterministic (id, name) order. getMap(type) is a QHash —
   // emitting during its iteration notifies in per-process-randomized order,
   // which makes the downstream envelope stream (and tier-2 goldens)
   // non-deterministic. Clients key by id and don't depend on order.
   // (updateFilterFlags takes Item*; a Spawn is-an Item, so no per-type cast.)
   ItemIterator it(getMap(type));
   std::vector<Item*> changed;
   while (it.hasNext())
   {
     it.next();
     Item* item = it.value();
     if (!item)
       continue;
     if (updateFilterFlags(item))
     {
       item->updateLastChanged();
       changed.push_back(item);
     }
   }
   std::sort(changed.begin(), changed.end(),
             [](const Item* a, const Item* b) {
               if (a->id() != b->id()) return a->id() < b->id();
               return a->name() < b->name();
             });
   for (Item* item : changed)
     emit changeItem(item, tSpawnChangedFilter);
}

void SpawnShell::refilterSpawnsRuntime()
{
  refilterSpawnsRuntime(tSpawn);
  refilterSpawnsRuntime(tDrop);
  refilterSpawnsRuntime(tDoors);
}

void SpawnShell::refilterSpawnsRuntime(spawnItemType type)
{
   // Same deterministic-order rationale as refilterSpawns(): collect the
   // runtime-filter-flag changes, then emit changeItem sorted by (id, name)
   // instead of QHash-iteration order.
   ItemIterator it(getMap(type));
   std::vector<Item*> changed;
   while (it.hasNext())
   {
     it.next();
     Item* item = it.value();
     if (!item)
       continue;
     if (updateRuntimeFilterFlags(item))
     {
       item->updateLastChanged();
       changed.push_back(item);
     }
   }
   std::sort(changed.begin(), changed.end(),
             [](const Item* a, const Item* b) {
               if (a->id() != b->id()) return a->id() < b->id();
               return a->name() < b->name();
             });
   for (Item* item : changed)
     emit changeItem(item, tSpawnChangedRuntimeFilter);
}

void SpawnShell::saveSpawns(void)
{
  QFile keyFile(showeq_params->saveRestoreBaseFilename + "Spawns.dat");
  if (keyFile.open(QIODevice::WriteOnly))
  {
    QDataStream d(&keyFile);

    // write the magic string
    d << *magic;

    // write a test value at the top of the file for a validity check
    uint32_t testVal = sizeof(spawnStruct);
    d << testVal;

    // save the name of the current zone
    d << m_zoneMgr->shortZoneName().toLower();

    // save the spawns
    ItemMap& theMap = getMap(tSpawn);

    // save the number of spawns
    testVal = theMap.count();
    d << testVal;

    ItemIterator it(theMap);
    Spawn* spawn;

    // iterate over all the items in the map
    while (it.hasNext())
    {
      it.next();

      // get the spawn
      spawn = (Spawn*)it.value();
      if (!spawn)
          break;

      // save the spawn id
      d << spawn->id();

      // save the spawn
      spawn->saveSpawn(d);
    }
  }

   // re-start the timer
   if (showeq_params->saveSpawns)
   {
     m_timer->setSingleShot(true);
     m_timer->start(showeq_params->saveSpawnsFrequency);
   }
}

void SpawnShell::restoreSpawns(void)
{
  QString fileName = showeq_params->saveRestoreBaseFilename + "Spawns.dat";
  QFile keyFile(fileName);
  if (keyFile.open(QIODevice::ReadOnly))
  {
    size_t i;
    uint32_t testVal;
    uint16_t id;
    Spawn* item;

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

    // check the test value at the top of the file
    d >> testVal;
    if (testVal != sizeof(spawnStruct))
    {
      seqWarn("Failure loading %s: Bad spawnStruct size!",
              fileName.toLatin1().data());
      return;
    }

    // attempt to validate that the info is from the current zone
    QString zoneShortName;
    d >> zoneShortName;
    if (zoneShortName != m_zoneMgr->shortZoneName().toLower())
    {
      seqWarn("\aWARNING: Restoring spawns for potentially incorrect zone (%s != %s)!",
              zoneShortName.toLatin1().data(),
              m_zoneMgr->shortZoneName().toLower().toLatin1().data());
    }

    // read the expected number of elements
    d >> testVal;

    // read in the spawns
    for (i = 0; i < testVal; i++)
    {
      // get the spawn id
      d >> id;

      // re-create the spawn
      item = new Spawn(d, id);

      // filter and add it to the list
      updateFilterFlags(item);
      updateRuntimeFilterFlags(item);
      m_spawns.insert(id, item);
      emit addItem(item);
    }

    emit numSpawns(m_spawns.count());

    seqInfo("Restored SPAWNS: count=%d!",
            m_spawns.count());
  }
  else
  {
    seqWarn("Failure loading %s: Unable to open!",
            fileName.toLatin1().data());
  }
}
