/*
 *  everquest.h
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
** Please be kind and remember to correctly re-order
** the values in here whenever you add a new item,
** thanks.  - Andon
*/

/*
** Structures used in the network layer of Everquest
*/

#ifndef EQSTRUCT_H
#define EQSTRUCT_H

#include "config.h"

#ifdef __FreeBSD__
#include <sys/types.h>
#else
#include <cstdint>
#endif

/*
** ShowEQ specific definitions
*/
// Statistical list defines
#define LIST_HP                         0
#define LIST_MANA                       1
#define LIST_STAM                       2
#define LIST_EXP                        3
#define LIST_FOOD                       4
#define LIST_WATR                       5
#define LIST_STR                        6
#define LIST_STA                        7
#define LIST_CHA                        8
#define LIST_DEX                        9
#define LIST_INT                        10
#define LIST_AGI                        11
#define LIST_WIS                        12
#define LIST_MR                         13
#define LIST_FR                         14
#define LIST_CR                         15
#define LIST_DR                         16
#define LIST_PR                         17
#define LIST_AC                         18
#define LIST_ALTEXP                     19
#define LIST_MAXLIST                    20 

/*
** MOB Spawn Type
*/
#define SPAWN_PLAYER                    0
#define SPAWN_NPC                       1
#define SPAWN_PC_CORPSE                 2
#define SPAWN_NPC_CORPSE                3
#define SPAWN_NPC_UNKNOWN               4
#define SPAWN_DROP                      6
#define SPAWN_DOOR                      7
#define SPAWN_SELF                      10

/* 
** Diety List
*/
#define DEITY_UNKNOWN                   0
#define DEITY_AGNOSTIC			396
#define DEITY_BRELL			202
#define DEITY_CAZIC			203
#define DEITY_EROL			204
#define DEITY_BRISTLE			205
#define DEITY_INNY			206
#define DEITY_KARANA			207
#define DEITY_MITH			208
#define DEITY_PREXUS			209
#define DEITY_QUELLIOUS			210
#define DEITY_RALLOS			211
#define DEITY_SOLUSEK			213
#define DEITY_TRIBUNAL			214
#define DEITY_TUNARE			215
#define DEITY_BERT			201	
#define DEITY_RODCET			212
#define DEITY_VEESHAN			216

//Team numbers for Deity teams
#define DTEAM_GOOD			1
#define DTEAM_NEUTRAL			2
#define DTEAM_EVIL			3
#define DTEAM_OTHER			5

//Team numbers for Race teams
#define RTEAM_HUMAN			1
#define RTEAM_ELF			2
#define RTEAM_DARK			3
#define RTEAM_SHORT			4
#define RTEAM_OTHER			5

//Maximum limits of certain types of data
#define MAX_KNOWN_SKILLS                100
#define MAX_SPELL_SLOTS                 16
#define MAX_KNOWN_LANGS                 32
#define MAX_SPELLBOOK_SLOTS             800
#define MAX_GROUP_MEMBERS               6
// Peer slots: the local player is filtered out of the GroupMgr roster, so
// only up to MAX_GROUP_MEMBERS-1 = 5 peers are ever tracked/emitted.
#define MAX_GROUP_PEERS                 (MAX_GROUP_MEMBERS - 1)
#define MAX_BUFFS                       42
#define MAX_GUILDS                      32768
#define MAX_AA                          300
#define MAX_BANDOLIERS                  20
#define MAX_POTIONS_IN_BELT             5
#define MAX_TRIBUTES                    5
#define MAX_DISCIPLINES                 300

//Item Flags
#define ITEM_NORMAL                     0x0000
#define ITEM_NORMAL1                    0x0031
#define ITEM_NORMAL2                    0x0036
#define ITEM_NORMAL3                    0x315f
#define ITEM_NORMAL4                    0x3336
#define ITEM_NORMAL5                    0x0032
#define ITEM_NORMAL6                    0x0033
#define ITEM_NORMAL7                    0x0034
#define ITEM_NORMAL8                    0x0039
#define ITEM_CONTAINER                  0x7900
#define ITEM_CONTAINER_PLAIN            0x7953
#define ITEM_BOOK                       0x7379
#define ITEM_VERSION                    0xFFFF

// Item spellId no spell value
#define ITEM_SPELLID_NOSPELL            0xffff

// Item Field Count
#define ITEM_FIELD_SEPERATOR_COUNT      117
#define ITEM_CMN_FIELD_SEPERATOR_COUNT  102

//Combat Flags
#define COMBAT_MISS					0
#define COMBAT_BLOCK					-1
#define COMBAT_PARRY					-2
#define COMBAT_RIPOSTE					-3
#define COMBAT_DODGE					-4

#define PLAYER_CLASSES     16
#define PLAYER_RACES       15

#define MAX_PLAYER_LEVEL    130

/*
** Item Packet Type
*/
enum ItemPacketType
{
  ItemPacketViewLink		= 0x00,
  ItemPacketMerchant		= 0x64,
  ItemPacketLoot		= 0x66,
  ItemPacketTrade		= 0x67,
  ItemPacketSummonItem		= 0x6a,
  ItemPacketWorldContainer      = 0x6b
};

/*
** Item types
*/
enum ItemType
{
  ItemTypeCommon	= 0,
  ItemTypeContainer	= 1,
  ItemTypeBook		= 2
};

/*
** Chat Colors
*/
enum ChatColor
{
  CC_Default               = 0,
  CC_DarkGrey              = 1,
  CC_DarkGreen             = 2,
  CC_DarkBlue              = 3,
  CC_Purple                = 5,
  CC_LightGrey             = 6,
  CC_User_Say              = 256,
  CC_User_Tell             = 257,
  CC_User_Group            = 258,
  CC_User_Guild            = 259,
  CC_User_OOC              = 260,
  CC_User_Auction          = 261,
  CC_User_Shout            = 262,
  CC_User_Emote            = 263,
  CC_User_Spells           = 264,
  CC_User_YouHitOther      = 265,
  CC_User_OtherHitYou      = 266,
  CC_User_YouMissOther     = 267,
  CC_User_OtherMissYou     = 268,
  CC_User_Duels            = 269,
  CC_User_Skills           = 270,
  CC_User_Disciplines      = 271,
  CC_User_Default          = 273,
  CC_User_MerchantOffer    = 275,
  CC_User_MerchantExchange = 276,
  CC_User_YourDeath        = 277,
  CC_User_OtherDeath       = 278,
  CC_User_OtherHitOther    = 279,
  CC_User_OtherMissOther   = 280,
  CC_User_Who              = 281,
  CC_User_Yell             = 282,
  CC_User_NonMelee         = 283,
  CC_User_SpellWornOff     = 284,
  CC_User_MoneySplit       = 285,
  CC_User_Loot             = 286,
  CC_User_Random           = 287,
  CC_User_OtherSpells      = 288,
  CC_User_SpellFailure     = 289,
  CC_User_ChatChannel      = 290,
  CC_User_Chat1            = 291,
  CC_User_Chat2            = 292,
  CC_User_Chat3            = 293,
  CC_User_Chat4            = 294,
  CC_User_Chat5            = 295,
  CC_User_Chat6            = 296,
  CC_User_Chat7            = 297,
  CC_User_Chat8            = 298,
  CC_User_Chat9            = 299,
  CC_User_Chat10           = 300,
  CC_User_MeleeCrit        = 301,
  CC_User_SpellCrit        = 302,
  CC_User_TooFarAway       = 303,
  CC_User_NPCRampage       = 304,
  CC_User_NPCFurry         = 305,
  CC_User_NPCEnrage        = 306,
  CC_User_EchoSay          = 307,
  CC_User_EchoTell         = 308,
  CC_User_EchoGroup        = 309,
  CC_User_EchoGuild        = 310,
  CC_User_EchoOOC          = 311,
  CC_User_EchoAuction      = 312,
  CC_User_EchoShout        = 313,
  CC_User_EchoEmote        = 314,
  CC_User_EchoChat1        = 315,
  CC_User_EchoChat2        = 316,
  CC_User_EchoChat3        = 317,
  CC_User_EchoChat4        = 318,
  CC_User_EchoChat5        = 319,
  CC_User_EchoChat6        = 320,
  CC_User_EchoChat7        = 321,
  CC_User_EchoChat8        = 322,
  CC_User_EchoChat9        = 323,
  CC_User_EchoChat10       = 324,
  CC_User_UnusedAtThisTime = 325,
  CC_User_ItemTags         = 326,
  CC_User_RaidSay          = 327,
  CC_User_MyPet            = 328,
  CC_User_DamageShield     = 329,
};

/*
** Group Update actions
*/
enum GroupUpdateAction
{
  GUA_Joined = 0,
  GUA_Left = 1,
  GUA_LastLeft = 6,
  GUA_FullGroupInfo = 7,
  GUA_MakeLeader = 8,
  GUA_Started = 9,
};

/**
 * Leadership AAs enum, used to index into leadershipAAs in charProfileStruct
 */
enum LeadershipAAIndex
{
  groupMarkNPC = 0,
  groupNPCHealth,
  groupDelegateMainAssist,
  groupDelegateMarkNPC,
  groupUnknown4,
  groupUnknown5,
  groupInspectBuffs,
  groupUnknown7,
  groupSpellAwareness,
  groupOffenseEnhancement,
  groupManaEnhancement,
  groupHealthEnhancement,
  groupHealthRegeneration,
  groupFindPathToPC,
  groupHealthOfTargetsTarget,
  groupUnknown15,
  raidMarkNPC,                                   //0x10
  raidNPCHealth,
  raidDelegateMainAssist,
  raidDelegateMarkNPC,
  raidUnknown4,
  raidUnknown5,
  raidUnknown6,
  raidSpellAwareness,
  raidOffenseEnhancement,
  raidManaEnhancement,
  raidHealthEnhancement,
  raidHealthRegeneration,
  raidFindPathToPC,
  raidHealthOfTargetsTarget,
  raidUnknown14,
  raidUnknown15,
  MAX_LEAD_AA //=32
};

/**
 * Recast timer types. Used as an off set to charProfileStruct timers.
 */
enum RecastTypes
{
  RecastTimer0 = 0,
  RecastTimer1,
  WeaponHealClickTimer,                          // 2
  MuramiteBaneNukeClickTimer,                    // 3
  RecastTimer4,
  DispellClickTimer,                             // 5 (also click heal orbs?)
  EpicTimer,                                     // 6
  OoWBPClickTimer,                               // 7
  VishQuestClassItemTimer,                       // 8
  HealPotionTimer,                               // 9
  RecastTimer10,
  RecastTimer11,
  RecastTimer12,
  RecastTimer13,
  RecastTimer14,
  RecastTimer15,
  RecastTimer16,
  RecastTimer17,
  RecastTimer18,
  ModRodTimer,                                   // 19
  MAX_RECAST_TYPES                               // 20
};


/*
** Compiler override to ensure
** byte aligned structures
*/
#pragma pack(1)

/*
**            Generic Structures used in specific
**                      structures below
*/

// OpCode stuff (all kinda silly, but until we stop including the OpCode everywhere)...
struct opCodeStruct
{
    int16_t opCode;

  // kinda silly -- this is required for us to be able to stuff them in a QValueList
  bool operator== ( const opCodeStruct t ) const
  {
    return ( opCode == t.opCode);
  }
  bool operator== ( uint16_t opCode2 ) const
  {
    return ( opCode == opCode2 );
  }
};

/**
 * Session request on a stream. This is sent by the client to initiate
 * a session with the zone or world server.
 * 
 * Size: 12 Octets
 */
struct SessionRequestStruct
{
/*0000*/ uint32_t unknown0000;
/*0004*/ uint32_t sessionId;
/*0008*/ uint32_t maxLength;
/*0012*/ uint8_t tag[10];  // "Everquest\0"
/*0022*/
};

/**
 * Session response on a stream. This is the server replying to a session
 * request with session information.
 *
 * Size: 19 Octets
 */
struct SessionResponseStruct
{
/*0000*/ uint32_t sessionId;
/*0004*/ uint32_t key;
/*0008*/ uint16_t unknown0008;
/*0010*/ uint8_t unknown0010;
/*0011*/ uint32_t maxLength;
/*0015*/ uint32_t unknown0015;
/*0019*/
};

/**
 * Session disconnect on a stream. This is the server telling the client to
 * close a stream.
 *
 * Size: 9 Octets
 */
struct SessionDisconnectStruct
{
/*0000*/ uint8_t unknown0000;
/*0001*/ uint32_t sessionId;
/*0005*/ uint8_t unknown[4];
/*0009*/
};

/* 
 * Used in charProfileStruct
 * Size: 4 Octets
 */
struct Color_Struct
{
  union
  {
    struct
    {
/*0000*/uint8_t blue;
/*0001*/uint8_t red;
/*0002*/uint8_t green;
/*0003*/uint8_t unknown0003;
    } rgb;
/*0000*/uint32_t color;
  };
};

/*
* Used in charProfileStruct. Buffs
* Length: 110 Octets
*/
// Post-2026-05-22 patch: the leading 12-byte unknown block + playerId
// (caster spawn id) is GONE. Field offsets compacted by 12; the trailing
// padding grew to keep the 110-byte slot size constant. Cracked from
// groupbuff-repro.vpk PlayerProfile (spell 0xdb 'Center' in slot 0,
// duration 227 ticks of 270 init, level 16) with the `float 1.0` modifier
// at +99 used as the per-slot anchor for the array-locator scan in
// ZoneMgr::zonePlayer. caster spawn id no longer reachable from here;
// SpellShell::buff (OP_Buff wire) is the new source for caster spawn id.
struct spellBuff
{
/*0000*/ int32_t duration;                       // Time remaining in ticks
/*0004*/ int32_t initDuration;                   // Buff length in ticks
/*0008*/ int8_t level;                           // Level of caster
/*0009*/ int32_t spellid;                        // Spell (unaligned!)
/*0013*/ int32_t effect;                         // Dmg-absorb amount on runes
/*0017*/ uint8_t unknown0017[81];                // 81 bytes of zeros + 0xff
                                                 // sentinel cells (slot-empty
                                                 // markers for sub-effects)
/*0098*/ float    modifier;                      // per-buff multiplier (always
                                                 // observed = 1.0); used by
                                                 // ZoneMgr::zonePlayer as the
                                                 // 110-byte-period anchor for
                                                 // locating the buff array
/*0102*/ uint8_t unknown0102[8];                 // tail padding
/*0110*/
};


