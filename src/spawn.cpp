/*
 *  spawn.cpp
 *  Copyright 2001 Crazy Joe Divola (cjd1@users.sourceforge.net)
 *  Copyright 2001-2010, 2012-2013, 2019 by the respective ShowEQ Developers
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

#ifdef __FreeBSD__
#include <sys/types.h>
#endif

#include <climits>
#include <cmath>
#include <cstring>

#include <QRegularExpression>

#include "spawnshell.h"
#include "protoencoder.h"
#include "fixpt.h"
#include "util.h"

//----------------------------------------------------------------------
// constants
const char * Spawn_Corpse_Designator = "'s corpse";

// fix point q format to use for spawn optimizations
const int qFormat = 14;

// used to calculate where the mob/player should be while animating
// 1.3 was figured empiraccly.. feel free to change it..  It seems to 
// be preety close though.
const float animationCoefficient = 0.0013;

// fixed point animation coefficient
const int animationCoefficientFixPt = 
   fixPtToFixed<int, float>(animationCoefficient, qFormat);

const EquipStruct SlotEmpty = { 0, 0, 0, 0, 0 };

//----------------------------------------------------------------------
// Handy utility functions
// static 
QString print_item (uint16_t item)
{
  // sparse array of item names, some are NULL
  static const char*  itemnames[] = 
  {
#include "weapons.h"
  };

  // sparse array of item names (in 0x01 range), some are NULL
  static const char*  itemnames1[] = 
  {
#include "weapons1.h"
  };

  // sparse array of item names (in 0x27 range), some are NULL
  static const char*  itemnames27[] = 
  {
#include "weapons27.h"
  };

  // sparse array of item names (in 0x28 range), some are NULL
  static const char*  itemnames28[] = 
  {
#include "weapons28.h"
  };

  // sparse array of item names (in 0x29 range), some are NULL
  static const char*  itemnames29[] = 
  {
#include "weapons29.h"
  };

  // sparse array of item names (in 0x2a range), some are NULL
  static const char*  itemnames2a[] = 
  {
#include "weapons2a.h"
  };

  // sparse array of item names (in 0x2b range), some are NULL
  static const char*  itemnames2b[] = 
  {
#include "weapons2b.h"
  };

  // sparse array of item names (in 0x2c range), some are NULL
  static const char*  itemnames2c[] = 
  {
#include "weapons2c.h"
  };

  // sparse array of item names (in 0x2d range), some are NULL
  static const char*  itemnames2d[] =
  {
#include "weapons2d.h"
  };

  // sparse array of item names (in 0x2e range), some are NULL
  static const char*  itemnames2e[] =
  {
#include "weapons2e.h"
  };

  // sparse array of item names (in 0x2f range), some are NULL
  static const char*  itemnames2f[] =
  {
#include "weapons2f.h"
  };

  // sparse array of item names (in 0x30 range), some are NULL
  static const char*  itemnames30[] =
  {
#include "weapons30.h"
  };

  // sparse array of item names (in 0x4e range), some are NULL
  static const char*  itemnames4e[] =
  {
#include "weapons4e.h"
  };

  // assume no material name found
  const char *itemStr = NULL;

  uint32_t itemLo = item & 0x00ff;
  uint32_t itemHi = (item & 0xff00) >> 8;

  if (itemHi == 0x00)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames) / sizeof (char*)))
      itemStr = itemnames[itemLo];
  }
  else if (itemHi == 0x01)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames1) / sizeof (char*)))
      itemStr = itemnames1[itemLo];
  }
  else if (itemHi == 0x27)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames27) / sizeof (char*)))
      itemStr = itemnames27[itemLo];
  }
  else if (itemHi == 0x28)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames28) / sizeof (char*)))
      itemStr = itemnames28[itemLo];
  }
  else if (itemHi == 0x29)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames29) / sizeof (char*)))
      itemStr = itemnames29[itemLo];
  }
  else if (itemHi == 0x2a)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames2a) / sizeof (char*)))
      itemStr = itemnames2a[itemLo];
  }
  else if (itemHi == 0x2b)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames2b) / sizeof (char*)))
      itemStr = itemnames2b[itemLo];
  }
  else if (itemHi == 0x2c)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames2c) / sizeof (char*)))
      itemStr = itemnames2c[itemLo];
  }
  else if (itemHi == 0x2d)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames2d) / sizeof (char*)))
      itemStr = itemnames2d[itemLo];
  }
  else if (itemHi == 0x2e)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames2e) / sizeof (char*)))
      itemStr = itemnames2e[itemLo];
  }
  else if (itemHi == 0x2f)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames2f) / sizeof (char*)))
      itemStr = itemnames2f[itemLo];
  }
  else if (itemHi == 0x30)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames30) / sizeof (char*)))
      itemStr = itemnames30[itemLo];
  }
  else if (itemHi == 0x4e)
  {
    // retrieve pointer to item name
    if (itemLo < (sizeof(itemnames4e) / sizeof (char*)))
      itemStr = itemnames4e[itemLo];
  }

  // if race name exists, then return it, otherwise return a number string
  if (itemStr != NULL)
    return itemStr;
  else
  {
    QString item_str;
    item_str = QString::asprintf("U%04x", item);
    return item_str;
  }
}

//----------------------------------------------------------------------
// Item
Item::Item(spawnItemType t, uint16_t id)
  : m_filterFlags(0),
    m_runtimeFilterFlags(0),
    m_ID(id),
    m_NPC(99), // random bogus value
    m_type(t)
{
  m_spawnTime = QTime::currentTime();
  m_lastUpdate = QTime::currentTime();
  updateLastChanged();
}

Item::~Item()
{
}

QString Item::name() const
{
  return m_name;
}

QString Item::transformedName() const
{
  return name();
}

uint16_t Item::race() const
{
  return 0;
}

QString Item::raceString() const
{
  return "Unknown";
}

uint8_t Item::classVal() const
{
  return 0; 
}

QString Item::classString() const
{
  return "Unknown";
}

QString Item::info() const
{
  return "";
}

QString Item::filterString() const
{
  QString buff;
  buff = QString::asprintf("Name:%s:Race:%s:Class:%s:NPC:%d:X:%d:Y:%d:Z:%d:",
          transformedName().toUtf8().data(),
          raceString().toUtf8().data(),
          classString().toUtf8().data(),
          NPC(), x(), y(), z());
  return buff;
}

QString Item::dumpString() const
{
  return QString("ID:") + QString::number(id()) + ":" + filterString();
}

void Item::setPos(int16_t x, int16_t y, int16_t z)
{
  // set the item position
  setPoint(x, y, z);
}

//----------------------------------------------------------------------
// Spawn
Spawn::Spawn()
  : Item(tSpawn, 0)
{
  m_name = "fake";
  m_lastName = "";
  setNPC(SPAWN_NPC_UNKNOWN);

  Item::setPos(0, 0, 0);
  setDeltas(0, 0, 0);
  setHeading(0, 0);
  setAnimation(0);
  setPetOwnerID(0);
  setLight(0);
  setGender(0);
  setDeity(0);
  setRace(0);
  setClassVal(0);
  setHP(0);
  setMaxHP(0);
  setGuildID(0xffff);
  setGuildServerID(0);
  setGuildTag(NULL);
  setLevel(0);
  setTypeflag(0);
  setGM(0);
  for (int i = 0; i < tNumWearSlots; i++)
    setEquipment(i, SlotEmpty);

  // The bool flags below are otherwise only set by Spawn::update()
  // when a spawnStruct arrives. Encoder and category logic both read
  // them via getters; uninitialized bytes turn into nondeterministic
  // wire output and weird category matches before the first update.
  setIsMount(false);
  setIsMercenary(0);
  setIsAura(0);
  setNotUpdated(false);

  // just clear the considred flag since data would be outdated
  setConsidered(false);

  // finally, note when this update occurred.
  updateLast();
}

Spawn::Spawn(const spawnStruct* s)
  : Item(tSpawn, s->spawnId)
{
  // have update initialize everything
  update(s);
}

Spawn::Spawn(uint16_t id, 
	     int16_t x, int16_t y, int16_t z,
	     int16_t deltaX, int16_t deltaY, int16_t deltaZ,
	     int8_t heading, int8_t deltaHeading,
	     uint8_t animation) 
  : Item(tSpawn, id)
{
  // apply the unknown mob values
  m_name = "unknown";
  m_lastName = "";
  setNPC(SPAWN_NPC_UNKNOWN);

  // set what is known
  setPos(x, y, z);
  setDeltas(deltaX, deltaY, deltaZ);
  setHeading(heading, deltaHeading);
  setAnimation(animation);
  
  // initialize what isn't to 0
  setPetOwnerID(0);
  setLight(0);
  setGender(0);
  setDeity(0);
  setRace(0);
  setClassVal(0);
  setHP(0);
  setMaxHP(0);
  setLevel(0);
  setGuildID(0xffff);
  setGuildServerID(0);
  setGuildTag(NULL);
  for (int i = 0; i < tNumWearSlots; i++)
    setEquipment(i, SlotEmpty);
  setTypeflag(0);
  setGM(0);
  setIsMount(false);
  setIsMercenary(0);
  setIsAura(0);
  setNotUpdated(false);
  setConsidered(false);

  // Finally, note when this update ocurred
  updateLast();
}
		  
Spawn::Spawn(QDataStream& d, uint16_t id)
  : Item(tSpawn, id)
{
  // EQPoint = Point3D<int16_t> has a virtual destructor, so it carries a
  // vtable pointer at offset 0. The constructor chain above just set it
  // correctly for this binary. readRawData would overwrite it with the
  // pointer from the saving process (which may be a different binary with
  // different load addresses), corrupting all subsequent virtual dispatch.
  // Save the correct vtable pointer and restore it after the raw read.
  // Use char* to avoid -Wclass-memaccess on the non-trivially-copyable Spawn.
  char* raw = reinterpret_cast<char*>(this);
  void* vptr;
  std::memcpy(&vptr, raw, sizeof(vptr));
  d.readRawData(raw, sizeof(EQPoint));
  std::memcpy(raw, &vptr, sizeof(vptr));

  d.readRawData((char*)&m_lastUpdate,
		 (raw + sizeof(Item)) - (char*)&m_lastUpdate);
  d.readRawData((char*)&m_petOwnerID,
		 (raw + sizeof(Spawn)) - (char*)&m_petOwnerID);
  d >> m_name;
  d >> m_lastName;

  // calculate race/deity team info
  calcRaceTeam();
  calcDeityTeam();
  
  // don't trust old movement data (minimize walkoffs causing scaling)
  setDeltas(0, 0, 0);
  setHeading(0, 0);

  // even if it had been considered, mark it as not
  setConsidered(false);
}

Spawn::Spawn(Spawn* s, uint16_t id) : Item(tSpawn, id)
{
    setName(s->name());
    setLastName(s->lastName());
    Item::setPoint(s->x(), s->y(), s->z());
    setPetOwnerID(s->petOwnerID());
    setLight(s->light());
    setGender(s->gender());
    setDeity(s->deity());
    setRace(s->race());
    setClassVal(s->classVal());
    setClassMask(s->classMask());
    setHP(s->HP());
    setMaxHP(s->maxHP());
    setGuildID(s->guildID());
    setGuildServerID(s->guildServerID());
    setLevel(s->level());
    for (int i = 0; i <= tLastCoreWearSlot; i++)
    setEquipment(i, s->equipment(i));
    setEquipment(tUnknown1, SlotEmpty);
    setTypeflag(s->typeflag());
    setGM(s->gm());
    setNPC(s->NPC());
    setAnimation(s->animation());
    setDeltas(s->deltaX(), s->deltaY(), s->deltaZ());
    setHeading(s->heading(), s->deltaHeading());
    setConsidered(s->considered());
}

Spawn::~Spawn()
{
  // clear out the spawn track list
  qDeleteAll(m_spawnTrackList);
  m_spawnTrackList.clear();
}

void Spawn::update(const spawnStruct* s)
{
  setName(s->name);
  setLastName(s->lastName);
  setPos(s->x >> 3, s->y >> 3, s->z >> 3);
  setPetOwnerID(s->petOwnerId);
  setLight(s->light);
  setGender(s->gender);
  setDeity(s->deity);
  setRace(s->race);
  setClassVal(s->class_);
  setHP(s->curHp);
  setMaxHP(100); //the client sets this to 100
  setGuildID(s->guildID);
  setGuildServerID(s->guildServerID);
  setLevel(s->level);
  for (int i = 0; i <= tLastCoreWearSlot; i++)
    setEquipment(i, s->equipment[i]);
  setEquipment(tUnknown1, SlotEmpty);

  setTypeflag(s->bodytype);
  setGM(s->gm);
  setIsMount(calcIsMount(s->race, s->level));
  setIsMercenary(s->isMercenary);
  setIsAura(s->aura);

  // If it is a corpse with Unknown (NPC) religion.
  if ((s->NPC == SPAWN_PC_CORPSE) && (s->deity == DEITY_UNKNOWN))
    setNPC(SPAWN_NPC_CORPSE); // it's a dead monster
  else
    setNPC(s->NPC); // otherwise it is what it is

  setAnimation(s->animation);

  // only non corpses move
  if (!isCorpse())
  {
    setDeltas(s->deltaX >> 2, s->deltaY >> 2, s->deltaZ >> 2);
    setHeading(s->heading, s->deltaHeading);
  }
  else
  {
    setDeltas(0, 0, 0);
    setHeading(0, 0);
  }


  // just clear the considred flag since data would be outdated
  setConsidered(false);
  
  setNotUpdated(false);

  // finally, note when this update occurred.
  updateLast();
}

void Spawn::backfill(const spawnStruct* s)
{
  int i;

  // set the characteristics that probably haven't changed.
  setGender(s->gender);
  setDeity(s->deity);
  setRace(s->race);
  setClassVal(s->class_);

  // don't know how we'd find out if this changed, but it may, currently 
  setTypeflag(s->bodytype);
  // no-check
  setPetOwnerID(s->petOwnerId);

  // preserve the NPC setting, have they died since the spawn info
  if ((m_NPC == SPAWN_PC_CORPSE) ||
      (m_NPC == SPAWN_NPC_CORPSE))
  {
    // No hit points for the dead, sorry.  You're not undead yet.
    setHP(0);

    // Set whether this is a player or monster corpse
    if ((s->NPC == SPAWN_PLAYER) || (s->NPC == SPAWN_SELF))
      setNPC(SPAWN_PC_CORPSE); // Player corpse
    else
      setNPC(SPAWN_NPC_CORPSE); // Monster corpse
    
    //If it is a corpse with Unknown (NPC) religion.
    if (s->NPC == SPAWN_PC_CORPSE && s->deity == DEITY_UNKNOWN)
      setNPC(SPAWN_NPC_CORPSE);  //It is a dead monster.
  }  // only change NPC value if it's unknown
  else if (m_NPC == SPAWN_NPC_UNKNOWN)
  {
    // If it is a corpse with Unknown (NPC) religion.
    if ((s->NPC == SPAWN_PC_CORPSE) && (s->deity == DEITY_UNKNOWN))
      setNPC(SPAWN_NPC_CORPSE); // it's a dead monster
    else
      setNPC(s->NPC); // otherwise it is what it is
  }

  setName(s->name);
  setLastName(s->lastName);

  // if it's dead,  append the corpse designator and make sure it's not moving
  if (isCorpse())
  {
    m_name += Spawn_Corpse_Designator;
    setDeltas(0, 0, 0);
    setHeading(0, 0);
  }

  // only change unknown equipment
  for (i = 0; i <= tLastCoreWearSlot; i++)
    if (equipment(i).itemId == SlotEmpty.itemId)
      setEquipment(i, s->equipment[i]);

  // only change unknown or no light
  if (light() == 0)
    setLight(s->light);

  // only set the level if it's higher (not perfect I know)
  if (m_level < s->level)
    setLevel(s->level);

  // set guildID
  if (s->NPC == SPAWN_PLAYER || s->NPC == SPAWN_SELF)
  {
    setGuildID(s->guildID);
    setGuildServerID(s->guildServerID);
  }
  else
    setGuildID(0xffff);
}

void Spawn::killSpawn()
{
  setDeltas(0, 0, 0);
  setHeading(0, 0);
  setHP(0);
  setMaxHP(0);
  if ((NPC() == SPAWN_PLAYER) || (NPC() == SPAWN_SELF) || (NPC() == SPAWN_PC_CORPSE))
    setNPC(SPAWN_PC_CORPSE);
  else
    setNPC(SPAWN_NPC_CORPSE);

   //setName(realName() + Spawn_Corpse_Designator);
}

void Spawn::setPos(int16_t x, int16_t y, int16_t z,
		   bool walkpathrecord, size_t walkpathlength)
{
  Item::setPos(x, y, z);
 
  if (walkpathrecord)
  {
    uint32_t count = m_spawnTrackList.count();

    // if this is the self spawn and this is the first spawn point, 
    // don't add it to the track list
    if ((m_NPC == SPAWN_SELF) && (count == 0) && 
	(x == 0) && (y == 0) && (z == 0))
      return;

    // only insert if the change includes either an x or y change, not just z
    if ((count == 0) ||
	((m_spawnTrackList.last()->x() != x) ||
	 (m_spawnTrackList.last()->y() != y)))
    {
      // if the walk path length is limited, make sure not to exceed the limit
      if ((walkpathlength > 0) && 
              (count > 2) && (count > walkpathlength))
          delete m_spawnTrackList.takeFirst();

      // append the new entry to the end of the list
      m_spawnTrackList.append(new SpawnTrackPoint(x, y, z));
    }
  }
}

void Spawn::setDeltas(int16_t deltaX, int16_t deltaY, int16_t deltaZ)
{
  m_deltaX = deltaX; 
  m_deltaY = deltaY; 
  m_deltaZ = deltaZ; 
  m_cookedDeltaXFixPt = fixPtMulI(animationCoefficientFixPt, qFormat, 
				  m_deltaX);
  m_cookedDeltaYFixPt = fixPtMulI(animationCoefficientFixPt, qFormat, 
				  m_deltaY);
  m_cookedDeltaZFixPt = fixPtMulI(animationCoefficientFixPt, qFormat, 
				  m_deltaZ);
}

QString Spawn::lightName() const
{
  // a non-sparse array of lightnames
  static const char*  lightnames[] = 
  {
    "",    // 0  - No light 
    "CDL", // 1  - Candle 
    "TR",  // 2  - Torch 
    "TGS", // 3  - Tiny Glowing Skull 
    "SL",  // 4  - Small Lantern 
    "SoM", // 5  - Stein of Moggok 
    "LL",  // 6  - Large Lantern 
    "FL",  // 7  - Flameless lantern, Halo of Light 
    "GOS", // 8  - Globe of stars 
    "LG",  // 9  - Light Globe
    "LS",  // 10 - Lightstone, Burnt-out lightstone, wispstone 
    "GLS", // 11 - Greater lightstone 
    "FBE", // 12 - Fire Beatle Eye, Firefly Globe
    "CL",  // 13 - Coldlight 
  };

  // return light name from list if it's within range
  if (light() < (sizeof(lightnames) / sizeof (char*)))
    return lightnames[light()];
  else
    return QString::number(light());
}

QString Spawn::equipmentStr(uint8_t wearingSlot) const
{
  if (wearingSlot <= tLastMaterial)
    return print_material(equipment(wearingSlot).itemId);
  else if (wearingSlot <= tLastWeapon)
    return print_item(equipment(wearingSlot).itemId);
  else if (wearingSlot < tNumWearSlots)
    return print_material(equipment(wearingSlot).itemId);
  else
    return "";
}

QString Spawn::genderName() const
{
  if (m_gender == 0)
    return "Male";
  if (m_gender == 1)
    return "Female";
  return "Neuter";
}

QString Spawn::deityName() const
{
  // make sure people don't add to list without modifying code to support
  static const char*  deitynames[(DEITY_VEESHAN - DEITY_BERT) + 1] = 
  {
#include "deity.h"
  };

  // if it's an NPC, return quickly
  if (deity() == 0)
    return "NPC";

  // if agnostic return it
  if (deity() == DEITY_AGNOSTIC)
    return "Agnostic";
  
  // if it is a deity in the table, retrieve and return it
  if ((deity() >= DEITY_BERT) && (deity() <= DEITY_VEESHAN))
  {
    // subtract out lowest #'d deity in list to give 0 offset
    int deityIndex = deity() - DEITY_BERT;
    
    // return deity name
    return deitynames[deityIndex];
  }

  // all else failed, so return a number
  return QString::number(deity());
}

void Spawn::calcDeityTeam()
{
  m_deityTeam = DTEAM_OTHER;

  switch(deity())
  {
    //Good
  case DEITY_EROL:
  case DEITY_MITH:
  case DEITY_RODCET:
  case DEITY_QUELLIOUS:
  case DEITY_TUNARE:
    m_deityTeam = DTEAM_GOOD;
    break;
    //Neutral
  case DEITY_BRELL:
  case DEITY_BRISTLE:
  case DEITY_KARANA:
  case DEITY_PREXUS:
  case DEITY_SOLUSEK:
  case DEITY_TRIBUNAL:
  case DEITY_VEESHAN:
    m_deityTeam = DTEAM_NEUTRAL;
    break;
    //Evil
  case DEITY_BERT:
  case DEITY_CAZIC:
  case DEITY_INNY:
  case DEITY_RALLOS:
    m_deityTeam = DTEAM_EVIL;
    break;
  }
}

void Spawn::calcRaceTeam()
{
  m_raceTeam = RTEAM_OTHER;

  switch(race())
  {
  case 1: // Human
  case 2: // Barb
  case 3:  // Erudite
  case 130: // Vah Shir
    m_raceTeam = RTEAM_HUMAN;
    break;
    
  case 4: // Wood Elf
  case 5: // High Elf
  case 7: // Half Elf
    m_raceTeam = RTEAM_ELF;
    break;
    
  case 6:  // Dark Elf
  case 9:  // Troll
  case 10: // Ogre
  case 128: // Iksar
    m_raceTeam = RTEAM_DARK;
    break;
    
  case 8:  // Dwarf
  case 11:  // Halfling 
  case 12:  // Gnome 
    m_raceTeam = RTEAM_SHORT;
    break;
  }
}

QString Spawn::lastName() const
{
  return m_lastName;
}

int Spawn::level() const
{ 
  return m_level; 
}

uint16_t Spawn::deity() const 
{ 
  return m_deity; 
}

QString Spawn::cleanedName() const
{
  QString newName = name();
  newName.replace(QRegularExpression("[0-9]"), "");
  newName.replace(QRegularExpression("_"), " ");
  return newName;
}

QString Spawn::transformedName() const
{
  QString         temp = cleanedName();
  QString         article;
  
  if (temp.startsWith( "a " ))
  {
    temp = temp.mid( 2 );
    article = "a";
  }
  else if (temp.startsWith( "an " ))
  {
    temp = temp.mid( 3 );
    article = "an";
  }
  else if (temp.startsWith( "the " ))
  {
    temp = temp.mid( 4 );
    article = "the";
  }

  if (!article.isEmpty())
  {
    temp += ", ";
    temp += article;
  }

  return temp;
}

uint16_t Spawn::race() const
{
  return m_race;
}

QString Spawn::raceString() const
{
  // sparse array of racenames, some are NULL
  static const char*  racenames[] = 
  {
#include "races.h"
  };

  // assume no racename found
  const char *raceString = NULL;

  // retrieve pointer to race name
  if (race() < (sizeof(racenames) / sizeof (char*)))
    raceString = racenames[race()];

  // if race name exists, then return it, otherwise return a number string
  if (raceString != NULL)
    return raceString;
  else
    return QString::number(race());
}

uint8_t Spawn::classVal() const
{
  return m_class;
}

QString Spawn::classString() const
{
  return ::classString(classVal());
}

QString Spawn::info() const
{
  // Head, Chest, Arms, Waist, Gloves, Legs, Feet, Primary, Secondary
  static const char* locs[]={"H","C","A","W","G","L","F","1","2", "B"};
  int i;
  QString temp = "";
  
  // Add the light source to the list if it has one
  if (light())
    temp += QString("Light:") + lightName() + " ";

  // Worn stuff
  for (i = tFirstMaterial; i <= tLastMaterial ; i++)
    if (equipment(i).itemId != SlotEmpty.itemId)
      temp += QString(locs[i]) + ":" + print_material(equipment(i).itemId) + " ";
 
 // Worn weapons
  for (i = tFirstWeapon; i <= tLastWeapon; i++)
    if (equipment(i).itemId != SlotEmpty.itemId)
      temp += QString(locs[i]) + ":" + print_item(equipment(i).itemId) + " ";

  // Worn stuff -- Current best quess is that this may be material?
  i = tUnknown1;
  if (equipment(i).itemId != SlotEmpty.itemId)
    temp += QString(locs[i]) + ":" + print_material(equipment(i).itemId) + " "; 

#if 1 // print also as slot U1 (Unknown1) until we're positive
  if (equipment(i).itemId != SlotEmpty.itemId)
    temp += QString("U1:U") + QString::number(equipment(i).itemId, 16) + " ";
#endif

  return temp;
}

QString Spawn::typeString() const
{
  static const char*  typenames[] =
  {
#include "typenames.h"
  };

  if ( (typeflag() < (sizeof(typenames) / sizeof (char*))) && (typenames[typeflag()] != NULL) )
    return typenames[typeflag()];
  else
    return QString::number(typeflag());
}

QString Spawn::filterString() const
{
  QString name = transformedName();

  QString buff;
  buff = QString::asprintf("Name:%s:Level:%d:Race:%s:Class:%s:NPC:%d:X:%d:Y:%d:Z:%d:"
          "Light:%s:Deity:%s:RTeam:%d:DTeam:%d:Type:%s:LastName:%s:Guild:%s:Spawn:%s:"
          "Info:%s:",
          name.toUtf8().data(),
          level(),
          raceString().toUtf8().data(),
          classString().toUtf8().data(),
          ((NPC() == 10) ? 0 : NPC()),
          x(), y(), z(),
          lightName().toUtf8().data(),
          deityName().toUtf8().data(),
          raceTeam(),
          deityTeam(),
          typeString().toUtf8().data(),
          lastName().toUtf8().data(),
          guildTag().toUtf8().data(),
          spawnTimeStr ().replace (":", ".").toUtf8 ().data (),
          info().toUtf8().data()
          );

  if (gm())
    buff += QString("GM:") + QString::number(gm()) + ":";

  return buff;
}

QString Spawn::dumpString() const
{
  return QString("ID:") + QString::number(id()) 
    + ":Name:" + transformedName() 
    + ":LastName:" + lastName() 
    + ":Level:" + QString::number(level())
    + ":HP:" + QString::number(HP())
    + ":MaxHP:" + QString::number(maxHP())
    + ":Race:" + raceString()
    + ":Class:" + classString() 
    + ":NPC:" + QString::number(NPC())
    + ":X:" + QString::number(x()) 
    + ":Y:" + QString::number(y())
    + ":Z:" + QString::number(z()) 
    + ":Deity:" + deityName() 
    + ":RTeam:" + QString::number(raceTeam())
    + ":DTeam:" + QString::number(deityTeam())
    + ":Type:" + typeString()
    + ":Guild:" + guildTag()
    + ":Info:" + info()
    + ":FilterFlags:" + QString::number(filterFlags())
    + ":";
}


bool Spawn::approximatePosition(bool animating, 
				const QTime& curTime,
				EQPoint& newPos) const
{
  // default is the current location of the spawn
  newPos.setPoint(*this);

  // if animating calculate the current predicted position
  if (animating)
  {
    // get the amount of time since last update
    int msec = m_lastUpdate.msecsTo(curTime); 
    
    if (msec < 0) // if passed midnight, adjust time accordingly
      msec += 86400 * 1000;
    
    // if it's been over 90 seconds, then don't adjust position
    if (msec < (90 * 1000))
    {
      newPos.addPoint(fixPtMulII(m_cookedDeltaXFixPt, qFormat, msec),
		      fixPtMulII(m_cookedDeltaYFixPt, qFormat, msec),
		      fixPtMulII(m_cookedDeltaZFixPt, qFormat, msec));

      return true;
    }
    else
      return false;
  }

  return true;
}

void Spawn::saveSpawn(QDataStream& d)
{
  // dump spawn info
  // write out the raw spawn structure, skipping over the QStrings,
  // and SpawnTrackList (which can't be persisted in this fashion),
  // and the data we don't wan't copied over (heading/delta info).
  d.writeRawData((const char*)this, sizeof(EQPoint));
  d.writeRawData((const char*)&m_lastUpdate, 
		  ((char*)this + sizeof(Item)) 
		  - (char*)&m_lastUpdate);
  d.writeRawData((const char*)&m_petOwnerID,
		  ((char*)this + sizeof(Spawn)) - (char*)&m_petOwnerID);
  d << m_name;
  d << m_lastName;
}

/*static*/ bool Spawn::calcIsMount(uint32_t race, uint8_t level)
{
  // Some races can be both a mount and a non-mount NPC, so level==30 is
  // required as a tiebreak (mounts are always spawned at level 30).
  bool isMountRace;
  switch (race)
  {
	case 216: //Horse
	case 348: //Drogmore
	case 492: //Horse
	case 517: //Nightmare/Unicorn
	case 518: //Horse
	case 519: //Nightmare/Unicorn
	case 583: //Kirin
	case 584: //Puma
	case 594: //Worg
	case 597: //Cragslither
	case 598: //Wrulon
	case 623: //Feran Mount
	case 625: //Sokokar Mount
	case 631: //Hydra Mount
	case 652: //Cliknar Mount
	case 654: //Spider Mount
	case 655: //Bear Mount
	case 656: //Rat Mount
	case 657: //Sessiloid Mount
	case 671: //Topiary Lion Mount
	case 672: //Rot Dog Mount
	case 673: //Goral Mount
	case 674: //Selyrah Mount
	case 675: //Sclera Mount
	case 676: //Braxi Mount
	case 677: //Kangon Mount
	case 679: //Wurm Mount
	case 680: //Raptor Mount
	case 682: //Whirligig
	case 684: //Gnomish Rocket Pack
	case 709: //Skystrider
	case 720: //Flying Carpet
	case 721: //Carrier Hand
	case 751: //Dragonfly Mount
	case 752: //Ruishi Mount
	case 753: //Komodo Dragon Mount
	case 754: //Raptor Mount
	case 757: //Reindeer Mount
	case 794: //Prowler Mount
	case 795: //Wyvern Mount
	case 835: //Pridewing Mount
	case 836: //Direhound Mount
	case 850: //Horse
	case 853: //Chokidai Mount
	case 854: //Cockatrice Mount
	case 859: //Bear Mount
	case 861: //Snail Mount
	case 875: //Griffin Mount
	case 885: //Rabbit Mount
	case 898: //Owlbear Mount
	case 903: //Boar Mount
	case 911: //Sonic Wolf Mount
	case 912: //Shik'Nar Mount
	case 921: //Squirrel Mount
	case 923: //Phoenix Mount
	case 952: //Leviathan Mount
	case 953: //Scalewrought Mount
	case 954: //Otter Mount
	case 961: //Giant Bee Mount
      isMountRace = true;
	  break;
	default:
	  isMountRace = false;
	  break;
  }
  return (level == 30 && isMountRace);
}

