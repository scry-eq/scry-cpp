/*
 *  player.h
 *  Copyright 2001-2008, 2013, 2019 by the respective ShowEQ Developers
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

#ifndef EQPLAYER_H
#define EQPLAYER_H

#include <QHash>
#include <QObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <vector>

#include "everquest.h"
#include "spawn.h"

//----------------------------------------------------------------------
// forward declarations
class GuildMgr;
class ZoneMgr;

//----------------------------------------------------------------------
// Player
class Player : public QObject, public Spawn
{
  Q_OBJECT 
public:
  Player (QObject* parent,
	  ZoneMgr* zoneMgr,
	  GuildMgr* guildMgr,
	  const char* name = "player");
  virtual ~Player();

  // One wire packet's worth of self vitals; see setVitals(). Declared out here
  // rather than beside setVitals because moc rejects type declarations inside a
  // `slots:` section.
  struct Vitals
  {
     bool haveHP = false;    uint32_t hpCur = 0,   hpMax = 0;
     bool haveMana = false;  uint32_t manaCur = 0, manaMax = 0;
     bool haveEnd = false;   uint32_t endCur = 0,  endMax = 0;
  };

 public slots:
   void clear();
   void reset();
   void setUseAutoDetectedSettings(bool enable);
   void setDefaultName(const QString&);
   void setDefaultLastname(const QString&);
   void setDefaultLevel(uint8_t);
   void setDefaultRace(uint16_t);
   void setDefaultClass(uint8_t);
   void setDefaultDeity(uint16_t);

   void player(const charProfileStruct* player); 
   void loadProfile(const playerProfileStruct& player);
   void increaseSkill(const uint8_t* skilli);
   void manaChange(const uint8_t* mana);
   // Neutral primitive: apply whichever of the self's current+max HP/mana/endurance
   // arrived in ONE wire packet, then emit a single vitalsChanged(). Used where self
   // vitals arrive as real cur/max already decoded (the eql 0x2735 stat-sync channel,
   // which multiplexes all three into one packet — the player is not a m_spawns entry
   // there, so its vitals surface via player_stats, not spawn_updated). Applying them
   // together is what keeps one packet to one player_stats envelope; three separate
   // setters emitted three near-identical full snapshots. Unused on live/test
   // (endurance there comes from the standalone OP_EndUpdate via updateEndurance()).
   void setVitals(const Vitals& v);
   void updateExp(const uint8_t* exp);
   void updateAltExp(const uint8_t* altexp);
   void updateLevel(const uint8_t* levelup);
   // Neutral level-set primitive for backends that DERIVE a level change with
   // no discrete level packet: eql has no OP_LevelUpdate, so it infers a ding
   // from the exp bar wrapping (see EqlDispatch::expUpdate / OPCODES_LEGENDS.md).
   // Sets the level and fires the same con-table refresh + notifications
   // updateLevel() does. Unused on live/test (they get level from OP_LevelUpdate).
   void applyLevel(uint8_t level);
   void updateNpcHP(const uint8_t* hpupdate);
   void updateSpawnInfo(const uint8_t* su);
   void updateStamina(const uint8_t* stam);
   // Authoritative carried coin, from OP_MoneyUpdate or OP_PlayerProfile (they
   // agree). EQL does not normalize denominations on the wire (101 silver / 281
   // copper observed), so the total is summed rather than assuming each is < 10.
   void setMoneyCoins(uint32_t plat, uint32_t gold, uint32_t silver,
                            uint32_t copper);
   // Incremental coin delta between authoritative updates. Neither money source
   // fires per coin-earning event, so auto-sell income would otherwise stay
   // invisible until the next zone-in. Runs optimistic — it sees income but not
   // spending — and the next authoritative update resyncs the true total.
   void adjustMoney(int64_t deltaCopper);
   uint32_t money() const { return m_money; }
   // Hunger/thirst from OP_Stamina: "ticks till next eat", ~6000 full -> 0 hungry.
   uint16_t food() const { return m_food; }
   uint16_t water() const { return m_water; }
   // EQL multiclass bitmask (bit N = class N); classVal() is only the primary.
   uint32_t classMask() const { return m_classMask; }
   void setClassMask(uint32_t m) { m_classMask = m; }
   // EQL active stance/invocation display names, resolved from the wire ability
   // id by EqlDispatch. Backend-neutral: Player holds the resolved STRINGS, not
   // eql ability ids. Empty until the server echoes OP_Stance/OP_Invocation.
   const QString& stance() const { return m_stance; }
   const QString& invocation() const { return m_invocation; }
   void setStance(const QString& s) { m_stance = s; }
   void setInvocation(const QString& s) { m_invocation = s; }
   // Exact, gear+buff-inclusive max mana from the eql stat-sync wide form
   // (OP_HPUpdate 0xa5c0) — the server's own value, so it tracks equip/unequip
   // and buffs. 0 on Live, which never sends max mana.
   uint32_t wireMaxMana() const { return m_wireMaxMana; }
   // Fallback when the wire max is absent (Live): highest current-mana ever
   // seen. You can't hold more current than your max, so this equals the exact
   // max once the player has regen'd to full (beats the calcMaxMana estimate).
   uint16_t observedMaxMana() const { return m_observedMaxMana; }
   void updateEndurance(const uint8_t* end);
   void setLastKill(const QString& name, int level);
   void zoneChanged(void);
   void playerUpdateSelf(const uint8_t* pupdate, size_t, uint8_t);
   void consMessage(const uint8_t* con, size_t, uint8_t dir);
   void tradeSpellBookSlots(const uint8_t*, size_t, uint8_t);

   void setPlayerID(uint16_t playerID);
   void savePlayerState(void);
   void restorePlayerState(void);
   void setUseDefaults(bool bdefaults) { m_useDefaults = bdefaults; }

 public:
   virtual QString name() const;
   virtual QString lastName() const;
   virtual int level() const;
   virtual uint16_t deity() const;
   virtual uint16_t race() const;
   virtual uint8_t classVal() const;

   bool useAutoDetectedSettings() const { return m_useAutoDetectedSettings; }
   QString defaultName() const { return m_defaultName; }
   QString defaultLastName() const { return m_defaultLastName; }
   uint8_t defaultLevel() const { return m_defaultLevel; }
   uint16_t defaultDeity() const { return m_defaultDeity; }
   uint16_t defaultRace() const { return m_defaultRace; }
   uint8_t defaultClass() const { return m_defaultClass; }
   QString realName() const { return m_realName; }
   void setRealName(const QString& name) { m_realName = name; }

   virtual void killSpawn();
   void update(const spawnStruct* s) override;

   // ZBTEMP: compatibility code
   uint16_t getPlayerID() const { return id(); }
   // Alternate self spawn-id. eql keys the self's MOVEMENT to one id (== id())
   // but its PROFILE/BUFF data to a second, near-value id in the same zone; this
   // holds that second id so per-spawn character data (e.g. OP_BuffList) routes
   // to the player. 0 = none (always 0 on live/test).
   int16_t headingDegrees() const { return m_headingDegrees; }
   bool validPos() const { return m_validPos; }

   uint32_t getSkill(uint8_t skillId) { return m_playerSkills[skillId]; }
   uint8_t getLanguage(uint8_t langId) { return m_playerLanguages[langId]; }
   
   int getPlusHP() { return m_plusHP; }
   int getPlusMana() { return m_plusMana; }

   uint16_t getMaxSTR() { return m_maxSTR; }
   uint16_t getMaxSTA() { return m_maxSTA; }
   uint16_t getMaxCHA() { return m_maxCHA; }
   uint16_t getMaxDEX() { return m_maxDEX; }
   uint16_t getMaxINT() { return m_maxINT; }
   uint16_t getMaxAGI() { return m_maxAGI; }
   uint16_t getMaxWIS() { return m_maxWIS; }
   uint16_t getMaxMana() { return m_maxMana; }
   uint16_t getMana() { return m_mana; }
   uint32_t getSpellBookSlot(uint32_t slotid) { return m_spellBookSlots[slotid]; }

   uint32_t getCurrentExp() { return m_currentExp; }
   uint32_t getMaxExp() { return m_maxExp; }
   uint32_t getCurrentAltExp() const { return m_currentAltExp; }
   uint16_t getCurrentAApts() const { return m_currentAApts; }
   uint32_t getCurrentAAUnspent() const { return m_currentAAUnspent; }
   // Sparse list of AAs the player has purchased (rank > 0). Populated
   // from charProfileStruct.aa_array on each OP_PlayerProfile; auto-grant
   // entries (value=0) are filtered out. Empty until the first PP arrives.
   struct PurchasedAA { uint32_t abilityId; uint32_t rank; };
   const QVector<PurchasedAA>& getPurchasedAA() const { return m_purchasedAA; }
   // AA display names resolved from OP_SendAATable (eql): descID -> title.
   // Filled by EqlDispatch::sendAATable (titleSID -> dbstr type-1); read by
   // protoencoder to populate AAEntry.name. Empty on live/test (no such table
   // wired), so those clients keep the "#<id>" fallback. Neutral: no eql type.
   void setAAName(uint32_t descID, const QString& name) { m_aaNames.insert(descID, name); }
   QString aaName(uint32_t descID) const { return m_aaNames.value(descID); }
   uint16_t getFatigue() const { return m_fatigue; }
   uint32_t getEnduranceCur() const { return m_enduranceCur; }
   uint32_t getEnduranceMax() const { return m_enduranceMax; }
   

   bool getStatValue(uint8_t stat,
		     uint32_t& curValue,
		     uint32_t& maxValue);

   // Target-neutral primitives driven by the eql backend's EqlDispatch (no
   // Legends types here, so they compile on every target and go unused on
   // live/test). setIdentity applies a decoded race/class/level; applySelfPos
   // applies a decoded position+heading. See src/backend/eql/eqldispatch.cpp.
   void setIdentity(uint16_t race, uint8_t classVal, uint8_t level);
   void applyLifecycleIdentity(const QString& name, const QString& lastName,
                               uint16_t race, uint8_t classVal, uint8_t level,
                               uint16_t deity, uint32_t classMask);
   void applyProfileSupplement(const charProfileStruct* player);
   // heading is eql's 13-bit facing (0..8191, 8192 per circle); valid on every
   // packet, turning included. See seq-backend-eql/src/player_self_pos.rs.
   void applySelfPosition(int16_t x, int16_t y, int16_t z,
                          int16_t deltaX, int16_t deltaY, int16_t deltaZ,
                          uint16_t heading, float speed);
   // Sets the character name from a decoded profile (eql: the OP_PlayerProfile
   // name, located by the Rust anchor-scan). Stores it (setName) + emits
   // identityNameResolved so DaemonApp can authoritatively name/merge the box.
   void setPlayerName(const QString& name);
   // Bulk-seed the player's skill values from a decoded profile (eql: the
   // OP_PlayerProfile skill array, walked in seq-backend-eql). Writes
   // m_playerSkills[id] for each id < MAX_KNOWN_SKILLS then emits ONE changeSkill
   // so the whole set surfaces in a single coalesced PlayerStats snapshot at
   // zone-in (instead of ticking up one skill at a time via OP_SkillUpdate).
   // Neutral primitive: unused on live/test (they seed via loadProfile).
   void seedSkills(const std::vector<uint32_t>& skills);
   // eql: seed the purchased-AA list (parallel id/value arrays walked from
   // OP_PlayerProfile) + spent points, so the AA window populates at zone-in.
   void seedPurchasedAA(const std::vector<uint32_t>& ids,
                        const std::vector<uint32_t>& values, uint32_t spent);
   // eql: seed BASE stats from OP_PlayerProfile (fixed offset). The loadout
   // roll (race + primary + additional classes), not gear-inclusive totals.
   void seedBaseStats(uint16_t str, uint16_t sta, uint16_t cha, uint16_t dex,
                      uint16_t intel, uint16_t agi, uint16_t wis);

 signals:
   void newPlayer(void);
   // Authoritative character name decoded from the player's own profile (eql).
   void identityNameResolved(const QString& name);
   void buffLoad(const spellBuff*); 
   void newSpeed               (double speed);
   void statChanged            ( int statNum,
                                 int val,
                                 int max
                               );
   void addSkill               ( int,
                                 int
                               );
                               
   void changeSkill            ( int,
                                 int
                               );
   void deleteSkills();
   void addLanguage            ( int,
                                 int
                               );
   void changeLanguage         ( int,
                                 int
                               );
   void deleteLanguages();

   void setExp(uint32_t totalExp, uint32_t totalTick,
	       uint32_t minExpLevel, uint32_t maxExpLevel, 
	       uint32_t tickExpLevel);

   void newExp(uint32_t newExp, uint32_t totalExp, uint32_t totalTick,
	       uint32_t minExpLevel, uint32_t maxExpLevel, 
	       uint32_t tickExpLevel);
   void setAltExp(uint32_t totalExp,
		  uint32_t maxExp, uint32_t tickExp, uint32_t aapoints);
   void newAltExp(uint32_t newExp, uint32_t totalExp, uint32_t totalTick, 
		  uint32_t maxExp, uint32_t tickExp, uint32_t aapoints);
   void expAltChangedInt       (int, int, int);
   void expChangedInt          (int, int, int);
                               
   void expGained              ( const QString &,
                                 int,
                                 long,
                                 QString
                               );
                               
   void manaChanged            ( uint32_t,
                                 uint32_t
                               );
                               
   void stamChanged            ( int, int, int, int);
   void endChanged             ( uint32_t cur, uint32_t max );
   void moneyChanged           ( uint32_t copper );
  void hpChanged(int16_t, int16_t);
  // One packet's worth of self vitals landed (see setVitals) — coalesces what
  // would otherwise be up to three hp/mana/endChanged emissions.
  void vitalsChanged();
  void changedID(uint16_t oldPlayerID, uint16_t newPlayerID);
  void posChanged(int16_t x, int16_t y, int16_t z,
		  int16_t deltaX, int16_t deltaY, int16_t deltaZ,
		  int32_t heading);
  void changeItem(const Item* item, uint32_t changeType);
  void headingChanged(int32_t heading);
  void levelChanged(uint8_t level);
  void guildChanged();
  void playerUpdate(const uint8_t* data, size_t len, uint8_t dir);

 private:
  ZoneMgr* m_zoneMgr;
  GuildMgr* m_guildMgr;

  // The default values are set either by info showeq_params.
  // We keep a second copy in case the player levels while playing.
  QString m_defaultName;
  QString m_defaultLastName;
  QString m_realName;
  uint16_t m_mana;
  uint16_t m_defaultRace;
  uint16_t m_defaultDeity;
  uint8_t m_defaultClass;
  uint8_t m_defaultLevel;
  uint32_t m_playerSkills[MAX_KNOWN_SKILLS];
  uint8_t m_playerLanguages[MAX_KNOWN_LANGS];
  QVector<PurchasedAA> m_purchasedAA;
  QHash<uint32_t, QString> m_aaNames;   // eql: descID -> AA title (OP_SendAATable)
  
  uint16_t m_plusMana;
  uint16_t m_plusHP;

  uint16_t m_maxMana;
  // Widened from uint8_t (legacy showeq cap of 255) to fit modern Live's
  // post-cap stats — players today routinely exceed 255 (e.g. INT 330)
  // and the truncation was making calcMaxMana undershoot wildly.
  uint16_t m_maxSTR;
  uint16_t m_maxSTA;
  uint16_t m_maxCHA;
  uint16_t m_maxDEX;
  uint16_t m_maxINT;
  uint16_t m_maxAGI;
  uint16_t m_maxWIS;
  
  uint16_t m_food;
  uint16_t m_water;
  uint32_t m_classMask = 0;   // EQL multiclass bitmask (bit N = class N)
  QString m_stance;           // EQL active stance name (resolved); empty if none
  QString m_invocation;       // EQL active invocation name (resolved); empty if none
  uint16_t m_observedMaxMana = 0;   // peak current mana = exact max once full
  // eql: exact gear+buff max from stat-sync wide form. uint32 not uint16 —
  // the wide form carries 64-bit values and setMana takes uint32.
  uint32_t m_wireMaxMana = 0;
  uint32_t m_money = 0;   // carried coin as total copper (eql OP_PlayerProfile)
  uint16_t m_fatigue;
  uint32_t m_enduranceCur;
  uint32_t m_enduranceMax;
  
  // ExperienceWindow needs this
  uint32_t m_currentAltExp;
  uint16_t m_currentAApts;
  uint32_t m_currentAAUnspent;
  uint32_t m_currentExp;
  uint32_t m_minExp;
  uint32_t m_maxExp;
  uint32_t m_tickExp;
  
  uint32_t m_spellBookSlots[MAX_SPELLBOOK_SLOTS];
  
  // last spawn this player killed
  QString m_lastSpawnKilledName;
  int m_lastSpawnKilledLevel;
  
  // is the kill information fresh
  bool m_freshKill;
  
  // last spell cast on this player
  uint32_t m_lastSpellOnId;
  
  int16_t m_headingDegrees;
  // Wether or not we use defaults, determined by wether or not we could 
  // decode the zone loading data.  
  bool m_useDefaults;
  
  // Whether or not to use auto-detected character settings
  bool m_useAutoDetectedSettings;
  
  // which things are valid
  bool m_validStam;
  bool m_validMana;
  bool m_validHP;
  bool m_validExp;
  bool m_validAttributes;
  bool m_validPos;
};

#endif	// EQPLAYER_H