/* 
 * Used in charProfileStruct
 * Size: 12 Octets
 */
struct AA_Array
{
/*000*/ uint32_t AA;
/*004*/ uint32_t value;
/*008*/ uint32_t unknown008;
/*012*/
};

/**
 * Used in charProfileStruct. An item inline in the stream, used in Bandolier and Potion Belt.
 * Size: 72 Octets 
 */
struct InlineItem
{
/*000*/ uint32_t itemId;
/*004*/ uint32_t icon;
/*008*/ char itemName[64];
/*072*/
};

/**
 * Used in charProfileStruct. Contents of a Bandolier.
 * Size: 320 Octets 
 */
struct BandolierStruct
{
/*000*/ char bandolierName[32];
/*032*/ InlineItem mainHand;
/*104*/ InlineItem offHand;
/*176*/ InlineItem range;
/*248*/ InlineItem ammo;
/*320*/
};

/**
 * Used in charProfileStruct. A tribute a player can have loaded.
 * Size: 8 Octets 
 */
struct TributeStruct
{
/*000*/ uint32_t tribute;
/*004*/ uint32_t rank;
/*008*/
};

/**
 * Used in charProfileStruct. A bind point.
 * Size: 20 Octets
 */
struct BindStruct
{
/*000*/ uint32_t zoneId;
/*004*/ float x;
/*008*/ float y;
/*012*/ float z;
/*016*/ float heading;
/*020*/
};

/*
 * Used in charProfileStruct. Visible Equipment.
 * Size: 20 Octets
 */
struct EquipStruct
{
/*00*/ uint32_t equip0;
/*04*/ uint32_t equip1;
/*08*/ uint32_t equip2;
/*12*/ uint32_t itemId;
/*16*/ uint32_t equip3;
/*20*/
};

/*
** Type:   Zone Change Request (before hand)
** Length: 100 Octets
** OpCode: ZoneChangeCode
*/
struct zoneChangeStruct
{
/*0000*/ char     name[64];	     	         // Character Name
/*0064*/ uint16_t zoneId;                        // zone Id
/*0066*/ uint16_t zoneInstance;                  // zone Instance
/*0068*/ uint8_t  unknown0068[8];                // unknown
/*0076*/ uint8_t  unknown0076[12];               // ***Placeholder (6/29/2005)
/*0088*/ uint8_t  unknown0088[4];                // HoT Beta (9/7/2010)
/*0092*/ uint8_t  unknown0092[8];                // RoF (12/12/2012)
/*0100*/
};

/*
** Type:  Request Zone Change (server asking the client to change zones)
** Size:  24 Octets
** OpCode: OP_RequestZoneChange
*/
struct requestZoneChangeStruct
{
/*0000*/ uint16_t zoneId;                        // Zone to change to
/*0002*/ uint16_t zoneInstance;                  // Instance to change to
/*0004*/ float x;                                // Zone in x coord in next zone
/*0008*/ float y;                                // Zone in y coord in next zone
/*0012*/ float z;                                // Zone in z coord in next zone
/*0016*/ float heading;                          // Zone in heading in next zone
/*0020*/ uint32_t unknown0020;                   // *** Placeholder
/*0024*/
};

/*
** Client Zone Entry struct
** Length: 88 Octets
** OpCode: ZoneEntryCode (when direction == client)
*/
struct ClientZoneEntryStruct
{
/*0000*/ uint32_t unknown0000;                   // ***Placeholder
/*0004*/ char     name[32];                      // Player firstname
/*0036*/ uint8_t  unknown0036[28];               // ***Placeholder
/*0064*/ uint32_t unknown0064[7];                // unknown
/*0092*/
};


/*
** New Zone Code
** Length: 936 Octets
** OpCode: NewZoneCode
*/
struct newZoneStruct
{
/*0000*/ char    name[64];                       // Character name
/*0064*/ char    shortName[32];                  // Zone Short Name (maybe longer?)
/*0096*/ char    unknown0096[96];
/*0192*/ char    longName[278];                  // Zone Long Name
/*0470*/ uint8_t ztype;                          // Zone type
/*0471*/ uint8_t fog_red[4];                     // Zone fog (red)
/*0475*/ uint8_t fog_green[4];                   // Zone fog (green)
/*0479*/ uint8_t fog_blue[4];                    // Zone fog (blue)
/*0483*/ uint8_t unknown0483[55];                // *** Placeholder
/*0538*/ uint8_t sky;                            // Zone sky
/*0539*/ uint8_t unknown0539[13];                // *** Placeholder
/*0551*/ float   zone_exp_multiplier;            // Experience Multiplier
/*0556*/ float   safe_y;                         // Zone Safe Y
/*0560*/ float   safe_x;                         // Zone Safe X
/*0564*/ float   safe_z;                         // Zone Safe Z
/*0568*/ float   unknown0568;                    // *** Placeholder
/*0572*/ float   unknown0572;                    // *** Placeholder
/*0576*/ float   underworld;                     // Underworld
/*0580*/ float   minclip;                        // Minimum view distance
/*0584*/ float   maxclip;                        // Maximum view distance
/*0588*/ uint8_t unknown0588[84];                // *** Placeholder
/*0672*/ char    zonefile[64];                   // Zone file name?
/*0736*/ uint8_t unknown0736[36];                // *** Placeholder (12/05/2006)
/*0772*/ uint8_t unknown0772[32];                // *** Placeholder (02/13/2007)
/*0804*/ uint8_t unknown0804[12];                // *** Placeholder 
/*0816*/ uint8_t unknown0816[4];                 // *** Placeholder (06/29/2005)
/*0820*/ uint8_t unknown0820[4];                 // *** Placeholder (09/13/2005)
/*0824*/ uint8_t unknown0824[4];                 // *** Placeholder (02/21/2006)
/*0828*/ uint8_t unknown0828[36];                // *** Placeholder (06/13/2006)
/*0864*/ uint8_t unknown0864[12];                // *** Placeholder (12/05/2006)
/*0876*/ uint8_t unknown0876[8];                 // *** Placeholder (02/13/2007)
/*0884*/ uint8_t unknown0884[4];                 // *** Placeholder (11/24/2007)
/*0888*/ uint8_t unknown0888[4];                 // *** Placeholder (01/17/2008)
/*0892*/ uint8_t unknown0892[4];                 // *** Placeholder (09/03/2008)
/*0896*/ uint8_t unknown0896[4];                 // *** Placeholder (10/07/2008)
/*0900*/ uint8_t unknown0900[8];                 // *** Placeholder (11/04/2009)
/*0908*/ uint8_t unknown0908[4];                 // *** Placeholder (12/15/2009)
/*0912*/ uint8_t unknown0912[4];                 // *** Placeholder (11/15/2011)
/*0916*/ uint8_t unknown0916[4];                 // *** Placeholder (04/29/2014)
/*0920*/ uint8_t unknown0920[4];                 // *** Placeholder (10/28/2014)
/*0924*/ uint8_t unknown0924[4];                 // *** Placeholder (03/16/2016)
/*0928*/ uint8_t unknown0928[8];		 // *** Placeholder (09/21/2016)
/*0936*/
};

/*
** Dynamic Zone Switch Info Struct
** Length: 32 Octets
** OpCode: DzSwitchInfo
*/
struct dzSwitchInfo
{
/*0000*/ uint32_t unknown0000;
/*0004*/ uint32_t show;                          // Show compass line
/*0008*/ uint16_t zoneID;
/*0010*/ uint16_t instanceID;
/*0012*/ uint32_t type;                          // if(type != 1 && type > 2 && type <= 5) { color = green; } else { color = pink; }
/*0016*/ uint32_t unknown0016;
/*0020*/ float    y;
/*0024*/ float    x;
/*0028*/ float    z;
/*0032*/
};

/*
** Dynamic Zone Info Struct
** Length: 212 Octets
** OpCode: DzInfo
*/
struct dzInfo
{
/*0000*/ uint32_t unknown0000;
/*0004*/ uint32_t unknown0004;
/*0008*/ uint8_t  newDZ;
/*0009*/ uint8_t  padding0009[3];
/*0012*/ uint32_t maxPlayers;
/*0016*/ char     dzName[128];                   // Instance name
/*0144*/ char     name[64];                      // Your player's name
/*0208*/ uint32_t unknown0208;
/*0212*/
};

/**
 * Player Profile. Common part of charProfileStruct shared between
 * shrouding and zoning profiles.
 *
 * NOTE: Offsets are kept in here relative to OP_PlayerProfile to ease in
 *       diagnosing changes in that struct.
 */
struct playerProfileStruct
{
/*00004*/ uint16_t  gender;                           // Player Gender - 0 Male, 1 Female
/*00008*/ uint32_t  race;                             // Player race
/*00012*/ uint32_t  class_;                           // Player class
/*00016*/ uint8_t   unknown00016[44];                 // ***Placeholder
/*00056*/ uint8_t   level;                            // Level of player
/*00057*/ uint8_t   level1;                           // Level of player (again?)
/*00058*/ uint8_t   unknown00058[2];                  // ***Placeholder
/*00060*/ BindStruct binds[5];                        // Bind points (primary is first)
/*00160*/ uint32_t  deity;                            // deity
/*00164*/ uint32_t  intoxication;                     // Alcohol level (in ticks till sober?)
/*00168*/ uint32_t  spellSlotRefresh[13];             // Refresh time (millis)
/*00220*/ uint8_t   haircolor;                        // Player hair color
/*00221*/ uint8_t   beardcolor;                       // Player beard color
/*00222*/ uint8_t   unknown00222[6];                  // *** Placeholder
/*00228*/ uint8_t   eyecolor1;                        // Player left eye color
/*00229*/ uint8_t   eyecolor2;                        // Player right eye color
/*00230*/ uint8_t   hairstyle;                        // Player hair style
/*00231*/ uint8_t   beard;                            // Player beard type
/*00232*/ uint8_t   unknown00232[4];                  // *** Placeholder
/*00236*/ union
         {
           struct
           {
               /*00236*/ EquipStruct equip_helmet;         // Equipment: Helmet visual
               /*00256*/ EquipStruct equip_chest;          // Equipment: Chest visual
               /*00276*/ EquipStruct equip_arms;           // Equipment: Arms visual
               /*00296*/ EquipStruct equip_bracers;        // Equipment: Wrist visual
               /*00316*/ EquipStruct equip_hands;          // Equipment: Hands visual
               /*00336*/ EquipStruct equip_legs;           // Equipment: Legs visual
               /*00356*/ EquipStruct equip_feet;           // Equipment: Boots visual
               /*00376*/ EquipStruct equip_primary;        // Equipment: Main visual
               /*00396*/ EquipStruct equip_secondary;      // Equipment: Off visual
           } equip;
            /*00416*/ EquipStruct equipment[22];
         };
/*00416*/ uint8_t   unknown00416[268];                // *** Placeholder
/*00416*/ uint8_t   unknowntmp[30];                   // *** Placeholder
/*00688*/ Color_Struct item_tint[9];                  // RR GG BB 00
/*00724*/ AA_Array  aa_array[MAX_AA];                 // AAs
/*04324*/ uint32_t  points;                           // Unspent Practice points
/*04328*/ uint32_t  MANA;                             // Current MANA
/*04332*/ uint32_t  curHp;                            // Current HP without +HP equipment
/*04336*/ uint32_t  STR;                              // Strength
/*04340*/ uint32_t  STA;                              // Stamina
/*04344*/ uint32_t  CHA;                              // Charisma
/*04348*/ uint32_t  DEX;                              // Dexterity
/*04352*/ uint32_t  INT;                              // Intelligence
/*04356*/ uint32_t  AGI;                              // Agility
/*04360*/ uint32_t  WIS;                              // Wisdom
/*04364*/ uint8_t   unknown04364[28];                 // *** Placeholder
/*04392*/ uint32_t  face;                             // Player face
/*04396*/ uint8_t   unknown04396[180];                // *** Placeholder
/*04576*/ int32_t   sSpellBook[729];                  // List of the Spells in spellbook
/*07492*/ int32_t   sMemSpells[MAX_SPELL_SLOTS];      // List of spells memorized
/*07540*/ uint8_t   unknown07540[17];                 // *** Placeholder
/*07585*/ uint32_t  platinum;                         // Platinum Pieces on player
/*07564*/ uint32_t  gold;                             // Gold Pieces on player
/*07568*/ uint32_t  silver;                           // Silver Pieces on player
/*07572*/ uint32_t  copper;                           // Copper Pieces on player
/*07576*/ uint32_t  platinum_cursor;                  // Platinum Pieces on cursor
/*07580*/ uint32_t  gold_cursor;                      // Gold Pieces on cursor
/*07584*/ uint32_t  silver_cursor;                    // Silver Pieces on cursor
/*07588*/ uint32_t  copper_cursor;                    // Copper Pieces on cursor
/*07592*/ uint32_t  skills[MAX_KNOWN_SKILLS];         // List of skills
/*07992*/ uint32_t  innateSkills[26];                 // innateSkills
/*08096*/ uint8_t   unknown08096[16];                 // *** Placeholder
/*08112*/ uint32_t  toxicity;                         // Potion Toxicity (15=too toxic, each potion adds 3)
/*08116*/ uint32_t  thirst;                           // Drink (ticks till next drink)
/*08120*/ uint32_t  hunger;                           // Food (ticks till next eat)
/*08124*/ uint8_t   unknown08124[20];                 // *** Placeholder
/*08140*/ spellBuff buffs[MAX_BUFFS];                 // Buffs currently on the player
/*11836*/ uint32_t  disciplines[MAX_DISCIPLINES];     // Known disciplines
/*12236*/ uint8_t   unknown12236[400];                // *** Placeholder
/*12636*/ uint32_t recastTimers[MAX_RECAST_TYPES];    // Timers (GMT of last use)
/*12716*/ uint8_t   unknown12716[480];                // *** Placeholder
/*13196*/ uint32_t  endurance;                        // Current endurance
/*13200*/ uint32_t  aa_spent;                         // Number of spent AA points (including glyphs)
/*13204*/ uint32_t  aa_assigned;                      // Number of points currently assigned to AAs
/*13208*/ uint32_t   unknown13208[4];                 // *** Placeholder
/*13224*/ uint32_t  aa_unspent;                       // Unspent AA points
/*13228*/ uint8_t   unknown13228[4];                  // *** Placeholder
/*13232*/ BandolierStruct bandoliers[MAX_BANDOLIERS]; // bandolier contents
/*19632*/ InlineItem potionBelt[MAX_POTIONS_IN_BELT]; // potion belt
/*19992*/ uint8_t   unknown19992[92];                 // *** Placeholder
/*20084*/
 };