//----------------------------------------------------------------------
// Door
Door::Door(const doorStruct* d)
  : Item(tDoors, d->doorId)
{
  m_NPC = SPAWN_DOOR;

  update(d);
}

Door::Door(uint32_t id, const QString& name, float x, float y, float z,
           float heading, uint32_t incline, uint32_t size, uint8_t openType,
           uint8_t state, uint8_t invertState,
           std::optional<uint32_t> zonePointId)
  : Item(tDoors, id)
  , m_zonePoint(zonePointId)
  , m_incline(incline)
  , m_size(size)
  , m_openType(openType)
  , m_state(state)
  , m_invertState(invertState)
{
  m_NPC = SPAWN_DOOR;
  m_name = name;
  m_semanticX = x;
  m_semanticY = y;
  m_semanticZ = z;
  setPos(int16_t(x), int16_t(y), int16_t(z));
  setHeading(int8_t(std::lrint(heading)));
  updateLast();
}

Door::~Door()
{
}

void Door::update(const doorStruct* d)
{
  QString temp;
  m_semanticX = d->x;
  m_semanticY = d->y;
  m_semanticZ = d->z;
  setPos((int16_t)(d->x), 
	 (int16_t)(d->y), 
	 (int16_t)(d->z * 10.0));
  setHeading((int8_t)lrintf(d->heading));
  m_name = QString::fromLatin1(d->name,
      int(qstrnlen(d->name, sizeof(d->name))));
  setZonePoint(d->zonePoint);
  m_incline = d->incline;
  m_size = d->size;
  m_openType = d->opentype;
  m_state = d->spawnstate;
  m_invertState = d->invertstate;
  updateLast();
}

