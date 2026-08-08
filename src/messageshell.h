/*
 *  messageshell.h
 *  Copyright 2002-2003 Zaphod (dohpaz@users.sourceforge.net)
 *  Copyright 2005-2009, 2019 by the respective ShowEQ Developers
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

#ifndef _MESSAGESHELL_H_
#define _MESSAGESHELL_H_

#include "messages.h"

#include <cstdint>

#include <QObject>

//----------------------------------------------------------------------
// forward declarations
class QString;
class QDateTime;

class EQStr;
class Spells;
class SpellMessages;
class DbStrings;
class ZoneMgr;
class SpawnShell;
class Item;
class Player;

struct ClientZoneEntryStruct;
struct ServerZoneEntryStruct;
struct charProfileStruct;
struct inspectDataStruct;
struct zoneChangeStruct;

//----------------------------------------------------------------------
// MessageShell
class MessageShell : public QObject
{
  Q_OBJECT
 public:
  MessageShell(Messages* messages, EQStr* eqStrings, Spells* spells,
	       SpellMessages* spellMessages, DbStrings* dbStrings,
	       ZoneMgr* zoneMgr, SpawnShell* spawnShell,
               Player* player, QObject* parent = 0, const char* name = 0);

 public slots:
   void channelMessage(const uint8_t* cmsg, size_t, uint8_t);
   void formattedMessage(const uint8_t* cmsg, size_t, uint8_t);
   // EQL OP_FormattedMessage (0x3c0a) diverges from the Live layout (format
   // id @9, spell id @0, pre-split caret args); decoded + routed separately.
   void formattedMessageEQL(const uint8_t* cmsg, size_t, uint8_t);
   void lootMessage(const uint8_t* lmsg, size_t, uint8_t);
   void lootDrops(const uint8_t* data, size_t, uint8_t);
   // OP_LootTransaction: item confirmation (sale proceeds) or corpse coin pile.
   void lootTransaction(const uint8_t* data, size_t, uint8_t);
   void simpleMessage(const uint8_t* cmsg, size_t, uint8_t);
   void specialMessage(const uint8_t* smsg, size_t, uint8_t);
   // EQ Legends UCS cross-zone chat: one raw port-9877 server->client payload
   // (from EQPacket::ucsChatData) + the owning client's addr. Runs the Rust
   // decode + channel resolution and emits chatMessage per line. No-op on
   // live/test (Rust stub is empty).
   void ucsChatMessage(const uint8_t* data, size_t len, uint8_t dir,
                       uint32_t clientAddr);
   void guildMOTD(const uint8_t* gmotd, size_t, uint8_t);
   void consent(const uint8_t* consent, size_t, uint8_t);
   void moneyOnCorpse(const uint8_t* money);
   void moneyUpdate(const uint8_t* money);
   void moneyThing(const uint8_t* money);
   void randomRequest(const uint8_t* randr);
   void random(const uint8_t* randr);
   void emoteText(const uint8_t* emotetext);
   void inspectData(const uint8_t* inspt);

   void logOut(const uint8_t*, size_t, uint8_t);
   void zoneEntryClient(const ClientZoneEntryStruct* zsentry);
   void zoneNew(const uint8_t* zoneNew, size_t, uint8_t);
   void zoneChanged(const zoneChangeStruct*, size_t, uint8_t);
   void zoneBegin(const QString& shortZoneName);
   void zoneEnd(const QString& shortZoneName, const QString& longZoneName);
   void zoneChanged(const QString& shortZoneName);

   void worldMOTD(const uint8_t* motd);

   void handleSpell(const uint8_t* mem, size_t, uint8_t);
   void beginCast(const uint8_t* bcast);
   void spellFaded(const uint8_t* sf);
   void interruptSpellCast(const uint8_t*icast);
   void startCast(const uint8_t* cast);

   void groupUpdate(const uint8_t* gmem, size_t, uint8_t);
   void groupInvite(const uint8_t* gmem, size_t, uint8_t);
   void groupDecline(const uint8_t* gmem);
   void groupFollow(const uint8_t* gmem);
   void groupDisband(const uint8_t* gmem);
   void groupLeaderChange(const uint8_t* gmem);

   void syncDateTime(const QDateTime&);

   void player(const charProfileStruct* player);
   void increaseSkill(const uint8_t* data);
   void updateLevel(const uint8_t* data);
   void consMessage(const uint8_t* data, size_t, uint8_t dir);

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

   void addItem(const Item* item);
   void delItem(const Item* item);
   void killSpawn(const Item* item);
   void filterMessage(const QString& prefix, MessageType type,
		      const Item* item);

 signals:
   // Structured emission of a player-to-player chat message, in addition
   // to the formatted addMessage() call further down channelMessage().
   // Non-player message types (system, NPC emote, formatted/special
   // server messages, etc.) are not emitted here. Phase 3 sessionadapter
   // listens to this and forwards to clients as seq.v1.ChatMessage.
   // channelName carries the literal UCS channel name ("General", "NewPlayers")
   // for cross-zone channels that don't map to a fixed MessageType; empty for
   // the typed channels enumerated in `channel`. Defaulted so the existing
   // typed-channel emitters below stay 5-arg.
   void chatMessage(uint32_t channel, const QString& from,
                    const QString& target, const QString& text,
                    uint32_t chatColor, const QString& channelName = QString());

   // Emitted when OP_InspectAnswer arrives. SessionAdapter listens and
   // forwards to clients as seq.v1.InspectAnswer.
   void inspectReceived(const inspectDataStruct* data);
   void lootDropsReceived(uint32_t corpseId, const QString& corpseName,
                          const QStringList& names, const QVector<uint32_t>& icons,
                          const QVector<uint32_t>& itemIds);
   void lootTransactionReceived(uint32_t corpseId, uint32_t itemId,
                                uint32_t quantity, uint32_t coinCopper,
                                bool fromCorpse);

 protected:
   Messages* m_messages;
   EQStr* m_eqStrings;
   Spells* m_spells;
   SpellMessages* m_spellMessages;
   DbStrings* m_dbStrings;
   ZoneMgr* m_zoneMgr;
   SpawnShell* m_spawnShell;
   Player* m_player;
};


#endif // _MESSAGESHELL_H_