/*
** Player Profile
** Length: Variable
** OpCode: OP_PlayerProfile
*/
struct charProfileStruct
{
/*00000*/ uint32_t  checksum;                         //
/*00004*/ playerProfileStruct profile;                // Profile
/*20084*/ char      name[64];                         // Name of player
/*20148*/ char      lastName[32];                     // Last name of player
/*20180*/ uint8_t   unknown20180[4];                  // *** Placeholder
/*20184*/ uint32_t  accountCreateDate;                // Date account was created
/*20188*/ int32_t   guildID;                          // guildID
/*20192*/ uint32_t  birthdayTime;                     // character birthday
/*20196*/ uint32_t  lastSaveTime;                     // character last save time
/*20200*/ uint32_t  timePlayedMin;                    // time character played
/*20204*/ uint8_t   unknown20204[4];                  // *** Placeholder
/*20208*/ uint8_t   pvp;                              // 1=pvp, 0=not pvp
/*20209*/ uint8_t   anon;                             // 2=roleplay, 1=anon, 0=not anon
/*20210*/ uint8_t   gm;                               // 0=no, 1=yes (guessing!)
/*20211*/ int8_t    guildstatus;                      // 0=member, 1=officer, 2=guildleader
/*20212*/ uint8_t   unknown20212[16];                 // *** Placeholder
/*20224*/ uint32_t  guildServerID;
/*20228*/ uint32_t  exp;                              // Current Experience
/*20232*/ uint8_t   unknown20232[12];                 // *** Placeholder
/*20244*/ uint8_t   languages[MAX_KNOWN_LANGS];       // List of languages
/*20270*/ uint8_t   unknown20272[6];                  // All 0x00 (language buffer?)
/*20276*/ float     y;                                // Players y position
/*20280*/ float     x;                                // Players x position
/*20284*/ float     z;                                // Players z position
/*20288*/ float     heading;                          // Players heading
/*20292*/ uint32_t  standState;                       // 0x64 = stand
/*20296*/ uint32_t  platinum_inventory;               // Platinum Pieces in Inventory
/*20300*/ uint32_t  gold_inventory;                   // Gold Pieces in Inventory
/*20304*/ uint32_t  silver_inventory;                 // Silver Pieces in Inventory
/*20308*/ uint32_t  copper_inventory;                 // Copper Pieces in Inventory
/*20296*/ uint32_t  platinum_bank;                    // Platinum Pieces in Bank
/*20300*/ uint32_t  gold_bank;                        // Gold Pieces in Bank
/*20304*/ uint32_t  silver_bank;                      // Silver Pieces in Bank
/*20308*/ uint32_t  copper_bank;                      // Copper Pieces in Bank
/*20312*/ uint32_t  platinum_shared;                  // Shared platinum pieces
/*20316*/ uint8_t   unknown20316[6220];               // *** Placeholder
/*26536*/ uint32_t  expansions;                       // Bitmask for expansions
/*26540*/ uint8_t   unknown22540[12];                 // *** Placeholder
/*26552*/ uint32_t  autosplit;                        // 0 = off, 1 = on
/*26556*/ uint8_t   unknown26556[16];                 // *** Placeholder
/*26572*/ uint16_t  zoneId;                           // see zones.h
/*26574*/ uint16_t  zoneInstance;                     // Instance id
/*26576*/ uint8_t   unknown26576[992];                // *** Placeholder
/*27568*/ uint32_t  leadAAActive;                     // 0 = leader AA off, 1 = leader AA on
/*27572*/ uint8_t   unknown27572[4];                  // *** Placeholder
/*27576*/ uint32_t  ldon_guk_points;                  // Earned GUK points
/*27580*/ uint32_t  ldon_mir_points;                  // Earned MIR points
/*27584*/ uint32_t  ldon_mmc_points;                  // Earned MMC points
/*27588*/ uint32_t  ldon_ruj_points;                  // Earned RUJ points
/*27592*/ uint32_t  ldon_tak_points;                  // Earned TAK points
/*27596*/ uint32_t  ldon_avail_points;                // Available LDON points
/*27600*/ uint8_t   unknown27600[144];                // *** Placeholder
/*27744*/ uint32_t  tributeTime;                      // Time remaining on tribute (millisecs)
/*27748*/ uint32_t  careerTribute;                    // Total favor points for this char
/*27752*/ uint32_t  unknown23536;                     // *** Placeholder
/*27756*/ uint32_t  currentTribute;                   // Current tribute points
/*27760*/ uint32_t  unknown23544;                     // *** Placeholder
/*27764*/ uint32_t  tributeActive;                    // 0 = off, 1=on
/*27768*/ TributeStruct tributes[MAX_TRIBUTES];       // Current tribute loadout
/*27808*/ uint8_t   unknown27808[100];                // *** Placeholder
/*27908*/ float     expGroupLeadAA;                   // Current group lead exp points
/*27912*/ float     expRaidLeadAA;                    // Current raid lead AA exp points
/*27916*/ uint32_t  groupLeadAAUnspent;               // Unspent group lead AA points
/*27920*/ uint32_t  unknown27920;                     // ***Placeholder
/*27924*/ uint32_t  raidLeadAAUnspent;                // Unspent raid lead AA points
/*27928*/ uint32_t  unknown27928;                     // ***Placeholder
/*27932*/ uint32_t  leadershipAAs[MAX_LEAD_AA];       // Leader AA ranks
/*28060*/ uint8_t   unknown28060[128];                // *** Placeholder
/*28188*/ uint32_t  airRemaining;                     // Air supply (seconds)
/*28192*/ uint8_t   unknown28192[4592];               // *** Placeholder
/*32784*/ uint32_t  expAA;                            // Exp earned in current AA point
/*32788*/ uint8_t   unknown32788[40];                 // *** Placeholder
/*32828*/ uint32_t  currentRadCrystals;               // Current count of radiant crystals
/*32832*/ uint32_t  careerRadCrystals;                // Total count of radiant crystals ever
/*32836*/ uint32_t  currentEbonCrystals;              // Current count of ebon crystals
/*32840*/ uint32_t  careerEbonCrystals;               // Total count of ebon crystals ever
/*32844*/ uint8_t   groupAutoconsent;                 // 0=off, 1=on
/*32845*/ uint8_t   raidAutoconsent;                  // 0=off, 1=on
/*32846*/ uint8_t   guildAutoconsent;                 // 0=off, 1=on
/*32847*/ uint8_t   unknown32487[5];                  // ***Placeholder (6/29/2005)
/*32852*/ uint32_t  showhelm;                         // 0=no, 1=yes
/*32856*/ uint8_t   unknown32856[1048];               // ***Placeholder (2/13/2007)
/*33904*/
 };

#if 0
// The following seem to be totally gone from charProfileStruct (9/13/05)
/*2384*/ char      title[32];                    // Current character title
/*2352*/ char      servername[32];               // server the char was created on
/*2416*/ char      suffix[32];                   // Current character suffix
#endif

#if 1
struct playerAAStruct {
/*    0 */  uint8_t unknown0;
  union {
    uint8_t unnamed[17];
    struct _named {  
/*    1 */  uint8_t innate_strength;
/*    2 */  uint8_t innate_stamina;
/*    3 */  uint8_t innate_agility;
/*    4 */  uint8_t innate_dexterity;
/*    5 */  uint8_t innate_intelligence;
/*    6 */  uint8_t innate_wisdom;
/*    7 */  uint8_t innate_charisma;
/*    8 */  uint8_t innate_fire_protection;
/*    9 */  uint8_t innate_cold_protection;
/*   10 */  uint8_t innate_magic_protection;
/*   11 */  uint8_t innate_poison_protection;
/*   12 */  uint8_t innate_disease_protection;
/*   13 */  uint8_t innate_run_speed;
/*   14 */  uint8_t innate_regeneration;
/*   15 */  uint8_t innate_metabolism;
/*   16 */  uint8_t innate_lung_capacity;
/*   17 */  uint8_t first_aid;
    } named;
  } general_skills;
  union {
    uint8_t unnamed[17];
    struct _named {
/*   18 */  uint8_t healing_adept;
/*   19 */  uint8_t healing_gift;
/*   20 */  uint8_t unknown20;
/*   21 */  uint8_t spell_casting_reinforcement;
/*   22 */  uint8_t mental_clarity;
/*   23 */  uint8_t spell_casting_fury;
/*   24 */  uint8_t chanelling_focus;
/*   25 */  uint8_t unknown25;
/*   26 */  uint8_t unknown26;
/*   27 */  uint8_t unknown27;
/*   28 */  uint8_t natural_durability;
/*   29 */  uint8_t natural_healing;
/*   30 */  uint8_t combat_fury;
/*   31 */  uint8_t fear_resistance;
/*   32 */  uint8_t finishing_blow;
/*   33 */  uint8_t combat_stability;
/*   34 */  uint8_t combat_agility;
    } named;
  } archetype_skills;
  union {
    uint8_t unnamed[93];
    struct _name {
/*   35 */  uint8_t mass_group_buff;             // All group-buff-casting classes(?)
// ===== Cleric =====
/*   36 */  uint8_t divine_resurrection;
/*   37 */  uint8_t innate_invis_to_undead;      // cleric, necromancer
/*   38 */  uint8_t celestial_regeneration;
/*   39 */  uint8_t bestow_divine_aura;
/*   40 */  uint8_t turn_undead;
/*   41 */  uint8_t purify_soul;
// ===== Druid =====
/*   42 */  uint8_t quick_evacuation;            // wizard, druid
/*   43 */  uint8_t exodus;                      // wizard, druid
/*   44 */  uint8_t quick_damage;                // wizard, druid
/*   45 */  uint8_t enhanced_root;               // druid
/*   46 */  uint8_t dire_charm;                  // enchanter, druid, necromancer
// ===== Shaman =====
/*   47 */  uint8_t cannibalization;
/*   48 */  uint8_t quick_buff;                  // shaman, enchanter
/*   49 */  uint8_t alchemy_mastery;
/*   50 */  uint8_t rabid_bear;
// ===== Wizard =====
/*   51 */  uint8_t mana_burn;
/*   52 */  uint8_t improved_familiar;
/*   53 */  uint8_t nexus_gate;
// ===== Enchanter  =====
/*   54 */  uint8_t unknown54;
/*   55 */  uint8_t permanent_illusion;
/*   56 */  uint8_t jewel_craft_mastery;
/*   57 */  uint8_t gather_mana;
// ===== Mage =====
/*   58 */  uint8_t mend_companion;              // mage, necromancer
/*   59 */  uint8_t quick_summoning;
/*   60 */  uint8_t frenzied_burnout;
/*   61 */  uint8_t elemental_form_fire;
/*   62 */  uint8_t elemental_form_water;
/*   63 */  uint8_t elemental_form_earth;
/*   64 */  uint8_t elemental_form_air;
/*   65 */  uint8_t improved_reclaim_energy;
/*   66 */  uint8_t turn_summoned;
/*   67 */  uint8_t elemental_pact;
// ===== Necromancer =====
/*   68 */  uint8_t life_burn;
/*   69 */  uint8_t dead_mesmerization;
/*   70 */  uint8_t fearstorm;
/*   71 */  uint8_t flesh_to_bone;
/*   72 */  uint8_t call_to_corpse;
// ===== Paladin =====
/*   73 */  uint8_t divine_stun;
/*   74 */  uint8_t improved_lay_of_hands;
/*   75 */  uint8_t slay_undead;
/*   76 */  uint8_t act_of_valor;
/*   77 */  uint8_t holy_steed;
/*   78 */  uint8_t fearless;                    // paladin, shadowknight

/*   79 */  uint8_t two_hand_bash;               // paladin, shadowknight
// ===== Ranger =====
/*   80 */  uint8_t innate_camouflage;           // ranger, druid
/*   81 */  uint8_t ambidexterity;               // all "dual-wield" users
/*   82 */  uint8_t archery_mastery;             // ranger
/*   83 */  uint8_t unknown83;
/*   84 */  uint8_t endless_quiver;              // ranger
// ===== Shadow Knight =====
/*   85 */  uint8_t unholy_steed;
/*   86 */  uint8_t improved_harm_touch;
/*   87 */  uint8_t leech_touch;
/*   88 */  uint8_t unknown88;
/*   89 */  uint8_t soul_abrasion;
// ===== Bard =====
/*   90 */  uint8_t instrument_mastery;
/*   91 */  uint8_t unknown91;
/*   92 */  uint8_t unknown92;
/*   93 */  uint8_t unknown93;
/*   94 */  uint8_t jam_fest;
/*   95 */  uint8_t unknown95;
/*   96 */  uint8_t unknown96;
// ===== Monk =====
/*   97 */  uint8_t critical_mend;
/*   98 */  uint8_t purify_body;
/*   99 */  uint8_t unknown99;
/*  100 */  uint8_t rapid_feign;
/*  101 */  uint8_t return_kick;
// ===== Rogue =====
/*  102 */  uint8_t escape;
/*  103 */  uint8_t poison_mastery;
/*  104 */  uint8_t double_riposte;              // all "riposte" users
/*  105 */  uint8_t unknown105;
/*  106 */  uint8_t unknown106;
/*  107 */  uint8_t purge_poison;                // rogue
// ===== Warrior =====
/*  108 */  uint8_t flurry;
/*  109 */  uint8_t rampage;
/*  110 */  uint8_t area_taunt;
/*  111 */  uint8_t warcry;
/*  112 */  uint8_t bandage_wound;
// ===== (Other) =====
/*  113 */  uint8_t spell_casting_reinforcement_mastery; // all "pure" casters
/*  114 */  uint8_t unknown114;
/*  115 */  uint8_t extended_notes;              // bard
/*  116 */  uint8_t dragon_punch;                // monk
/*  117 */  uint8_t strong_root;                 // wizard
/*  118 */  uint8_t singing_mastery;             // bard
/*  119 */  uint8_t body_and_mind_rejuvenation;  // paladin, ranger, bard
/*  120 */  uint8_t physical_enhancement;        // paladin, ranger, bard
/*  121 */  uint8_t adv_trap_negotiation;        // rogue, bard
/*  122 */  uint8_t acrobatics;                  // all "safe-fall" users
/*  123 */  uint8_t scribble_notes;              // bard
/*  124 */  uint8_t chaotic_stab;                // rogue
/*  125 */  uint8_t pet_discipline;              // all pet classes except enchanter
/*  126 */  uint8_t unknown126;
/*  127 */  uint8_t unknown127;
    } named;
  } class_skills;
};
#endif