QString Door::raceString() const
{
  return "Door";
}

QString Door::classString() const
{
  return "Thing";
}

QString Door::compatibilityName() const
{
  return seq::encode::compatibilityDoorName(m_name, id());
}

//----------------------------------------------------------------------
// Drop
Drop::Drop(const makeDropStruct* d, const QString& name)
  : Item(tDrop, d->dropId)
{
  m_NPC = SPAWN_DROP;

  update(d, name);
}

Drop::Drop(uint32_t id, const QString& actorDefinition, float x, float y,
           float z, std::optional<float> heading)
  : Item(tDrop, id)
{
  m_NPC = SPAWN_DROP;
  m_name = actorDefinition;
  m_idFile = actorDefinition;
  m_compatibilityName =
      seq::encode::compatibilityGroundItemName(actorDefinition);
  setPos(int16_t(x), int16_t(y), int16_t(z));
  if (heading) setHeading(int8_t(std::lrint(*heading)));
  updateLast();
}

Drop::~Drop()
{
}

void Drop::update(const makeDropStruct* d, const QString& name)
{
  int itemId;
  QString buff;

  // set the position
  setPos((int16_t)d->x, 
	 (int16_t)d->y, 
	 (int16_t)d->z);
  setHeading((int8_t)lrintf(d->heading));

  // set the drop specific info
  //setItemNr(d->itemNr);
  setIdFile(d->idFile);
  
  // calculate the drop name
  if (name.isEmpty())
  {
    if (d->idFile[0] == 'I' && d->idFile[1] == 'T')
    {
      buff = (d->idFile + 2);
      buff = buff.section('_', 0, 0, QString::SectionCaseInsensitiveSeps);
    }
    else
      buff = d->idFile;

    
    itemId = buff.toInt();
    
    buff = "Drop: ";
    if (itemId > 0) 
      buff.append(print_item(itemId));
    else 
      buff.append(d->idFile);
  }
  else
    buff = QString("Drop: '") + name + "'";
  
  // Keep the semantic actor definition in state. seq.v1 compatibility naming
  // belongs to the projector.
  m_name = m_idFile;
  m_compatibilityName = buff;

  updateLast();
}

QString Drop::compatibilityName() const
{
  return m_compatibilityName.isEmpty() ? m_name : m_compatibilityName;
}

QString Drop::raceString() const
{
  return "Drop";
}

QString Drop::classString() const 
{
  return "Thing";
}