/*
** Generic Spawn Struct 
** Length: Variable.
** Used in: 
**   dbSpawnStruct
**   petStruct
**   spawnShroudOther
**   spawnShroudSelf
*/

// Fixed-length struct that we'll fill with data from the variable-length packet,
// unnecessary fields removed, arranged in order with the packet.
struct spawnStruct
{
/*0000*/ char     name[64];
/*0000*/ uint32_t spawnId;
/*0000*/ uint8_t  level;
/*0000*/ uint8_t  NPC;                           // 0=player,1=npc,2=pc corpse,3=npc corpse
/*0000*/ union
         {
           struct
           {
             unsigned   gender:1;                // Gender (0=male, 1=female)
             unsigned   AFK:1;
             unsigned   sneak:1;
             unsigned   LFG:1;
             unsigned   padding6:1;
             unsigned   invis:1;
             unsigned   padding5:11;
             unsigned   gm:1;
             unsigned   anon:1;                  // 0=normal, 1=anon, 2=roleplay
             unsigned   padding4:1;
             unsigned   padding7:1;
             unsigned   padding3:1;
             unsigned   linkdead:1;
             unsigned   betabuffed:1;
             unsigned   padding2:2;
             unsigned   targetable:1;
             unsigned   targetcyclable:1;
             unsigned   padding1:2;
             unsigned   trader:1;
             unsigned   padding8:1;
           };
           int32_t miscData;
         };
/*0000*/ union
         {
	       struct
	       {
	    	 unsigned  buyer:1;
	    	 unsigned  offline:1;
	    	 unsigned  aura:1;
	    	 unsigned  padding9:1;
	    	 unsigned  hasTitle:1;
	    	 unsigned  hasSuffix:1;
	    	 unsigned  padding10:1;
	    	 unsigned  padding11:1;
	       };
	       uint8_t  otherData;
         };
/*0000*/ uint32_t race;
/*0000*/ uint8_t  charProperties;
/*0000*/ uint32_t bodytype;
/*0000*/ uint32_t bodytype2;
/*0000*/ uint8_t  curHp;
/*0000*/ uint8_t  holding;
/*0000*/ uint32_t deity;
/*0000*/ uint32_t guildID;
/*0000*/ uint32_t guildServerID;
/*0000*/ uint32_t guildstatus;                   // 0=member, 1=officer, 2=leader, -1=not guilded
/*0000*/ uint8_t  class_;
/*0000*/ uint8_t  state;                         // stand state 
/*0000*/ uint8_t  light;
/*0000*/ char     lastName[32];
/*0000*/ uint32_t petOwnerId;
         union
         {
           struct
           {

              signed   z:19;                              // z coord (3rd loc value)
              signed   animation:10;                      // current animation
              unsigned padding00:3;

              signed   y:19;                              // y coord (2nd loc value)
              unsigned heading:12;                        // heading
              unsigned padding01:1;

              signed   deltaY:13;                         // change in y
              unsigned pitch:12;                          // pitch (up/down heading)
              unsigned padding02:7;

              signed   x:19;                              // x coord (1st loc value)
              signed   deltaX:13;                         // change in x

              signed   deltaHeading:10;                   // change in heading
              signed   deltaZ:13;                         // change in z
              unsigned padding04:9;

           };
           int32_t posData[5];
         };

/*0000*/ union
         {
           struct
           {
               /*0000*/ Color_Struct color_helmet;    // Color of helmet item
               /*0000*/ Color_Struct color_chest;     // Color of chest item
               /*0000*/ Color_Struct color_arms;      // Color of arms item
               /*0000*/ Color_Struct color_bracers;   // Color of bracers item
               /*0000*/ Color_Struct color_hands;     // Color of hands item
               /*0000*/ Color_Struct color_legs;      // Color of legs item
               /*0000*/ Color_Struct color_feet;      // Color of feet item
               /*0000*/ Color_Struct color_primary;   // Color of primary item
               /*0000*/ Color_Struct color_secondary; // Color of secondary item
           } equipment_colors;
            /*0000*/ Color_Struct colors[9]; // Array elements correspond to struct equipment_colors above
         };
/*0000*/ union
         {
           struct
           {
               /*0000*/ EquipStruct equip_helmet;     // Equipment: Helmet visual
               /*0000*/ EquipStruct equip_chest;      // Equipment: Chest visual
               /*0000*/ EquipStruct equip_arms;       // Equipment: Arms visual
               /*0000*/ EquipStruct equip_bracers;    // Equipment: Wrist visual
               /*0000*/ EquipStruct equip_hands;      // Equipment: Hands visual
               /*0000*/ EquipStruct equip_legs;       // Equipment: Legs visual
               /*0000*/ EquipStruct equip_feet;       // Equipment: Boots visual
               /*0000*/ EquipStruct equip_primary;    // Equipment: Main visual
               /*0000*/ EquipStruct equip_secondary;  // Equipment: Off visual
           } equip;
            /*0000*/ EquipStruct equipment[9];
         };
/*0000*/ char title[32];
/*0000*/ char suffix[32];
/*0000*/ uint8_t isMercenary;
};

#if 0
// Basic structure of how the packet looks on the wire, for reference.
// March 16, 2012 eqgame.exe
struct spawnStruct
{
/*0000*/ char     name[0];
/*0000*/ uint32_t spawnId;
/*0000*/ uint8_t  level;
/*0000*/ float    unknown1;
/*0000*/ uint8_t  NPC;                           // 0=player,1=npc,2=pc corpse,3=npc corpse
/*0000*/ unsigned   padding7:1;
         unsigned   AFK:1;
         unsigned   sneak:1;
         unsigned   LFG:1;
         unsigned   padding6:1;
         unsigned   invis:1;
         unsigned   padding5:11;
         unsigned   gm:1;
         unsigned   anon:1;                      // 0=normal, 1=anon, 2=roleplay
         unsigned   padding4:1;
         unsigned   gender:1;                    // Gender (0=male, 1=female)
         unsigned   padding3:1;
         unsigned   linkdead:1;
         unsigned   betabuffed:1;
         unsigned   padding2:2;
         unsigned   targetable:1;
         unsigned   targetcyclable:1;
         unsigned   padding1:2;
         unsigned   trader:1;
         unsigned   buyer:1;
/*0000*/ uint8_t  otherData;                     // & 8 - has title, & 16 - has suffix, & 2 - auras, & 1 - it's a chest or untargetable
/*0000*/ uint32_t unknown3;
/*0000*/ uint32_t unknown4;
/*0000*/ uint32_t unknown5;
/*0000*/ uint8_t  facestyle;
/*0000*/ float    walkspeed;
/*0000*/ float    runspeed;
/*0000*/ uint32_t race;
/*0000*/ uint8_t  charProperties;                // for body types - value indicates how many properties are present
/*0000*/ uint32_t bodytype;
/*0000*/ uint32_t bodytype2;                     // this is only present if charProperties==2
                                                 // are there more than two possible properties?
/*0000*/ uint8_t  curHp;
/*0000*/ uint8_t  haircolor;
/*0000*/ uint8_t  facialhaircolor;
/*0000*/ uint8_t  eyecolor1;
/*0000*/ uint8_t  eyecolor2;
/*0000*/ uint8_t  hairstyle;
/*0000*/ uint8_t  facialhair;
/*0000*/ uint32_t heritage;
/*0000*/ uint32_t tattoo;
/*0000*/ uint32_t details;
/*0000*/ uint8_t  holding;
/*0000*/ uint32_t deity;
/*0000*/ uint32_t guildID;
/*0000*/ uint32_t guildstatus;                   // 0=member, 1=officer, 2=leader, -1=not guilded
/*0000*/ uint8_t  class_;
/*0000*/ uint8_t  PVP;
/*0000*/ uint8_t  state;                         // stand state 
/*0000*/ uint8_t  light;
/*0000*/ uint8_t  unknown7;
/*0000*/ uint8_t  unknown8;
/*0000*/ uint8_t  unknown9;
/*0000*/ uint8_t  unknown10;
/*0000*/ uint8_t  unknown11;
/*0000*/ char     lastName[0];
/*0000*/ uint32_t AARank;
/*0000*/ uint8_t  unknown12;
/*0000*/ uint32_t petOwnerId;
/*0000*/ uint8_t  unknown13;
/*0000*/ uint32_t unknown14;
/*0000*/ uint32_t unknown15;
/*0000*/ uint32_t unknown16;
/*0000*/ uint32_t unknown17;
/*0000*/ uint32_t unknown18;
/*0000*/ uint32_t unknown19;
/*0000*/ signed   padding0000:12;                // ***Placeholder
         signed   deltaX:13;                     // change in x
         signed   padding0005:7;                 // ***Placeholder
/*0000*/ signed   deltaHeading:10;               // change in heading
         signed   deltaY:13;                     // change in y
         signed   padding0006:9;                 // ***Placeholder
/*0000*/ signed   y:19;                          // y coord
         signed   animation:13;                  // animation
/*0000*/ unsigned heading:12;                    // heading
         signed   x:19;                          // x coord
         signed   padding0014:1;                 // ***Placeholder
/*0000*/ signed   z:19;                          // z coord
         signed   deltaZ:13;                     // change in z
// If not a valid player race (skip these if a valid player race)
/*0000*/ union
         {
           struct
           {
               /*0000*/ EquipStruct equip_helmet;     // Equipment: Helmet visual (maybe)
               /*0000*/ EquipStruct equip_primary;    // Equipment: Main visual
               /*0000*/ EquipStruct equip_secondary;  // Equipment: Off visual
           } equip;
           /*0000*/ EquipStruct equipment[3];
         };
// skip these bytes if not a valid player race - colors[9] and equipment[9]
/*0000*/ union
         {
           struct
           {
               /*0000*/ Color_Struct color_helmet;    // Color of helmet item
               /*0000*/ Color_Struct color_chest;     // Color of chest item
               /*0000*/ Color_Struct color_arms;      // Color of arms item
               /*0000*/ Color_Struct color_bracers;   // Color of bracers item
               /*0000*/ Color_Struct color_hands;     // Color of hands item
               /*0000*/ Color_Struct color_legs;      // Color of legs item
               /*0000*/ Color_Struct color_feet;      // Color of feet item
               /*0000*/ Color_Struct color_primary;   // Color of primary item
               /*0000*/ Color_Struct color_secondary; // Color of secondary item
           } equipment_colors;
            /*0000*/ Color_Struct colors[9]; // Array elements correspond to struct equipment_colors above
        } 
/*0000*/ union
         {
           struct
           {
               /*0000*/ EquipStruct equip_helmet;     // Equipment: Helmet visual
               /*0000*/ EquipStruct equip_chest;      // Equipment: Chest visual
               /*0000*/ EquipStruct equip_arms;       // Equipment: Arms visual
               /*0000*/ EquipStruct equip_bracers;    // Equipment: Wrist visual
               /*0000*/ EquipStruct equip_hands;      // Equipment: Hands visual
               /*0000*/ EquipStruct equip_legs;       // Equipment: Legs visual
               /*0000*/ EquipStruct equip_feet;       // Equipment: Boots visual
               /*0000*/ EquipStruct equip_primary;    // Equipment: Main visual
               /*0000*/ EquipStruct equip_secondary;  // Equipment: Off visual
           } equip;
           /*0000*/ EquipStruct equipment[9];
         };
/*0000*/ char title[0];                          // only read if(otherData & 8)
/*0000*/ char suffix[0];                         // only read if(otherData & 16)
/*0000*/ char unknown20[8];
/*0000*/ uint8_t isMercenary;
/*0000*/ char unknown21[54];
};
#endif


#if 0
// Old stuff from spawnStruct seq doesn't actually use at all..
//
/*0004*/ float    size;                          // Model size
/*0078*/ int8_t   aa_title;                      // 0=none, 1=general, 2=archtype, 3=class
/*0074*/ uint8_t  invis;                         // Invis (0=not, 1=invis)
/*0117*/ uint8_t  lfg;                           // 0=off, 1=lfg on
/*0196*/ uint8_t  afk;                           // 0=no, 1=afk
/*0207*/ int8_t   guildrank;                     // 0=normal, 1=officer, 2=leader
/*0213*/ uint8_t  face;	                         // Face id for players
/*0247*/ uint8_t  is_pet;                        // 0=no, 1=yes
/*0284*/ uint8_t  beardcolor;                    // Beard color
/*0500*/ uint8_t  showhelm;                      // 0=no, 1=yes
/*0501*/ uint8_t  helm;                          // Helm texture
/*0660*/ uint8_t  hairstyle;                     // Hair style
/*0090*/ uint8_t  eyecolor1;                     // Player's left eye color
/*0542*/ uint8_t  eyecolor2;                     // Left eye color
/*0547*/ uint8_t  haircolor;                     // Hair color
/*0574*/ uint8_t  is_npc;                        // 0=no, 1=yes
/*0575*/ uint8_t  findable;                      // 0=can't be found, 1=can be found
/*0728*/ uint8_t  beard;                         // Beard style (not totally, sure but maybe!)
/*0723*/ uint8_t  max_hp;                        // (name prolly wrong)takes on the value 100 for PCs, 100 or 110 for NPCs and 120 for PC corpses...
/*122*/ uint8_t pvp;                             // 0=Not pvp,1=pvp
union 
{
/*0091*/ int8_t equip_chest2;                    // Second place in packet for chest texture (usually 0xFF in live packets)
                                                 // Not sure why there are 2 of them, but it effects chest texture!
/*0091*/ int8_t mount_color;                     // drogmor: 0=white, 1=black, 2=green, 3=red
};
#endif

/*
** Server Zone Entry struct
** Length: 383 Octets
** OpCode: ZoneEntryCode (when direction == server)
*
*  This is just a spawnStruct for the player
*/
struct ServerZoneEntryStruct : public spawnStruct
{
};

/*
** Generic Door Struct
** Length: 136 Octets
** Used in: 
**    OP_SpawnDoor
**
*/

struct doorStruct
{
/*0000*/ char    name[32];                       // Filename of Door?
/*0032*/ float   y;                              // y loc
/*0036*/ float   x;                              // x loc
/*0040*/ float   z;                              // z loc
/*0044*/ float   heading;                        // heading
/*0048*/ uint32_t incline;                       // incline
/*0052*/ uint8_t unknown0048[20];		 // seems to be a copy of the previous 5 fields?
/*0072*/ uint32_t size;                          // size
/*0076*/ uint8_t unknown0056[4];                 // ***Placeholder
/*0080*/ uint8_t doorId;                         // door's id #
/*0081*/ uint8_t opentype;                       // open type
/*0082*/ uint8_t spawnstate;                     // spawn state
/*0083*/ uint8_t invertstate;                    // invert state
/*0084*/ uint32_t zonePoint;
/*0088*/ uint8_t unknown068[28];                 // ***Placeholder
/*0116*/ uint8_t unknown096[20];                 // ***Placeholder
/*0136*/
};

/*
** Drop Item On Ground
** Length: Variable
** OpCode: MakeDropCode
*/
// Note: Unknowns and other members removed that we don't use since we
//       now only fill this with data we need from the serialized packet
struct makeDropStruct
{
   uint32_t dropId;                              // DropID
   float    heading;                             // Heading
   float    z;                                   // Z Position
   float    x;                                   // X Position
   float    y;                                   // Y Position
   char     idFile[30];                          // ACTOR ID - The client reads 30 bytes from the packet
                                                 //          - 20100210 eqgame.exe in EQItemList::UnpackNetData
};

/*
** ZonePoint
** Length: 24 Octets
** Sent as part of zonePointsStruct
*/

struct zonePointStruct
{
  /*0000*/ uint32_t zoneTrigger;
  /*0004*/ float    y;
  /*0008*/ float    x;
  /*0012*/ float    z;
  /*0016*/ float    heading;
  /*0020*/ uint16_t zoneId;
  /*0022*/ uint16_t zoneInstance;
  /*0024*/
};

/*
** ZonePointsStruct
** Length: Variable
** OPCode: OP_SendZonePoints
*/
struct zonePointsStruct
{
  /*0000*/ uint32_t        count;
  /*0004*/ zonePointStruct zonePoints[0]; 
  /*0xxx*/ uint8_t         unknown0xxx[24];
  /*0yyy*/
};

/*
** Zone Server Info
** Length: 130 Octets
** OpCode: OP_ZoneServerInfo (world stream, S>C)
**
** Sent by the world server on a world->zone handoff with the FQDN/IP and
** UDP port of the target zone server. Confirmed 2026-05-06.
*/
struct zoneServerInfoStruct
{
/*0000*/ char     host[128];                     // NUL-terminated zone server hostname
/*0128*/ uint16_t port;                          // little-endian UDP port
/*0130*/
};

/*
** Time of Day
** Length: 8 Octets
** OpCode: TimeOfDayCode
*/
struct timeOfDayStruct
{
/*0000*/ uint8_t  hour;                          // Hour (1-24)
/*0001*/ uint8_t  minute;                        // Minute (0-59)
/*0002*/ uint8_t  day;                           // Day (1-28)
/*0003*/ uint8_t  month;                         // Month (1-12)
/*0004*/ uint16_t year;                          // Year
/*0006*/ uint16_t unknown0006;                   // Placeholder
/*0008*/
};

/*
** Inventory slot descriptor used by OP_MoveItem (and likely other
** modern item-flow opcodes). The legacy 4-byte slot id was expanded
** into a 12-byte descriptor that can address bag sub-slots and
** augments. Field meanings beyond `slot` are inferred from observed
** wire samples and are not yet fully resolved — see comment on
** moveItemStruct below for raw bytes.
** Length: 12 Octets
*/
struct invSlotStruct
{
/*000*/ uint32_t main;                           // 0 in observed samples (container/area selector?)
/*004*/ uint16_t slot;                           // top-level slot index
/*006*/ uint32_t sub_slot;                       // 0xffffffff in observed samples (bag sub-slot when nested?)
/*010*/ uint16_t aug;                            // varies per slot, often 0; aug index?
/*012*/
};

/*
** Move Item Struct
** Length: 28 Octets
** OpCode: OP_MoveItem (zone)
**
** Client-to-server slot move. The legacy 12-byte struct
** {from_slot, to_slot, number_in_stack} was replaced by two 12-byte
** invSlotStruct descriptors plus a 4-byte stack count.
**
** Sample wire bytes (from tests/replay/inventory.opcodestats.txt,
** captured during a slot-swap session):
**   00 00 00 00 0e 00 ff ff ff ff 00 be    <- from: slot 14
**   00 00 00 00 23 00 ff ff ff ff 00 00    <- to:   slot 35
**   00 00 00 00                            <- number_in_stack
*/
struct moveItemStruct
{
/*000*/ invSlotStruct from;                      // source slot
/*012*/ invSlotStruct to;                        // destination slot
/*024*/ uint32_t      number_in_stack;           // stack count moved (1 for non-stackable)
/*028*/
};

/*
** Item Packet Struct - Works on a variety of item operations
** Packet Types: See ItemPacketType enum
** Length: Variable
** OpCode: ItemCode
*/
struct itemPacketStruct
{
/*000*/	ItemPacketType	packetType;              // See ItemPacketType for more info.
/*004*/	char		serializedItem[0];
/*xx*/
};

/*
** Parsed Item Template Struct
** Length: 63+ Octets (variable tail follows for effects/augments)
**
** The post-name region of itemPacketStruct.serializedItem. Layout
** decoded by aligning OP_ItemPacket dumps of items with known stats:
** Crescent Gi (AC=8, end=10, STR=7, AGI=3, MR=4), Platinum Fire
** Wedding Ring (AC=5, HP=55), Outstanding Necklace/Mask/Band of
** Distant Echoes (rich stat sets including HP, mana, all 7 stats,
** all 5 standard resists, corruption, hp_regen, mana_regen).
**
** Offsets here are relative to the start of the post-name region,
** which begins right after the second null terminator of the
** duplicated item-name + lore-name strings. Use parseItemPacket()
** in itempacket.{h,cpp} to locate the post-name region in a raw
** OP_ItemPacket payload.
**
** Wire format gives BASE values; equipped augments contribute their
** own (separately-fetched) parsedItemTemplateStruct. Total stats are
** the sum of base + each filled aug.
*/
struct parsedItemTemplateStruct
{
/*000*/	uint32_t	format_const;            // observed = 63 (0x3f) in all dumps
/*004*/	uint32_t	reserved04;              // observed = 0
/*008*/	uint32_t	itemId;                  // server-side item id
/*012*/	uint32_t	weight_x10;              // weight × 10 (e.g. 30 = 3.0 lb)
/*016*/	uint32_t	flags;                   // size enum at byte[19] + bit-packed flags
/*020*/	uint32_t	slot_bitmask;            // EQ slot mask: 0x6000=Pri|Sec, 0x18000=Ring1|Ring2, 0x20=Neck, 0x20000=Chest
/*024*/	uint32_t	unk024;                  // varies per item; unresolved
/*028*/	uint32_t	unk028;                  // varies per item; unresolved
/*032*/	uint16_t	pad032;                  // observed = 0
/*034*/	int8_t  	resists[5];              // CR, DR, PR, MR, FR (cold/disease/poison/magic/fire)
/*039*/	int8_t  	corruption;              // corruption resist
/*040*/	int8_t  	stats[7];                // STR, STA, AGI, DEX, CHA, INT, WIS
/*047*/	int32_t 	hp;                      // HP modifier
/*051*/	int32_t 	mana;                    // mana modifier
/*055*/	int32_t 	endurance;               // endurance modifier
/*059*/	int32_t 	ac;                      // AC modifier
/*063*/	// effect/augment/proc fields follow; not yet decoded
};

/*
** Item Info Request Struct 
** Length: 80 Octets 
** OpCode: ItemInfoCode
*/
struct itemInfoReqStruct
{
/*000*/ uint32_t itemNr;                         // ItemNr 
/*004*/ uint32_t requestSeq;                     // Request sequence number
/*008*/ char     name[64];                       // Item name
/*072*/ uint8_t unknown0006[8];                  // Placeholder
/*080*/
};

/*
** Item Info Response Struct
** Length: Variable
** OpCode: ItemInfoCode
*/
struct itemInfoStruct
{
/*000*/	uint32_t	requestSeq;              // Corresponds to sequence # in req
/*004*/	char		serializedItem[0];
/*xxx*/
};

/*
** Simple Spawn Update
** Length: 14 Octets
** OpCode: MobUpdateCode
*/


struct spawnPositionUpdate 
{
/*0000*/ int16_t  spawnId;
/*0002*/ uint8_t unk1[2];		         // BSH 13 Apr 2011
/*0004*/ int64_t  y:19, z:19, u3:7,x:19;
         unsigned heading:12;
         signed unused2:4;
/*0014*/
};

/*
** Rename a spawn
** Length: 195 Octets
** OpCode: SpawnRename
*/
struct spawnRenameStruct
{
/*000*/	char        old_name[64];
/*064*/	char        old_name_again[64];	         //not sure what the difference is
/*128*/	char        new_name[64];
/*192*/	uint8_t     unknown0192[3];              // ***Placeholder
/*195*/
};

/*
** Illusion a spawn
** Length: 336 Octets
** OpCode: Illusion
*/
struct spawnIllusionStruct
{
/*0000*/ uint32_t   spawnId;                     // Spawn id of the target
/*0004*/ char       name[64];                    // Name of the target
/*0068*/ uint32_t   race;                        // New race
/*0072*/ uint8_t    gender;                      // New gender (0=male, 1=female)
/*0073*/ uint8_t    texture;                     // ???
/*0074*/ uint8_t    helm;                        // ???
/*0075*/ uint8_t    unknown0075;                 // ***Placeholder
/*0076*/ uint32_t   unknown0076;                 // ***Placeholder
/*0080*/ uint32_t   face;                        // New face
/*0084*/ uint8_t    unknown0084[248];            // ***Placeholder
/*0336*/
};

/**
 * Shroud spawn. For others shrouding, this has their spawnId and
 * spawnStruct.
 * 
 * Length: variable
 * OpCode: OP_Shroud
 */
struct spawnShroudOther
{
/*00000*/ uint32_t spawnId;                      // Spawn Id of the shrouded player
/*00004*/ uint16_t spawnStructSize;              // Size of spawnStruct (or start of)
/*00006*/ spawnStruct spawn;                     // Updated spawn struct for the player (variable length)
/*xxxxx*/
};

/**
 * Shroud yourself. For yourself shrouding, this has your spawnId, spawnStruct,
 * bits of your charProfileStruct (no checksum, then charProfile up till
 * but not including name), and an itemPlayerPacket for only items on the player
 * and not the bank.
 *
 * Length: Variable
 * OpCode: OP_Shroud
 */
struct spawnShroudSelf
{
/*00000*/ uint32_t spawnId;                      // Spawn Id of you
/*00004*/ uint16_t ppStart;                      // Start of playerProfile data (spawnId+ppStart+spawnStruct)
/*00004*/ spawnStruct spawn;                     // Updated spawnStruct for you (variable length)
/*xxxxx*/ playerProfileStruct profile;           // Character profile for shrouded char
/*xxxxx*/ uint8_t items;                         // Items on the player
/*xxxxx*/
};

/*
** Campfire spawn
** Length: 997
** OpCode: OP_ZoneEntry
*/
struct spawnCampfire
{
/*0000*/ spawnStruct spawn;
/*0532*/ uint8_t     unknown0532[465];
/*0997*/ 
};


/*
**                 ShowEQ Specific Structures
*/

/*
** DB spawn struct (adds zone spawn was in)
*/

struct dbSpawnStruct
{
/*0000*/ struct spawnStruct spawn;               // Spawn Information
/*0258*/ char   zoneName[40];                    // Zone Information
};

/*
** Pet spawn struct (pets pet and owner in one struct)
*/

struct petStruct
{
/*0000*/ struct spawnStruct owner;               // Pet Owner Information
/*0258*/ struct spawnStruct pet;                 // Pet Infromation
};

/*
** Server System Message
** Length: Variable Length
** OpCode: SysMsgCode
*/

struct sysMsgStruct
{
/*0000*/ char     message[0];                    // Variable length message
};

/*
** Emote text
** Length: Variable Text
** OpCode: emoteTextCode
*/

struct emoteTextStruct
{
/*0000*/ uint8_t  unknown0000[4];                // ***Placeholder
/*0004*/ char     text[0];                       // Emote `Text
};

/*
** Channel Message received or sent
** Length: Variable
** OpCode: ChannelMessageCode

This is how channelMessageStruct looks on the wire, for reference (8/12/09 eqgame.exe)

char            sender[0];                       // Variable length senders name 
char            target[0];                       // Variable length target characters name
uint32_t        unknown;
uint32_t        language;                        // Language
uint32_t        chanNum;                         // Channel
uint32_t        unknown;
uint8_t         unknown;
uint32_t        skillInLanguage;                 // senders skill in language
char            message[0];                      // Variable length message
uint8_t         unknown;
uint32_t        unknown;
uint32_t        unknown;
char            unknown[0];                      // Variable legth unknown text
uint8_t         unknown;
uint32_t        unknown;

*/

// This will get filled with data from the serialized packet
struct channelMessageStruct
{
/*0000*/ char     sender[64];
/*0064*/ char     target[64];
/*0136*/ uint32_t language;
/*0140*/ uint32_t chanNum;
/*0149*/ uint32_t skillInLanguage;
/*0153*/ char     message[2048];                   // Maximum message size according to eqgame.exe
};

/*
** Formatted text messages
** Length: Variable Text
** OpCode: emoteTextCode
*/

struct formattedMessageStruct
{
/*0000*/ uint8_t  unknown0000;
/*0001*/ uint8_t  unknown0001[4];                // ***Placeholder
/*0005*/ uint32_t messageFormat;                 // Indicates the message format
/*0009*/ ChatColor messageColor;                 // Message color
/*0013*/ char     messages[0];                   // no longer null terminated
						 // repeat (4-bytes len, string of len)
};

/*
** Simple text messages
** Length: 12 Octets
** OpCode: SimpleMessageCode
*/

struct simpleMessageStruct
{
/*0000*/ uint32_t  messageFormat;                // Indicates the message format
/*0004*/ ChatColor messageColor;                 // Message color
/*0008*/ uint32_t  unknown;                      // ***Placeholder
/*0012*/
};

/*
** Special Message Struct
** Length: Variable Text
** OPCode: OP_SpecialMesg
*/

struct specialMessageStruct
{
  /*0000*/ uint8_t   unknown0000[3];             // message style?
  /*0003*/ ChatColor messageColor;               // message color
  /*0007*/ uint16_t  target;                     // message target
  /*0009*/ uint16_t  padding;                    // padding
  /*0011*/ char      source[0];                  // message text
  /*0xxx*/ uint32_t  unknown0xxx[3];             //***Placeholder
  /*0yyy*/ char      message[0];                 // message text
};

/*
** Guild MOTD Struct
** Length: Variable Text
** OPCode: OP_GuildMOTD
*/
struct guildMOTDStruct
{
  /*0000*/ uint32_t unknown0000;                 //***Placeholder
  /*0004*/ uint32_t unknown0004;		 // added 11/16/2016
  /*0008*/ char     target[64];                  // motd target
  /*0072*/ char     sender[64];                  // motd "sender" (who set it)
  /*0136*/ uint32_t unknown0136;                 //***Placeholder
  /*0140*/ char     message[0];
};

/*
** Corpse location
** Length: 18 Octets
** OpCode: corpseLocCode
*/

struct corpseLocStruct
{
/*0000*/ uint32_t spawnId;
/*0004*/ float    x;
/*0008*/ float    y;
/*0012*/ float    z;
/*0018*/
};

/*
** Consent request
** Length: Variable by length of the name of the consentee
*/

struct consentRequestStruct
{
/*0000*/ char consentee[0];                      // Name of player who was consented
};

/*
** Consent Response
** Length: 193 Octets
*/

struct consentResponseStruct
{
/*0000*/ char consentee[64];                     // Name of player who was consented
/*0064*/ char consenter[64];                     // Name of player who consented
/*0128*/ uint8_t allow;                          // 00 = deny, 01 = allow
/*0129*/ char corpseZoneName[64];                // Zone where the corpse is
/*0193*/
};

/*
** Grouping Information
** Length: 456 Octets
** OpCode: OP_GroupUpdate
*/


struct groupUpdateStruct
{
/*0000*/ int32_t  action;                        // Group update action
/*0004*/ char     yourname[64];                  // Group Member Names
/*0068*/ char     membername[64];                // Group leader name
/*0132*/ uint8_t  unknown0132[324];              // ***Placeholder
/*456*/
};


/*
** DEPRECATED
** Grouping Information
** Length: 768 Octets
** OpCode: OP_GroupUpdate
*/

struct groupFullUpdateStruct
{
/*0000*/ int32_t  action;
/*0004*/ char     membernames[MAX_GROUP_MEMBERS][64]; // Group Member Names
/*0388*/ char     leader[64];                         // Group leader Name
/*0452*/ char     unknown0452[316];                   // ***Placeholder
/*0768*/
};

/*
** Grouping Invite
** Length 152 Octets
** Opcode OP_GroupInvite
*/

struct groupInviteStruct
{
/*0000*/ char     invitee[64];                   // Invitee's Name
/*0064*/ char     inviter[64];                   // Inviter's Name
/*0128*/ uint8_t  unknown0128[24];               // ***Placeholder
/*0152*/
};

/*
** Grouping Invite Answer - Decline
** Length 156 Octets
** Opcode GroupDeclineCode
*/

struct groupDeclineStruct
{
/*0000*/ char     yourname[64];                  // Player Name
/*0064*/ char     membername[64];                // Invited Member Name
/*0128*/ uint8_t  unknown0128[24];               // ***Placeholder
/*0152*/ uint8_t  reason;                        // Already in Group = 1, Declined Invite = 3
/*0153*/ uint8_t  unknown0149[3];                // ***Placeholder
/*0156*/
};

/*
** Grouping Invite Answer - Accept 
** Length 152 Octets
** Opcode OP_GroupFollow
*/

struct groupFollowStruct
{
/*0000*/ char     unknown0000[64];               // ***Placeholder (zeros)
/*0064*/ char     invitee[64];                   // Invitee's Member Name
/*0128*/ uint8_t  unknown0128[4];                // ***Placeholder
/*0132*/ uint32_t level;                         // Invitee's level
/*0136*/ uint8_t  unknown0136[16];               // ***Placeholder (zeros)
/*0152*/
};

/*
** Group Disbanding
** Length 152 Octets
** Opcode 
*/

struct groupDisbandStruct
{
/*0000*/ char     yourname[64];                  // Player Name
/*0064*/ char     membername[64];                // Invited Member Name
/*0128*/ uint8_t  unknown0128[24];               // ***Placeholder
/*0152*/
};

/*
** Group Leader Change
** Length 152 Octets
** Opcode OP_GroupLeader
*/

struct groupLeaderChangeStruct
{
/*0000*/ char     unknown0000[64];               // ***Placeholder
/*0064*/ char     membername[64];                // Invited Member Name
/*0128*/ uint8_t  unknown0128[24];               // ***Placeholder
/*0152*/
};

/*
** Delete Self
** Length: 4 Octets
** OpCode: OP_DeleteSpawn
*/

struct deleteSpawnStruct
{
/*0000*/ uint32_t spawnId;                       // Spawn ID to delete
/*0004*/
};

/*
** Remove Spawn
** Length: 5 Octets
** OpCode: OP_RemoveSpawn
*/

struct removeSpawnStruct
{
/*0000*/ uint32_t spawnId;                       // Spawn ID to delete
/*0004*/ uint8_t  removeSpawn;                   // 0 if spawn is not in your update radius
/*0005*/
};

/*
** Remove Drop Item On Ground
** Length: 12 Octets
** OpCode: RemDropCode
*/

struct remDropStruct
{
/*0000*/ uint16_t dropId;                        // ID assigned to drop
/*0002*/ uint8_t  unknown0002[2];                // ***Placeholder
/*0004*/ uint16_t spawnId;                       // ID of player picking item up
/*0006*/ uint8_t  unknown0006[2];                // ***Placeholder
/*0008*/ uint8_t  unknown0008[4];                // added 8/21/2019
/*0012*/
};

/*
** Consider Struct
** Length: 32 Octets
** OpCode: considerCode
*/

struct considerStruct
{
/*0000*/ uint32_t playerid;                      // PlayerID
/*0004*/ uint32_t targetid;                      // TargetID
/*0008*/ int32_t  faction;                       // Faction
/*0012*/ int32_t  level;                         // Level
/*0016*/ int32_t  unknown0016;                   // unknown
/*0020*/ int32_t  unknown0020;                   // unknown (rarity?)
/*0024*/ int32_t  unknown0024;                   // unknown
/*0028*/ int32_t  unknown0028;                   // unknown
/*0032*/
};

/*
** Spell Casted On
** Length: 36 Octets
** OpCode: castOnCode
*/

struct castOnStruct
{
/*0000*/ uint16_t targetId;                      // Target ID
/*0002*/ uint8_t  unknown0002[2];                // ***Placeholder
/*0004*/ int16_t  sourceId;                      // ***Source ID
/*0006*/ uint8_t  unknown0006[2];                // ***Placeholder
/*0008*/ uint8_t  unknown0008[24];               // might be some spell info?
/*0032*/ uint16_t spellId;                       // Spell Id
/*0034*/ uint8_t  unknown0034[2];                // ***Placeholder
/*0036*/
};

/*
** Spawn Death Blow
** Length: 40 Octets
** OpCode: NewCorpseCode
*/

struct newCorpseStruct
{
/*0000*/ uint32_t spawnId;                       // Id of spawn that died
/*0004*/ uint32_t killerId;                      // Killer
/*0008*/ uint32_t corpseid;                      // corpses id
/*0012*/ int32_t  type;                          // corpse type?  
/*0016*/ uint32_t spellId;                       // ID of Spell
/*0020*/ uint16_t zoneId;                        // Bind zone id
/*0022*/ uint16_t zoneInstance;                  // Bind zone instance
/*0024*/ uint32_t damage;                        // Damage
/*0028*/ uint8_t  unknown0028[12];                // ***Placeholder
/*0040*/
};

/**
** Environmental damage (lava, falls)
** Length: 46 Octets
*/

struct environmentDamageStruct
{
/*0000*/ uint32_t spawnId;                       // Who is taking the damage
/*0004*/ uint8_t unknown0004[4];
/*0008*/ uint32_t damage;                        // how much damage?
/*0012*/ uint8_t unknown0010[16];
/*0028*/ uint8_t type;                           // Damage type. FC = fall. FA = lava.
/*0029*/ uint8_t unknown0029[17];
/*0046*/
};

/*
** Money Loot
** Length: 24 Octets
** OpCode: MoneyOnCorpseCode
*/

struct moneyOnCorpseStruct
{
/*0000*/ uint8_t  unknown0000[8];                // ***Placeholder
/*0008*/ uint32_t platinum;                      // Platinum Pieces
/*0012*/ uint32_t gold;                          // Gold Pieces
/*0016*/ uint32_t silver;                        // Silver Pieces
/*0020*/ uint32_t copper;                        // Copper Pieces
/*0024*/
};

/*
** Stamina
** Length: 8 Octets
** OpCode: staminaCode
*/

struct staminaStruct
{
/*0000*/ uint32_t food;                          // Hunger, in ticks till next eat
/*0004*/ uint32_t water;                         // Thirst, in ticks till next eat
/*0008*/
};

/*
** Activate Ability (EQ Legends)
** Length: 4 Octets
** OpCode: OP_Stance (0x0fab) / OP_Invocation (0x3b12)
**
** S>C echo when a swappable stance or invocation is activated. One 32-bit
** ability id (stable client enum); the opcode picks stance vs invocation.
*/
struct activateAbilityStruct
{
/*0000*/ uint32_t abilityId;                     // active stance/invocation ability id
/*0004*/
};

/*
** Endurance Update (run/jump bar)
** Length: 10 Octets
** OpCode: OP_EndUpdate
**
** Server-pushed updates to the player's endurance bar — what the EQ
** UI renders below the mana bar in yellow. Resolved 2026-04-28 from
** the --opcode-stats sweep (jump-drain capture, n=139, dominant
** 10-byte S>C unknown). Distinct from OP_Stamina which carries
** hunger/thirst.
*/

struct endUpdateStruct
{
/*0000*/ uint16_t spawn_id;                      // owner of the endurance bar (player)
/*0002*/ uint32_t cur;                           // current endurance
/*0006*/ uint32_t max;                           // max endurance at current level
/*0010*/
};

/*
** Mana Update
** Length: 10 Octets
** OpCode: OP_ManaUpdate
**
** Per-tick mana regen update for the local player. Wire shape is
** identical to endUpdateStruct (spawn_id u16, cur u32, max u32);
** distinct opcode because EQ paints mana on its own bar. Resolved
** 2026-05-07 from the --opcode-stats sweep on inventory-mana.vpk
** (n=28, dominant 10-byte S>C unknown, monotonic +10/tick).
*/

struct manaUpdateStruct
{
/*0000*/ uint16_t spawn_id;                      // owner of the mana bar (player)
/*0002*/ uint32_t cur;                           // current mana
/*0006*/ uint32_t max;                           // max mana at current level
/*0010*/
};

/*
** Battle Code
** Length: 30 Octets
** OpCode: ActionCode
*/

// This can be used to gather info on spells cast on us
struct action2Struct
{
/*0000*/ uint16_t target;                        // Target ID
/*0002*/ uint16_t source;                        // Source ID
/*0004*/ uint8_t  unknown0004[4];
/*0008*/ int32_t  damage;
/*0012*/ int8_t   unknown0012[8];
/*0020*/ int32_t  spell;                         // SpellID
/*0024*/ uint8_t  uknown0024[16];
/*0040*/ uint8_t  type;                          // Bash, kick, cast, etc.
/*0041*/ uint8_t  unknown0042[7];
/*0048*/
};

// This can be used to gather info on spells cast on us
struct actionStruct
{
/*0000*/ uint16_t target;                        // Target ID
/*0002*/ uint16_t source;                        // SourceID
/*0004*/ uint16_t spell;                         // SpellID (unsigned: modern IDs > 32767 sign-extend if int16)
/*0006*/ uint8_t  unknown0006[6];
/*0012*/ uint8_t  level;                         // Caster level
/*0013*/ uint8_t  unknown0013[43];               // ***Placeholder
/*0056*/ uint8_t  type;                          // Casts, Falls, Bashes, etc...
/*0057*/ uint8_t  unknown0057[7];
/*0064*/
};

// Starting with 2/21/2006, OP_Actions seem to come in pairs, duplicating
// themselves, with the second one with slightly more information. Maybe this
// has to do with buff blocking??
struct actionAltStruct
{
/*0000*/ uint16_t target;                        // Target ID
/*0002*/ uint16_t source;                        // SourceID
/*0004*/ uint16_t spell;                         // SpellID (unsigned: modern IDs > 32767 sign-extend if int16)
/*0006*/ uint8_t  unknown0006[6];
/*0012*/ uint8_t  level;                         // Caster level
/*0013*/ uint8_t  unknown0013[43];               // ***Placeholder
/*0056*/ uint8_t  type;                          // Casts, Falls, Bashes, etc...
/*0057*/ uint8_t  unknown0057[31];
/*0088*/
};

/*
** client changes target struct
** Length: 4 Octets
** OpCode: clientTargetCode
*/

struct clientTargetStruct
{
/*0000*/ uint32_t newTarget;                     // Target ID
/*0004*/ 
};

/*
** Info sent when you start to cast a spell
** Length: 39 Octets
** OpCode: StartCastCode
*/

struct startCastStruct 
{
/*0000*/ int32_t  slot;                          // Spell slot
/*0004*/ uint32_t spellId;                       // Spell ID
/*0008*/ uint8_t  unknown0008[10];               // ***Placeholder
/*0018*/ uint32_t targetId;                      // The current selected target
/*0022*/ uint8_t  unknown0022[2];                // ***Placeholder
/*0024*/ uint8_t  unknown0024[2];                // ***Placeholder
/*0026*/ uint8_t  unknown0026[13];	             // ***Placeholder (4/7/2009)
/*0039*/
};

/*
** New Mana Amount
** Length: 20 Octets
** OpCode: manaDecrementCode
*/

struct manaDecrementStruct
{
/*0000*/ int32_t newMana;                        // New (current) mana amount
/*0004*/ int32_t curEndurance;                   // Current endurance, NOT max mana — confirmed 2026-05-26 by cross-ref against OP_EndUpdate cur (monk fixture). EQ never sends max mana on the wire.
/*0008*/ int32_t spellId;                        // Spell or combat-ability id just used
/*0012*/ uint8_t unknown0012[4];
/*0016*/ uint8_t unknown0016[4];
/*0020*/
};

/*
** Special Message
** Length: 4 Octets + Variable Text Length
** OpCode: SPMesgCode
*/
struct spMesgStruct
{
/*0000*/ int32_t msgType;                        // Type of message
/*0004*/ char    message[0];                     // Message, followed by four Octets?
};

/*
** Spell Fade Struct
** Length: Variable length
** OpCode: SpellFadedCode
*/
struct spellFadedStruct
{
/*0000*/ uint32_t color;                         // color of the spell fade message
/*0004*/ char     message[0];                    // fade message
/*0???*/ uint8_t  paddingXXX[3];                 // always 0's 
};

/*
** Spell Begin Cast Broadcast
** Length: 15 Octets
** OpCode: BeginCastCode
*/
struct beginCastStruct
{
/*0000*/ uint32_t spellId;                       // Spell being cast
/*0004*/ uint16_t spawnId;                       // Caster's spawn ID
/*0006*/ uint16_t castTime;                      // Cast time in milliseconds
/*0008*/ uint8_t  unknown0008[7];                // ***Placeholder
/*0015*/
};

/*
** Spell Action Struct
** Length: 16 Octets
** OpCode: MemSpellCode
*/

struct memSpellStruct
{
/*0000*/ uint32_t slotId;    // gem slot (0-based)
/*0004*/ uint32_t spellId;   // spell id
/*0008*/ int16_t  action;    // 1=begin, 2=complete (C>S+S>C; "You have finished memorizing"), 3=zone-in sync
/*0010*/ int16_t  unknown10; // always 0
/*0012*/ uint8_t  unknown12[4];
/*0016*/
};

/*
** Train Skill
** Length: 12 Octets
** OpCode: SkillTrainCode
*/

struct skillTrainStruct
{
/*0000*/ int32_t  playerid;                      // player doing the training
/*0004*/ int32_t  type;                          // type of training?
/*0008*/ uint32_t skillId;                       // Id of skill
/*0012*/
};

/*
** Skill Increment
** Length: 12 Octets
** OpCode: SkillIncCode
*/

struct skillIncStruct
{
/*0000*/ uint32_t skillId;                       // Id of skill
/*0004*/ int32_t  value;                         // New value of skill
/*0008*/ uint8_t  unknown0008[4];                // *** Placeholder
/*0012*/
};

/*
** When somebody changes what they're wearing
**      or give a pet a weapon (model changes)
** Length: 14 Octets
** Opcode: WearChangeCode
*/

/*
** WearChange — multi-slot appearance update packet header
** Total size: 13 bytes (slotCount=0, refresh only) or
**   11 + slotCount*(16 + strlen(ownerName)+1) + 2 pad bytes.
** Wire confirmed at 13 B (A's self-view) and 97 B (observer, 4 slots, name "Eeve").
*/
struct wearChangeStruct
{
/*0000*/ uint32_t spawnId;                       // spawn whose appearance changed
/*0004*/ uint32_t appearance;                    // overall appearance value (varies per equip)
/*0008*/ uint8_t  unknown0008;                   // = 1 in all observed samples
/*0009*/ uint8_t  slotCount;                     // number of wearChangeSlotStruct records following
/*0010*/ uint8_t  unknown0010;                   // = 0 (padding before record array)
/*0011*/
};

/*
** WearChangeSlot — per-slot record following wearChangeStruct header
** Fixed 16-byte body + null-terminated ownerName (variable length).
** Not directly sizeof()-able due to the trailing string.
*/
struct wearChangeSlotStruct
{
/*0000*/ uint32_t slotId;                        // wear slot index
/*0004*/ uint32_t material;                      // material/texture ID
/*0008*/ uint32_t color;                         // color tint
/*0012*/ uint32_t unknown0012;                   // = 0
/*0016*/ // char ownerName[] follows — null-terminated, variable length
};

/*
** Level Update
** Length: 24 Octets
** OpCode: LevelUpUpdateCode
*/

struct levelUpUpdateStruct
{
/*0000*/ uint32_t level;                         // New level
/*0004*/ uint32_t levelOld;                      // Old level
/*0008*/ uint32_t exp;                           // Current Experience
/*0012*/ uint32_t  unknown0012;                  // unknown
/*0016*/ uint32_t  unknown0016;                  // 2026-05-22 patch padding
/*0020*/ uint32_t  unknown0020;                  // 2026-05-22 patch padding
/*0024*/
};

/*
** Experience Update
** Length: 16 Octets
** OpCode: ExpUpdateCode
*/

struct expUpdateStruct
{
/*0000*/ uint32_t exp;                           // per-level progress, 0-100000 (0.001% granularity)
/*0004*/ uint32_t unknown0004;                   // unknown
/*0008*/ uint32_t type;                          // 0=set, 2=update
/*0012*/ uint32_t unknown0012;                   // unknown
/*0016*/
};

/*
** Alternate Experience Update
** Length: 12 Octets
** OpCode: AltExpUpdateCode
*/
struct altExpUpdateStruct
{
/*0000*/ uint32_t altexp;                        // alt exp x/330
/*0004*/ uint32_t aapoints;                      // current number of AA points
/*0008*/ uint8_t  percent;                       // percentage in integer form
/*0009*/ uint8_t  unknown0009[3];                // ***Place Holder
/*0012*/
};

/**
 * Leadership AA update
 * Length: 32 Octets
 * OpCode: LeadExpUpdate
 */
struct leadExpUpdateStruct
{
/*0000*/ uint32_t unknown0000;                   // All zeroes?
/*0004*/ uint32_t groupLeadExp;                  // Group leadership exp value
/*0008*/ uint32_t unspentGroupPoints;            // Unspent group points
/*0012*/ uint32_t unknown0012;                   // Type?
/*0016*/ uint32_t unknown0016;                   // All zeroes?
/*0020*/ uint32_t raidLeadExp;                   // Raid leadership exp value
/*0024*/ uint32_t unspentRaidPoints;             // Unspent raid points
/*0028*/ uint32_t unknown0028;
/*0032*/
};

/*
** Player Spawn Update
** Length: 27 Octets
** OpCode: SpawnUpdateCode
*/

struct SpawnUpdateStruct
{
/*0000*/ uint16_t spawnId;                       // Id of spawn to update
/*0002*/ uint16_t subcommand;                    // some sort of subcommand type
/*0004*/ int16_t  arg1;                          // first option
/*0006*/ int16_t  arg2;                          // second option
/*0008*/ uint8_t  arg3;                          // third option?
/*0009*/ uint8_t unknown0009[23];
/*0027*/
};

/*
** NPC Hp Update
** Length: 18 Octets
** Opcode NpcHpUpdateCode
*/

struct hpNpcUpdateStruct
{
/*0000*/ uint16_t spawnId;
/*0002*/ int32_t curHP;
/*0006*/ uint32_t unknown0006;                   // Placeholder added 06/19/19
/*0010*/ int32_t maxHP;
/*0014*/ uint32_t unknown0014;                   // Placeholder added 06/19/19
/*0018*/
};

// OP_MobHealth: subsequent HP ticks for any mob whose maxHP was already
// sent via OP_InitialMobHealth or OP_HPUpdate. Carries an HP percentage
// (0-100), not raw HP — confirmed 2026-05-01 by a kill capture where the
// first observed value was exactly 100 on a non-targeted-yet mob.
// Consumers should compute `curHP = maxHP * hpPercent / 100`.
struct mobHealthStruct
{
/*0000*/ uint16_t spawnId;
/*0002*/ int32_t  hpPercent;
/*0006*/
};

/*
** Inspecting Information
** Length: 1956 Octets
** OpCode: InspectDataCode
*/

struct inspectDataStruct
{
/*0000*/ uint8_t  unknown0000[4];                // ***Placeholder
/*0004*/ uint32_t spawnId;                       // Id of spawn inspected
/*0008*/ char     itemNames[23][64];             // 23 items with names 
                                                 // 64 characters long.
/*1480*/ int32_t  icons[23];                     // Icon Information
/*1572*/ char     mytext[200];                   // Player Defined Text Info
/*1772*/ uint8_t  unknown1772[184];              // ***Placeholder
/*1956*/
};

/*
** Reading Book Information
** Length: Variable Length Octets
** OpCode: BookTextCode
*/

struct bookTextStruct
{
/*0000*/ uint16_t unknown0000;
/*0002*/ char     text[0];                       // Text of item reading
};

/*
** Interrupt Casting
** Length: 6 Octets + Variable Length Octets
** Opcode: BadCastCode
*/

struct badCastStruct
{
/*0000*/ uint32_t spawnId;                       // Id of who is casting
/*0004*/ char     message[0];                    // Text Message
};

/*
** Random Number Request
** Length: 8 Octets
** OpCode: RandomCode
*/
struct randomReqStruct 
{
/*0000*/ uint32_t bottom;                        // Low number
/*0004*/ uint32_t top;                           // High number
/*0008*/
};

/*
** Random Number Result
** Length: 76 Octets
** OpCode: RandomCode
*/
struct randomStruct 
{
/*0000*/ uint32_t bottom;                        // Low number
/*0004*/ uint32_t top;                           // High number
/*0008*/ uint32_t result;                        // result number
/*0012*/ char     name[64];                      // name rolled by
/*0076*/
};

/*
** Player Position Update
** Length: 24 Octets
** OpCode: PlayerPosCode
*/
struct playerSpawnPosStruct
{
/*0000*/ uint16_t spawnId;
/*0002*/ uint16_t spawnId2;
/*0004*/
         signed   z:19;                            // z coord (3rd loc value)
         signed   animation:10;                    // current animation
         unsigned padding00:3;
/*0008*/
         signed   y:19;                            // y coord (2nd loc value)
         unsigned heading:12;                      // heading
         unsigned padding01:1;
/*0012*/
         signed   deltaY:13;                       // change in y
         unsigned pitch:12;                        // pitch (up/down heading)
         unsigned padding02:7;
/*0016*/
         signed   x:19;                            // x coord (1st loc value)
         signed   deltaX:13;                       // change in x
/*0020*/
         signed   deltaHeading:10;                 // change in heading
         signed   deltaZ:13;                       // change in z
         unsigned padding04:9;
/*0024*/
};

/*
** Self Position Update
** Length: 42 Octets
** OpCode: PlayerPosCode
*/
struct playerSelfPosStruct
{
/*0000*/ uint16_t unknown0000;                   // ***Placeholder (update time counter?)
/*0002*/ uint16_t spawnId;                       // Player's spawn id
/*0004*/ uint16_t unknown0004;                   // ***Placeholder
/*0006*/
	 signed   animation:10;              // current animation
	 unsigned padding00:22;
/*0010*/
	 float    y;                         // y coord (2nd loc value)
/*0014*/
	 float    deltaX;                    // change in x
/*0018*/
	 float    z;                         // z coord (3rd loc value)
/*0022*/
	 signed   deltaHeading:10;           // change in heading
	 unsigned pitch:12;                  // pitch (up/down heading)
	 unsigned padding04:10;
/*0026*/
	 float    deltaZ;                    // change in z
/*0030*/
	 float    x;                         // x coord (1st loc value)
/*0034*/
	 float    deltaY;                    // change in y
/*0038*/
	 unsigned heading:12;                // heading
	 unsigned padding08:20;
/*0042*/
};

/*
** Spawn Appearance
** Length: 8 Octets
** OpCode: spawnAppearanceCode
*/

// Re-derived 2026-07-28 against 54 S>C packets over two current-patch captures
// (2 zones). The legacy layout (u16 spawnId, u16 type, u32 parameter) reads
// type == 0 for EVERY packet, with 4/32/64 landing in `parameter` — an
// appearance opcode that only ever sends subcommand 0, never level/anon/AFK, is
// not credible over that sample. Read as the modern layout below it yields
// three distinct types and no value field, matching the u32/u32 convention of
// spawnAppearance2Struct. NOTE: the two readings put the spawn id at the same
// offset, so id decode was never wrong; what moved is `type`, and `parameter`
// no longer exists (a typed value now rides OP_SpawnAppearance2).
// Type semantics are NOT yet confirmed — see OPCODES_LIVE_TODO.md 2026-07-28.
struct spawnAppearanceStruct
{
/*0000*/ uint32_t spawnId;                       // ID of the spawn
/*0004*/ uint32_t type;                          // Type of data sent (no value field)
/*0008*/
};

// OP_SpawnAppearance2 (0x8cd3) — 24-byte extended appearance update. On
// lock-ruleset (TLP) servers, S>C type=0x2c (44) carries the mob-lock / FTE
// attackable flag (value: 1=locked/unattackable, 0=attackable). Other types
// carry guild/cast/appearance state; C>S type=0x24 is OP_ClickBuffOff.
struct spawnAppearance2Struct
{
/*0000*/ uint32_t spawnId;                       // ID of the spawn
/*0004*/ uint32_t type;                          // appearance type (0x2c = lock)
/*0008*/ uint32_t value;                          // type-specific value
/*0012*/ uint8_t  unknown0012[12];               // padding / unused
/*0024*/
};


/*
**               Structures that are not being currently used
 *               (except for logging)
*/

struct bindWoundStruct
{
/*0000*/ uint16_t playerid;                      // TargetID
/*0002*/ uint8_t  unknown0002[2];                // ***Placeholder
/*0004*/ uint32_t hpmaybe;                       // Hitpoints -- Guess
/*0008*/
};

struct inspectedStruct
{
/*0000*/ uint16_t inspectorid;                   // Source ID
/*0002*/ uint8_t  unknown0002[2];                // ***Placeholder
/*0004*/ uint16_t inspectedid;                   // Target ID - Should be you
/*0006*/ uint8_t  unknown0006[2];                // ***Placeholder
/*0008*/
};

struct attack1Struct
{
/*0000*/ uint16_t spawnId;                       // Spawn ID
/*0002*/ int16_t  param1;                        // ***Placeholder
/*0004*/ int16_t  param2;                        // ***Placeholder
/*0006*/ int16_t  param3;                        // ***Placeholder
/*0008*/ int16_t  param4;                        // ***Placeholder
/*0010*/ int16_t  param5;                        // ***Placeholder
/*0012*/
};

struct attack2Struct
{
/*0000*/ uint16_t spawnId;                       // Spawn ID
/*0002*/ int16_t  param1;                        // ***Placeholder
/*0004*/ int16_t  param2;                        // ***Placeholder
/*0006*/ int16_t  param3;                        // ***Placeholder
/*0008*/ int16_t  param4;                        // ***Placeholder
/*0010*/ int16_t  param5;                        // ***Placeholder
/*0012*/
};

struct newGuildInZoneStruct
{
/*0000*/ uint32_t  guildId;                      // Guild ID
/*0004*/ uint32_t  serverID;                     // ***Placeholder
/*0008*/ char      guildname[0];                 // Guild name, null terminated
/*xxxx*/
};

struct guildsInZoneListStruct
{
/*0000*/ uint32_t  name_len;                     // length of player name
/*0004*/ char      name[16];                     // player name of length name_len,
                                                 // no null terminator, max 16
/*xxxx*/ uint32_t  num_guilds;                   // number of guild names in this struct
/*xxxx*/ newGuildInZoneStruct guilds[0];
/*xxxx*/
};

struct moneyUpdateStruct
{
/*0000*/ uint32_t spawnid;                       // ***Placeholder
/*0004*/ uint32_t cointype;                      // Coin Type
/*0008*/ uint32_t amount;                        // Amount
/*0012*/
};

/* Memorize slot operations, mem, forget, etc */

struct memorizeSlotStruct
{
/*0000*/ uint32_t slot;                          // Memorization slot (0-7)
/*0004*/ uint32_t spellId;                       // Id of spell 
                                                 // (offset of spell in spdat.eff)
/*0008*/ uint32_t action;                        // 1-memming,0-scribing,2-forget
/*0012*/
};

/*
** Spawn Appearance
** Length: 4 Octets
** OpCode: SetRunModeCode
*/

struct cRunToggleStruct
{
/*0000*/ uint32_t status;                        //01=run  00=walk
/*0004*/
};

struct cChatFiltersStruct
{
/*0000*/ uint32_t DamageShields;                 //00=on  01=off
/*0004*/ uint32_t NPCSpells;                     //00=on  01=off
/*0008*/ uint32_t PCSpells;                      //00=all 01=off 02=grp
/*0012*/ uint32_t BardSongs;                     //00=all 01=me  02=grp 03=off
/*0016*/ uint32_t Unused;
/*0020*/ uint32_t GuildChat;                     //00=off 01=on
/*0024*/ uint32_t Socials;                       //00=off 01=on
/*0028*/ uint32_t GroupChat;                     //00=off 01=on
/*0032*/ uint32_t Shouts;                        //00=off 01=on
/*0036*/ uint32_t Auctions;                      //00=off 01=on
/*0040*/ uint32_t OOC;                           //00=off 01=on
/*0044*/ uint32_t MyMisses;                      //00=off 01=on
/*0048*/ uint32_t OthersMisses;                  //00=off 01=on
/*0052*/ uint32_t OthersHits;                    //00=off 01=on
/*0056*/ uint32_t AttackerMisses;                //00=off 01=on
/*0060*/ uint32_t CriticalSpells;                //00=all 01=me  02=off
/*0064*/ uint32_t CriticalMelee;                 //00=all 01=me  02=off
/*0068*/
};

struct cOpenSpellBookStruct
{
/*0000*/ int32_t status;                         //01=open 00=close
/*0004*/
};

struct tradeSpellBookSlotsStruct
{
/*0000*/ uint32_t slot1;
/*0004*/ uint32_t slot2;
/*0008*/
};


/*
** serverLFGStruct
** Length: 10 Octets
** signifies LFG, maybe afk, role, ld, etc
*/

struct serverLFGStruct
{
/*0000*/ uint16_t spawnID;
/*0002*/ uint16_t unknown0002;
/*0004*/ uint16_t LFG;                           //1=LFG
/*0006*/ uint16_t unknown0006;
/*0008*/
};

/*
** clientLFGStruct
** Length: 70 Octets
** signifies LFG, maybe afk, role, ld, etc
*/

struct clientLFGStruct
{
/*0000*/ uint8_t  name[64];
/*0064*/ uint16_t LFG;                           //1=LFG
/*0066*/ uint16_t unknown0066;
};

/*
** buffWindowSlotStruct (OP_BuffWindow)
** Length: 12 Octets
** Fires 10x per zone-in (slots 0-9) to initialize the short-duration buff window.
** field2 is always 0xffffffff (empty) on zone-in; never observed with a live spell id.
** Active buffs are communicated via OP_Buff / OP_BuffQuery separately.
*/

struct buffWindowSlotStruct
{
/*0000*/ uint32_t slot;     // buff window slot index (0-9)
/*0004*/ uint32_t spellid;  // 0xffffffff = empty (wire-verified: always empty at zone-in)
/*0008*/ uint32_t unknown;  // always 0 in observed captures
/*000C*/
};

/*
** buffStruct
** Length: 168 Octets
**
*/

struct buffStruct
{
/*0000*/ uint32_t spawnid;                       // spawn id (wire-verified)
/*0004*/ uint8_t  unknown0004[4];                // +4..+7: u32 (513/568/559 observed, meaning unknown)
/*0008*/ uint8_t  unknown0008[4];                // always 0xffffffff — likely caster spawn id = self
/*0012*/ uint8_t  unknown0012[104];              // includes variable content (possible caster name at +12)
/*0116*/ uint32_t spellid;                       // spell id (wire-verified)
/*0120*/ uint32_t duration;                      // remaining ticks (wire-verified)
/*0124*/ uint32_t initialDuration;               // initial/max ticks when buff was applied (wire-verified: always >= duration, delta = elapsed ticks)
/*0128*/ uint8_t  unknown0128[8];
/*0136*/ float    unknown0136;                   // always 1.0 — possible buff modifier/potency
/*0140*/ uint8_t  unknown0140[12];
/*0152*/ uint8_t  level;                         // caster level (wire-verified: monk=35, SK=18; was at wrong offset +153 in old struct)
/*0153*/ uint8_t  unknown0153[7];
/*0160*/ uint32_t spellslot;                     // buff window slot index (wire-verified)
/*0164*/ uint32_t changetype;                    // always 1 on Live (old comment 1=fading/2=duration is stale)
/*0168*/
};

/*
** Guild Member Update structure 
** Length: 88 Octets
**
*/

struct GuildMemberUpdate
{
/*000*/ uint32_t guildId;                        // guild id
/*004*/ uint8_t  unknown004[4];			         // 4 bytes added 11/16/16
/*008*/ char     name[64];                       // member name
/*072*/ uint16_t zoneId;                         // zone id 
/*074*/ uint16_t zoneInstance;                   // zone instance
/*076*/ uint32_t lastOn;                         // time the player was last on.
/*080*/ uint8_t  unknown080[8];                  // 4 bytes added 11/28/12, +4 added 11/16/16
/*088*/
};

/*
** Bazaar trader on/off struct
** Length: 76 Octets
**
*/
struct bazaarTraderRequest
{
/*000*/ uint32_t spawnId;                        // Spawn id of person turning trader on/off
/*004*/ uint8_t mode;                            // 0=off, 1=on
/*005*/ uint8_t uknown005[3];                    // 
/*008*/ char     name[64];                       // trader name
/*072*/ uint8_t uknown072[4];                    //
/*076*/
};

struct bazaarSearchQueryStruct 
{
/*0000*/ uint32_t mark;
/*0004*/ uint32_t type;
/*0008*/ char     unknownXXX0[32];                      // Search Criteria flags
/*0040*/ char     searchstring[64];
/*0104*/ uint32_t minplat;				// Minimum platinum price
/*0108*/ uint32_t maxplat;				// Maximum platinum price
/*0112*/ uint32_t minlvl;				// Minimum level for search
/*0116*/ uint32_t maxlvl;				// Maximum level for search
/*0120*/ uint32_t maxresults;				// limits number of results to this value
/*0124*/ uint32_t prestige;				// Prestige flag
/*0128*/ uint32_t augtype;				// Augmentation flag 
/*0132*/
};

/*
** Item Bazaar Search Result
** Length: 160 Octets
** OpCode: BazaarSearch
*/
struct bazaarSearchResponseStruct 
{
/*0000*/ uint32_t mark;                          // ***unknown***
/*0004*/ uint32_t player_id;                     // trader ID
/*0008*/ char     merchant_name[64];             // trader name
/*0072*/ uint32_t count;                         // Count of items for sale
/*0076*/ uint32_t item_id;                       // ID of item for sale
/*0080*/ uint8_t uknown0080[8];                  // ***unknown***
/*0088*/ char     item_name[64];                 // nul-padded name with appended "(count)"
/*0152*/ uint32_t price;                         // price in copper
/*0156*/ uint8_t uknown0156[4];                  // ***unknown***
/*0160*/
};

/*
bazaarSearchResponseStruct as of 03/13/19
Beginning of packet includes:

	uint32_t player_id;                      // player ID
	uint8_t uknown0004[10];                  // ***unknown***

If there are multiple results, they use the following repeating struct:

		uint8_t uknown0014[21];                 // ***unknown***
		uint32_t price;                         // price in copper
		uint32_t count;                         // Count of items for sale
		uint32_t item_id;                       // ID of item for sale
		uint32_t icon_id;                       // Icon of item for sale
		char     item_name[64];                 // null termintated (00) item name
		uint32_t uknownXXXX;                    // ***unknown***
*/


/*
** Item Bazaar Search Result
** Length: Variable
** OpCode: BazaarSearch
*/
union bazaarSearchStruct
{
  uint32_t mark;
  struct bazaarSearchQueryStruct query;
  struct bazaarSearchResponseStruct response[0];
};

/*******************************/
/* World Server Structs        */

/*
** Guild List (from world server)
** Length: 68 Octets
** used in: worldGuildList
*/

struct guildListStruct
{
/*0000*/ uint32_t guildId;
/*0004*/ char     guildName[64];
};

/*
** Guild List (from world server)
** Length: Variable (serialized)
*/
struct worldGuildListStruct
{
/*000*/ uint8_t   unknown000[64];
/*064*/ uint32_t  numberOfGuilds; // ?
/*068*/ guildListStruct guilds[MAX_GUILDS];      // MAX_GUILDS varies by server now
};

struct worldMOTDStruct
{
  /*002*/ char    message[0];
  /*???*/ uint8_t unknownXXX[3];
};

// Restore structure packing to default
#pragma pack()

#endif // EQSTRUCT_H

//. .7...6....,X....D4.M.\.....P.v..>..W....
//123456789012345678901234567890123456789012
//000000000111111111122222222223333333333444
