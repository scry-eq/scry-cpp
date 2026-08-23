/*
 *  messageshell.cpp
 *  Copyright 2002-2003, 2007 Zaphod (dohpaz@users.sourceforge.net)
 *  Copyright 2005-2009, 2012, 2016, 2019 by the respective ShowEQ Developers
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

#include "messageshell.h"
#include "seq-bridge-cxx/lib.h"
#include "lootstore.h"
#include "eqstr.h"
#include "messages.h"
#include "everquest.h"
#include "spells.h"
#include "zonemgr.h"
#include "spawnshell.h"
#include "player.h"
#include "packetcommon.h"
#include "filtermgr.h"
#include "util.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QHash>
#include <QSet>

namespace {

// EQ wraps inline item references in chat / system text with the
// 0x12 (DC2) control byte: \x12<binary item header><item name>\x12.
// The header is uppercase-hex digits with a lot of zero padding;
// the name follows. showeq rendered these as raw bytes (its
// MessagesWindow doesn't strip them either) — you just rarely see
// a loot line in showeq chat. On the wire to a web client the
// binary is plain noise; strip it down to just the readable name.
QString stripEqItemLinks(const QString& in)
{
    if (!in.contains(QChar(0x12))) return in;
    QString out = in;
    // Primary pattern: hex prefix followed by an "Aa"-style start of
    // a real word. Captures the readable tail.
    static const QRegularExpression rx(
        QStringLiteral("\\x12[0-9A-F]+([A-Z][a-z][^\\x12]*)\\x12"));
    out.replace(rx, "\\1");
    // Fallback: any remaining \x12...\x12 pair (link without a clear
    // name boundary). Drop entirely so the wire payload is clean.
    static const QRegularExpression fallback(
        QStringLiteral("\\x12[^\\x12]*\\x12"));
    out.replace(fallback, QString());
    return out;
}

int64_t nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString qString(const ::rust::String& value)
{
    return QString::fromUtf8(value.data(), int(value.size()));
}

} // namespace

//----------------------------------------------------------------------
// MessageShell
MessageShell::MessageShell(Messages* messages, EQStr* eqStrings,
			   Spells* spells, ZoneMgr* zoneMgr,
			   SpawnShell* spawnShell, Player* player,
                           QObject* parent, const char* name)
  : QObject(parent),
    m_messages(messages),
    m_eqStrings(eqStrings),
    m_spells(spells),
    m_zoneMgr(zoneMgr),
    m_spawnShell(spawnShell),
    m_player(player),
    m_lootTracker(seq::rust::eql_loot_tracker_new())
{
    setObjectName(name);
}

// Persist whatever the tracker just completed. No store (--replay) means the
// rows are dropped, which is the point: a regression run must not write.
void MessageShell::recordLoot(const rust::Vec<seq::rust::LootRow>& rows)
{
    // This is the compatibility writer. Rust-owned sessions persist only
    // through applyLootAcquired/applyCorpseLootSnapshot below.
    if ((m_lootMutationGuard && !m_lootMutationGuard()) ||
        !m_lootStore || rows.empty())
        return;
    QVector<LootRowRec> out;
    out.reserve(static_cast<int>(rows.size()));
    for (const auto& r : rows)
    {
        LootRowRec rec;
        rec.ts = r.ts;
        rec.source = QString::fromUtf8(r.source.data(), r.source.size());
        rec.itemName = QString::fromUtf8(r.item_name.data(), r.item_name.size());
        rec.itemId = r.item_id;
        rec.icon = r.icon;
        rec.qty = r.qty;
        rec.mobName = QString::fromUtf8(r.mob_name.data(), r.mob_name.size());
        rec.mobNorm = QString::fromUtf8(r.mob_norm.data(), r.mob_norm.size());
        rec.corpseId = r.corpse_id;
        rec.zoneShort = QString::fromUtf8(r.zone_short.data(), r.zone_short.size());
        rec.zoneBase = QString::fromUtf8(r.zone_base.data(), r.zone_base.size());
        rec.instance = QString::fromUtf8(r.instance.data(), r.instance.size());
        rec.sold = r.sold;
        rec.moneyCopper = r.money_copper;
        rec.disposition = QString::fromUtf8(r.disposition.data(), r.disposition.size());
        rec.looter = QString::fromUtf8(r.looter.data(), r.looter.size());
        rec.sequence = r.sequence;
        out.push_back(rec);
    }
    m_lootStore->record(out);
}

void MessageShell::applyLootAcquired(
    const seq::rust::EventLootAcquisition& p)
{
    if (p.complete || p.has_sequence) {
        emit lootTransactionReceived(
            p.has_corpse_id ? p.corpse_id : 0,
            p.has_item_id ? p.item_id : 0,
            p.quantity, p.coin_copper, p.from_corpse);
    }
    if (!m_lootStore) return;
    LootRowRec row;
    row.ts = p.timestamp;
    row.source = p.from_corpse && !p.has_item_id
        ? QStringLiteral("coin") : QStringLiteral("message");
    row.itemName = qString(p.item_name);
    if (row.itemName.isEmpty() && row.source == QLatin1String("coin"))
        row.itemName = QStringLiteral("Coin");
    row.itemId = p.has_item_id ? p.item_id : 0;
    row.qty = p.quantity;
    row.mobName = qString(p.corpse_name);
    row.mobNorm = qString(p.corpse_name_normalized);
    row.corpseId = p.has_corpse_id ? p.corpse_id : 0;
    row.zoneShort = qString(p.zone_short);
    row.zoneBase = qString(p.zone_base);
    row.instance = qString(p.instance);
    row.sold = p.sold;
    row.moneyCopper = p.coin_copper;
    row.disposition = qString(p.disposition);
    row.looter = qString(p.looter);
    row.sequence = p.has_sequence ? p.sequence : 0;
    m_lootStore->record({row});
}

void MessageShell::applyCorpseLootSnapshot(
    const seq::rust::EventCorpseLootSnapshot& p)
{
    QStringList names;
    QVector<uint32_t> icons;
    QVector<uint32_t> itemIds;
    QVector<LootRowRec> rows;
    names.reserve(int(p.items.size()));
    icons.reserve(int(p.items.size()));
    itemIds.reserve(int(p.items.size()));
    rows.reserve(int(p.items.size()));
    for (const auto& item : p.items) {
        const QString name = qString(item.name);
        names.push_back(name);
        icons.push_back(item.icon);
        itemIds.push_back(item.item_id);
        LootRowRec row;
        row.ts = p.timestamp;
        row.source = QStringLiteral("window");
        row.itemName = name;
        row.itemId = item.item_id;
        row.icon = item.icon;
        row.mobName = qString(p.corpse_name);
        row.mobNorm = qString(p.corpse_name_normalized);
        row.corpseId = p.corpse_id;
        row.zoneShort = qString(p.zone_short);
        row.zoneBase = qString(p.zone_base);
        row.instance = qString(p.instance);
        row.looter = qString(p.looter);
        rows.push_back(std::move(row));
    }
    emit lootDropsReceived(p.corpse_id, qString(p.corpse_name),
                           names, icons, itemIds);
    if (m_lootStore) m_lootStore->record(rows);
}

void MessageShell::channelMessage(const uint8_t* data, size_t len, uint8_t dir)
{
  auto out = seq::rust::decode_channel_message(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;
  if (m_communicationMutationGuard && !m_communicationMutationGuard()) return;

  const uint32_t chanNum = out.chan_num;

  // Tells and Group by us happen twice *shrug*. Ignore the client->server one.
  if (dir == DIR_Client &&
      (chanNum == MT_Tell || chanNum == MT_Group || chanNum == MT_Guild ||
       chanNum == MT_OOC || chanNum == MT_Shout || chanNum == MT_Auction ||
       chanNum == MT_System || chanNum == MT_Raid))
  {
    return;
  }

  const QString sender =
      QString::fromLatin1(out.sender.data(), out.sender.size());
  const QString targetName =
      QString::fromLatin1(out.target.data(), out.target.size());
  const QString message =
      QString::fromLatin1(out.message.data(), out.message.size());

  // Emit the structured chatMessage signal so the websocket layer can
  // forward it as seq.v1.ChatMessage. Limit to player-to-player channels;
  // MT_System and other server-side noise stay confined to the formatted
  // addMessage() path below.
  switch (chanNum) {
  case MT_Guild:
  case MT_Group:
  case MT_Shout:
  case MT_Auction:
  case MT_OOC:
  case MT_Tell:
  case MT_Say:
  case MT_Raid:
    // OP_CommonMessage has no wire ChatColor — pass 0 (CC_Default) so
    // the client falls back to the chanNum->colour mapping.
    emit chatMessage(chanNum, sender, targetName,
                     stripEqItemLinks(message), 0u);
    break;
  default:
    break;
  }

  QString tempStr;
  const bool hasTarget = (chanNum >= MT_Tell) && !targetName.isEmpty();

  if (out.language)
  {
    const QString lang = language_name(out.language);
    if (hasTarget)
      tempStr = QString("'%1' -> '%2' - %3 {%4}").arg(sender, targetName, message, lang);
    else
      tempStr = QString("'%1' - %2 {%3}").arg(sender, message, lang);
  }
  else // Don't show common, its obvious
  {
    if (hasTarget)
      tempStr = QString("'%1' -> '%2' - %3").arg(sender, targetName, message);
    else
      tempStr = QString("'%1' - %2").arg(sender, message);
  }

  m_messages->addMessage(static_cast<MessageType>(chanNum), tempStr);
}

static MessageType chatColor2MessageType(ChatColor chatColor)
{
  MessageType messageType;

  // use the message color to differentiate between certain messages
  switch(chatColor)
  {
  case CC_User_Say:
  case CC_User_EchoSay:
    messageType = MT_Say;
    break;
  case CC_User_Tell:
  case CC_User_EchoTell:
    messageType = MT_Tell;
    break;
  case CC_User_Group:
  case CC_User_EchoGroup:
    messageType = MT_Group;
    break;
  case CC_User_Guild:
  case CC_User_EchoGuild:
    messageType = MT_Guild;
    break;
  case CC_User_OOC:
  case CC_User_EchoOOC:
    messageType = MT_OOC;
    break;
  case CC_User_Auction:
  case CC_User_EchoAuction:
    messageType = MT_Auction;
    break;
  case CC_User_Shout:
  case CC_User_EchoShout:
    messageType = MT_Shout;
    break;
  case CC_User_Emote:
  case CC_User_EchoEmote:
    messageType = MT_Emote;
    break;
  case CC_User_RaidSay:
    messageType = MT_Raid;
    break;
  case CC_User_Spells:
  case CC_User_SpellWornOff:
  case CC_User_OtherSpells:
  case CC_User_SpellFailure:
  case CC_User_SpellCrit:
    messageType = MT_Spell;
    break;
  case CC_User_MoneySplit:
    messageType = MT_Money;
    break;
  case CC_User_Random:
    messageType = MT_Random;
    break;
  default:
    messageType = MT_General;
    break;
  }
  
  return messageType;
}

QString MessageShell::resolveChatText(
    uint32_t formatId, const std::vector<std::string>& rawArgs) const
{
  if (!m_eqStrings) return QString();
  if (rawArgs.empty())
    return stripEqItemLinks(m_eqStrings->message(formatId));
  QStringList args;
  args.reserve(int(rawArgs.size()));
  for (const auto& arg : rawArgs)
    args.push_back(QString::fromUtf8(arg.data(), int(arg.size())));
  return stripEqItemLinks(m_eqStrings->formatMessage(formatId, args));
}

void MessageShell::applyChatMessage(const seq::rust::EventChatMessage& p)
{
  std::vector<std::string> args;
  args.reserve(p.args.size());
  for (const auto& arg : p.args) args.emplace_back(arg);
  const QString text = p.has_format_id
      ? resolveChatText(p.format_id, args) : qString(p.text);
  if (text.isEmpty()) return;

  const QString sender = qString(p.from);
  const QString target = qString(p.target);
  const QString channelName = qString(p.channel_name);
  const MessageType mt = static_cast<MessageType>(p.channel);
  QString display = text;
  if (p.kind == seq::rust::EventChatMessageKind::Common) {
    display = target.isEmpty()
        ? QString("'%1' - %2").arg(sender, text)
        : QString("'%1' -> '%2' - %3").arg(sender, target, text);
  } else if (p.kind == seq::rust::EventChatMessageKind::Special) {
    display = target.isEmpty()
        ? QString("Special: '%1' - %2").arg(sender, text)
        : QString("Special: '%1' -> '%2' - %3").arg(sender, target, text);
  }
  if (p.kind != seq::rust::EventChatMessageKind::Ucs)
    m_messages->addMessage(mt, display);
  emit chatMessage(p.channel, sender, target, text, p.chat_color, channelName);
}

void MessageShell::formattedMessage(const uint8_t* data, size_t len, uint8_t dir)
{
  // avoid client chatter and do nothing if not viewing channel messages
  if (dir == DIR_Client)
    return;

  auto out = seq::rust::decode_formatted_message(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;
  if (m_communicationMutationGuard && !m_communicationMutationGuard()) return;

  // Variable-length text follows the 13-byte header; pass through to
  // EQStr::formatMessage which walks the {u32 len, bytes} subseq array.
  constexpr size_t HEADER_LEN = offsetof(formattedMessageStruct, messages);
  const char* messages = reinterpret_cast<const char*>(data) + HEADER_LEN;
  const size_t messagesLen = len - HEADER_LEN;

  const MessageType mt = chatColor2MessageType(
      static_cast<ChatColor>(out.message_color));
  const QString text = stripEqItemLinks(
      m_eqStrings->formatMessage(out.message_format, messages, messagesLen));
  m_messages->addMessage(mt, text);
  // Forward to the websocket as a system-flavored chatMessage so the web
  // chat panel sees NPC speech, system warnings, exp ticks, etc. Pass
  // the raw ChatColor through so the client can colour the line with
  // full fidelity instead of falling back to the lossy MT collapse.
  emit chatMessage(static_cast<uint32_t>(mt), QString(), QString(), text,
                   out.message_color);
}

void MessageShell::formattedMessageEQL(const uint8_t* data, size_t len, uint8_t dir)
{
  // avoid client chatter
  if (dir == DIR_Client)
    return;

  auto out = seq::rust::decode_formatted_message(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;
  if (m_communicationMutationGuard && !m_communicationMutationGuard()) return;

  // EQL 0x15d0 (07/14): stock length-prefixed FormattedMessage — format_id@5,
  // msg_color@9 (message type / chat colour), positional args (the parser has
  // already dropped empty slots and reduced links to a readable name). Resolve
  // the eqstr template + interpolate %N exactly as the Live path does.
  QStringList args;
  args.reserve(static_cast<int>(out.args.size()));
  for (const auto& a : out.args)
    args.push_back(QString::fromUtf8(a.data(), a.size()));

  const QString text = m_eqStrings->formatMessage(out.format_id, args);
  if (text.isEmpty()) return;

  // v1: surface every formatted message as general chat carrying the wire
  // message-type/colour (@9) so the web's `cc:` colour space labels/categorises
  // it. Channel-splitting (spell/combat/system) by format id or msg_color is a
  // future refinement — the 07/14 layout dropped the msg_type@4 discriminator
  // and spellId@0 the old 3c0a routing keyed on.
  m_messages->addMessage(MT_General, text);
  emit chatMessage(static_cast<uint32_t>(MT_General), QString(), QString(),
                   text, out.message_color);
}

// OP_LootMessage: eql personal auto-loot text (links already reduced to the item
// name by the parser, which also hands back the id off the link header).
void MessageShell::lootMessage(const uint8_t* data, size_t len, uint8_t dir)
{
  if (dir == DIR_Client)
    return;
  auto out = seq::rust::decode_loot_message(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok || out.text.empty())
    return;
  const QString text = QString::fromUtf8(out.text.data(), out.text.size());
  if (!m_communicationMutationGuard || m_communicationMutationGuard()) {
    m_messages->addMessage(MT_General, text);
    emit chatMessage(static_cast<uint32_t>(MT_General), QString(), QString(),
                     text, out.color);
  }
  if (m_lootMutationGuard && !m_lootMutationGuard()) return;
  recordLoot(m_lootTracker->on_loot_message(out.color, out.text, out.item_id,
                                            out.item_name, nowMs()));
}

void MessageShell::lootTransaction(const uint8_t* data, size_t len, uint8_t dir)
{
  // Subcode 7 = item confirmation (sale proceeds), subcode 5 = the corpse's
  // coin pile; the parser rejects the request/ack subcodes on this id. Corpse
  // coin is auto-taken by the client, so both accrue.
  if (dir == DIR_Client)
    return;
  auto out = seq::rust::decode_loot_transaction(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok)
    return;
  if (out.coin_copper > 0)
    m_player->adjustMoney((int64_t)out.coin_copper);
  if (m_lootMutationGuard && !m_lootMutationGuard()) return;
  emit lootTransactionReceived(out.corpse_id, out.item_id, out.quantity,
                               out.coin_copper, out.from_corpse);
  recordLoot(m_lootTracker->on_loot_transaction(out, nowMs()));
}

// OP_LootDrops (0x6768): corpse loot window -> LootDrops proto (via SessionAdapter).
void MessageShell::lootDrops(const uint8_t* data, size_t len, uint8_t dir)
{
  if (dir == DIR_Client)
    return;
  auto out = seq::rust::decode_loot_drops(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok)
    return;
  if (m_lootMutationGuard && !m_lootMutationGuard()) return;
  QStringList names;
  QVector<uint32_t> icons;
  QVector<uint32_t> itemIds;
  names.reserve(static_cast<int>(out.items.size()));
  icons.reserve(static_cast<int>(out.items.size()));
  itemIds.reserve(static_cast<int>(out.items.size()));
  const int64_t ts = nowMs();
  for (const auto& it : out.items)
  {
    names.push_back(QString::fromUtf8(it.name.data(), it.name.size()));
    icons.push_back(it.icon);
    itemIds.push_back(it.item_id);
    recordLoot(m_lootTracker->on_loot_drop_item(out.corpse_id, out.corpse_name,
                                                it.name, it.icon, it.item_id, ts));
  }
  emit lootDropsReceived(out.corpse_id,
                         QString::fromUtf8(out.corpse_name.data(),
                                           out.corpse_name.size()),
                         names, icons, itemIds);
}

void MessageShell::simpleMessage(const uint8_t* data, size_t len, uint8_t dir)
{
  // avoid client chatter and do nothing if not viewing channel messages
  if (dir == DIR_Client)
    return;

  auto out = seq::rust::decode_simple_message(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;
  if (m_communicationMutationGuard && !m_communicationMutationGuard()) return;

  const MessageType mt = chatColor2MessageType(
      static_cast<ChatColor>(out.message_color));
  const QString text = stripEqItemLinks(m_eqStrings->message(out.message_format));
  m_messages->addMessage(mt, text);
  emit chatMessage(static_cast<uint32_t>(mt), QString(), QString(), text,
                   out.message_color);
}

void MessageShell::specialMessage(const uint8_t* data, size_t len, uint8_t dir)
{
  // avoid client chatter and do nothing if not viewing channel messages
  if (dir == DIR_Client)
    return;

  auto out = seq::rust::decode_special_message(
      rust::Slice<const uint8_t>{data, len});
  if (!out.ok) return;
  if (m_communicationMutationGuard && !m_communicationMutationGuard()) return;

  const Item* target = NULL;
  if (out.target)
    target = m_spawnShell->findID(tSpawn, out.target);

  const MessageType mt = chatColor2MessageType(
      static_cast<ChatColor>(out.message_color));
  const QString sender = QString::fromLatin1(out.source.data(), out.source.size());
  const QString targetName = target ? target->name() : QString();
  const QString text = stripEqItemLinks(
      QString::fromLatin1(out.message.data(), out.message.size()));

  if (target) {
    m_messages->addMessage(mt,
        QString("Special: '%1' -> '%2' - %3").arg(sender, targetName, text));
  } else {
    m_messages->addMessage(mt,
        QString("Special: '%1' - %2").arg(sender, text));
  }
  // Web chat panel keeps the structured fields (sender + target + text)
  // and renders however it likes; no string concatenation on the wire.
  emit chatMessage(static_cast<uint32_t>(mt), sender, targetName, text,
                   out.message_color);
}

// Per-client UCS channel-name XOR mask cache. The channel name's masked first
// char is `true ^ mask`, where `mask` is a per-session constant. Keyed by
// client addr (NOT per-MessageShell) so it survives a client's zone/box
// switches — the UCS session is one persistent connection, the same mask across
// zones — and is re-derived from EVERY General* record so it self-heals after a
// re-login (new session key). Single-threaded decode, so a plain static is safe.
static QHash<uint32_t, int> s_ucsChanMask;
// Channel names resolved from dominant-framing records, per client — used to
// recover the ~1% framing-outlier records (a data-dependent NUL in the masked
// header shifts field[4] mid-name) by suffix match.
static QHash<uint32_t, QSet<QString>> s_ucsKnownChans;

// Resolve one UCS channel name. `rest` = clean remainder from field[5..];
// `run` = the field's whole trailing printable run; `first` = masked byte at
// field[4]; `mask` = the per-session first-char XOR (-1 if unknown).
static QString ucsResolveChannel(uint8_t first, const QString& rest,
                                 const QString& run, int mask,
                                 QSet<QString>& known)
{
  // 1. Match the trailing run against a learned channel name — from /list
  //    rosters and join notices (which carry names un-masked) AND from earlier
  //    dominant-framing chat — by longest shared suffix (>= 5 chars, so a short
  //    coincidental tail can't false-match). This is the authoritative path: it
  //    resolves framing outliers AND records seen before the General* mask
  //    bootstraps (e.g. the very first chat line, once /list has been seen).
  {
    QString best;
    int bestLen = 4;
    for (const QString& k : known)
    {
      int l = 0;
      while (l < run.length() && l < k.length() &&
             run.at(run.length() - 1 - l) == k.at(k.length() - 1 - l))
        ++l;
      if (l > bestLen) { bestLen = l; best = k; }
    }
    if (!best.isEmpty())
      return best;
  }

  // 2. Dominant framing: the trailing run is the clean rest, optionally preceded
  //    by the (printable) masked first char at field[4] — i.e. run == rest
  //    (masked first char non-printable, dropped) or run == [first][rest].
  //    Mask-repair it and LEARN the name so later records (and outliers) match
  //    it in step 1.
  const bool dominant =
      (run == rest) ||
      (run.length() == rest.length() + 1 && run.mid(1) == rest);
  if (dominant)
  {
    QString name = rest;
    if (mask >= 0)
    {
      const uint8_t f = first ^ (uint8_t)mask;
      if (f >= 0x20 && f < 0x7f)             // valid repaired first char
        name.prepend(QChar((ushort)f));
    }
    if (name.length() >= 2 && name.at(0).isLetter() && name.at(0).isUpper())
      known.insert(name);
    return name;
  }

  // 3. Framing outlier with nothing learned yet — best effort: the raw run.
  return run;
}

void MessageShell::ucsChatMessage(const uint8_t* data, size_t len, uint8_t dir,
                                  uint32_t clientAddr)
{
  // Only the inbound (server->client) side carries chat; outgoing rides the
  // zone/world server. Rust does the keyless XOR + SPAM-anchored record parse.
  if (dir != DIR_Server || data == NULL || len < 12)
    return;
  if (m_communicationMutationGuard && !m_communicationMutationGuard()) return;

  auto recs = seq::rust::decode_ucs_chat(rust::Slice<const uint8_t>{data, len});

  // Learn full channel names from any /list roster / join notice in this packet
  // (they carry names un-masked), seeding the per-client resolver so even the
  // first chat line and framing outliers resolve without the General crib.
  for (const auto& name :
       seq::rust::decode_ucs_channels(rust::Slice<const uint8_t>{data, len}))
    s_ucsKnownChans[clientAddr].insert(
        QString::fromLatin1(name.data(), name.size()));

  for (const auto& r : recs)
  {
    const QString rest =
        QString::fromLatin1(r.channel_rest.data(), r.channel_rest.size());
    const QString run =
        QString::fromLatin1(r.channel_run.data(), r.channel_run.size());

    // Recover (and keep re-deriving) the per-session first-char XOR mask from
    // the auto-joined General* channel (clean remainder "eneral"), keyed by
    // client so it carries across that client's zone switches — no /list
    // needed. See OPCODES_LEGENDS.md and ucs_chat.rs.
    if (rest == QLatin1String("eneral"))
      s_ucsChanMask.insert(clientAddr, (int)(r.channel_first ^ (uint8_t)'G'));

    const QString channelName =
        ucsResolveChannel(r.channel_first, rest, run,
                          s_ucsChanMask.value(clientAddr, -1),
                          s_ucsKnownChans[clientAddr]);

    const QString sender =
        QString::fromLatin1(r.sender.data(), r.sender.size());
    QString text = QString::fromLatin1(r.message.data(), r.message.size());
    if (r.spam)
      text.prepend(QStringLiteral("(SPAM) "));

    // General channel chat; the literal channel rides channelName. chat_color
    // 0 (CC_Default) — UCS carries no per-line wire colour.
    emit chatMessage(MT_General, sender, QString(), text, 0, channelName);
  }
}

void MessageShell::inspectData(const uint8_t* data)
{
  const inspectDataStruct *inspt = (const inspectDataStruct *)data;
  QString tempStr;

  for (int inp = 0; inp < 21; inp ++)
  {
    if (strnlen(inspt->itemNames[inp], 64) > 0)
    {
      tempStr = QString::asprintf("He has %s (icn:%i)", inspt->itemNames[inp], inspt->icons[inp]);
      m_messages->addMessage(MT_Inspect, tempStr);
    }
  }

  if (strnlen(inspt->mytext, 200) > 0)
  {
    tempStr = QString::asprintf("His info: %s", inspt->mytext);
    m_messages->addMessage(MT_Inspect, tempStr);
  }

  emit inspectReceived(inspt);
}

void MessageShell::syncDateTime(const QDateTime& dt)
{
  QString dateString = dt.toString(pSEQPrefs->getPrefString("DateTimeFormat", "Interface", "ddd MMM dd,yyyy - hh:mm ap"));

  m_messages->addMessage(MT_Time, dateString);
}

// 9/30/2008 - no longer used. Group info is sent differently now
void MessageShell::groupUpdate(const uint8_t* data, size_t size, uint8_t dir)
{
  if (size != sizeof(groupUpdateStruct))
  {
    // Ignore groupFullUpdateStruct
    return;
  }
  return;
  const groupUpdateStruct* gmem = (const groupUpdateStruct*)data;
  QString tempStr;

  switch (gmem->action)
  {
    case GUA_Joined :
      tempStr = QString::asprintf ("Update: %s has joined the group.", gmem->membername);
      break;
    case GUA_Left :
      tempStr = QString::asprintf ("Update: %s has left the group.", gmem->membername);
      break;
    case GUA_LastLeft :
      tempStr = QString::asprintf ("Update: The group has been disbanded when %s left.",
         gmem->membername);
      break;
    case GUA_MakeLeader : 
      tempStr = QString::asprintf ("Update: %s is now the leader of the group.", 
         gmem->membername);
      break;
    case GUA_Started :
      tempStr = QString::asprintf ("Update: %s has formed the group.", gmem->membername);
      break;
    default :
       tempStr = QString::asprintf ("Update: Unknown Update action:%d - %s - %s)", 
		   gmem->action, gmem->yourname, gmem->membername);
  }

  m_messages->addMessage(MT_Group, tempStr);
}

void MessageShell::groupInvite(const uint8_t* data, size_t len, uint8_t dir)
{
  const groupInviteStruct* gmem = (const groupInviteStruct*)data;
  QString tempStr;

  if(dir == DIR_Client)
     tempStr = QString::asprintf("Invite: You invite %s to join the group", gmem->invitee);
  else
     tempStr = QString::asprintf("Invite: %s invites %s to join the group", gmem->inviter, gmem->invitee);

  m_messages->addMessage(MT_Group, tempStr);
}

void MessageShell::groupDecline(const uint8_t* data)
{
  const groupDeclineStruct* gmem = (const groupDeclineStruct*)data;
  QString tempStr;
  switch(gmem->reason)
  {
     case 1:
        tempStr = QString::asprintf("Invite: %s declines invite from %s (player is grouped)", 
                        gmem->membername, gmem->yourname);
        break;
     case 3:
        tempStr = QString::asprintf("Invite: %s declines invite from %s", 
                        gmem->membername, gmem->yourname);
        break;
     default:
        tempStr = QString::asprintf("Invite: %s declines invite from %s (unknown reason: %i)", 
                        gmem->membername, gmem->yourname, gmem->reason);
        break;
  }
  m_messages->addMessage(MT_Group, tempStr);
}

// TODO(chat-synthesis, live-verify): "X has joined the group",
// "X disbands", "X is now the leader" are rendered CLIENT-SIDE on modern EQ,
// not sent as chat — so the web panel never sees them. archive/test-client
// (commit b403896) synthesized them by emitting chatMessage() from the three
// group handlers below (they currently only call addMessage, which the WS sink
// ignores) AND fanned OP_GroupFollow / OP_GroupDisband(2) / OP_GroupLeader out
// to MessageShell in daemonapp's wiring. NOT ported active because it depends
// on the Test groupFollowStruct layout (name@0[16], vs this struct's legacy
// name@64) — re-verify the group struct offsets against a current Live capture
// before wiring, and do NOT apply the Test everquest.h struct rewrite blindly.
void MessageShell::groupFollow(const uint8_t* data)
{
  const groupFollowStruct* gFollow = (const groupFollowStruct*)data;
  QString tempStr;

  if(!strcmp(gFollow->invitee, m_player->name().toLatin1().data()))
     tempStr = "Follow: You have joined the group";
  else
     tempStr = QString::asprintf("Follow: %s has joined the group", gFollow->invitee);
  m_messages->addMessage(MT_Group, tempStr);
}

void MessageShell::groupDisband(const uint8_t* data)
{
  const groupDisbandStruct* gmem = (const groupDisbandStruct*)data;
  QString tempStr;

  tempStr = QString::asprintf ("Disband: %s disbands from the group", gmem->membername);
  m_messages->addMessage(MT_Group, tempStr);
}

void MessageShell::groupLeaderChange(const uint8_t* data)
{
   const groupLeaderChangeStruct *gmem = (const groupLeaderChangeStruct*)data;
   QString tempStr;
   tempStr = QString::asprintf("Update: %s is now the leader of the group", 
                    gmem->membername);
   m_messages->addMessage(MT_Group, tempStr);
}

void MessageShell::player(const charProfileStruct* player)
{
  QString message;

  message = QString::asprintf("Name: '%s' Last: '%s'", 
		  player->name, player->lastName);
  m_messages->addMessage(MT_Player, message);

  message = QString::asprintf("Level: %d", player->profile.level);
  m_messages->addMessage(MT_Player, message);
  
  message = QString::asprintf("PlayerMoney: P=%d G=%d S=%d C=%d",
		 player->profile.platinum, player->profile.gold, 
		 player->profile.silver, player->profile.copper);
  m_messages->addMessage(MT_Player, message);
  
  message = QString::asprintf("BankMoney: P=%d G=%d S=%d C=%d",
		  player->platinum_bank, player->gold_bank, 
		  player->silver_bank, player->copper_bank);
  m_messages->addMessage(MT_Player, message);

  message = QString::asprintf("CursorMoney: P=%d G=%d S=%d C=%d",
		  player->profile.platinum_cursor, player->profile.gold_cursor, 
		  player->profile.silver_cursor, player->profile.copper_cursor);
  m_messages->addMessage(MT_Player, message);

  message = QString::asprintf("SharedMoney: P=%d",
		  player->platinum_shared);
  m_messages->addMessage(MT_Player, message);

  message = QString::asprintf("DoN Crystals: Radiant=%d Ebon=%d",
          player->currentRadCrystals, player->currentEbonCrystals);
  m_messages->addMessage(MT_Player, message);

// charProfileStruct.exp hasn't been found
//   message = "Exp: " + Commanate(player->exp);
//   m_messages->addMessage(MT_Player, message);

  message = "ExpAA: (spent: " + Commanate(player->profile.aa_spent) + 
      ", unspent: " + Commanate(player->profile.aa_unspent) + ")";
  m_messages->addMessage(MT_Player, message);

#if 0 
  // Format for the aa values used to 0-1000 for group, 0-2000 for raid,
  // but now it's different. Just drop it for now. %%%
  message = "GroupLeadAA: " + Commanate(player->expGroupLeadAA) + 
      " (unspent: " + Commanate(player->groupLeadAAUnspent) + ")";
  m_messages->addMessage(MT_Player, message);
  message = "RaidLeadAA: " + Commanate(player->expRaidLeadAA) + 
      " (unspent: " + Commanate(player->raidLeadAAUnspent) + ")";
  m_messages->addMessage(MT_Player, message);
#endif

// 09/03/2008 patch - this is no longer sent in charProfile
//   message.sprintf("Group: %s %s %s %s %s %s", player->groupMembers[0],
//     player->groupMembers[1],
//     player->groupMembers[2],
//     player->groupMembers[3],
//     player->groupMembers[4],
//     player->groupMembers[5]);
//   m_messages->addMessage(MT_Player, message);

  int buffnumber;
  QString spellName;

  for (buffnumber=0;buffnumber<MAX_BUFFS;buffnumber++)
  {
    if (player->profile.buffs[buffnumber].spellid && 
            player->profile.buffs[buffnumber].duration)
    {
      const Spell* spell = m_spells->spell(player->profile.buffs[buffnumber].spellid);
      if(spell)
         spellName = spell->name();
      else
         spellName = spell_name(player->profile.buffs[buffnumber].spellid);

      if(player->profile.buffs[buffnumber].duration == -1)
        message = QString::asprintf("You have buff %s (permanent).", spellName.toLatin1().data());
      else
        message = QString::asprintf("You have buff %s duration left is %d in ticks.",
                spellName.toLatin1().data(), player->profile.buffs[buffnumber].duration);

      m_messages->addMessage(MT_Player, message);
    }
  }

  message = "LDoN Earned Guk Points: " + Commanate(player->ldon_guk_points);
  m_messages->addMessage(MT_Player, message);
  message = "LDoN Earned Mira Points: " + Commanate(player->ldon_mir_points);
  m_messages->addMessage(MT_Player, message);
  message = "LDoN Earned MMC Points: " + Commanate(player->ldon_mmc_points);
  m_messages->addMessage(MT_Player, message);
  message = "LDoN Earned Ruj Points: " + Commanate(player->ldon_ruj_points);
  m_messages->addMessage(MT_Player, message);
  message = "LDoN Earned Tak Points: " + Commanate(player->ldon_tak_points);
  m_messages->addMessage(MT_Player, message);
  message = "LDoN Unspent Points: " + Commanate(player->ldon_avail_points);
  m_messages->addMessage(MT_Player, message);
}

void MessageShell::increaseSkill(const uint8_t* data)
{
  const skillIncStruct* skilli = (const skillIncStruct*)data;
  QString tempStr;
  tempStr = QString::asprintf("Skill: %s has increased (%d)",
          skill_name(skilli->skillId).toLatin1().data(),
          skilli->value);
  m_messages->addMessage(MT_Player, tempStr);
}

void MessageShell::updateLevel(const uint8_t* data)
{
  const levelUpUpdateStruct *levelup = (const levelUpUpdateStruct *)data;
  QString tempStr;
  tempStr = QString::asprintf("NewLevel: %d", levelup->level);
  m_messages->addMessage(MT_Player, tempStr);
}
  
void MessageShell::consMessage(const uint8_t* data, size_t, uint8_t dir) 
{
  const considerStruct * con = (const considerStruct*)data;
  const Item* item;

  QString lvl("");
  QString hps("");
  QString cn("");
  QString deity;

  QString msg("Your faction standing with ");

  // is it you that you've conned?
  if (con->playerid == con->targetid) 
  {
    deity = m_player->deityName();
    
    // well, this is You
    msg += m_player->name();
  }
  else 
  {
    // find the spawn if it exists
    item = m_spawnShell->findID(tSpawn, con->targetid);
    
    // has the spawn been seen before?
    if (item != NULL)
    {
      Spawn* spawn = (Spawn*)item;

      // yes
      deity = spawn->deityName();

      lvl = QString::number(spawn->level());

      msg += item->name() + " (Lvl: " + lvl + ")";
    } // end if spawn found
    else
      msg += "Spawn:" + QString::number(con->targetid, 16);
  } // else not yourself
  
  switch (con->level) 
  {
  case 0:
  case 5:
  case 20:
    msg += " (even)";
    break;
  case 1:
    msg += " (grey)";
    break;
  case 2:
    msg += " (green)";
    break;
  case 4:
    msg += " (blue)";
    break;
  case 7:
  case 13:
    msg += " (red)";
    break;
  case 6:
  case 15:
    msg += " (yellow)";
    break;
  case 3:
  case 18:
    msg += " (cyan)";
    break;
  default:
    msg += " (unknown: " + QString::number(con->level) + ")";
    break;
  }

  if (!deity.isEmpty())
    msg += QString(" [") + deity + "]";

  msg += QString(" is: ") + print_faction(con->faction) + " (" 
    + QString::number(con->faction) + ")!";

  m_messages->addMessage(MT_Consider, msg);
} // end consMessage()


void MessageShell::setExp(uint32_t totalExp, uint32_t totalTick,
			  uint32_t minExpLevel, uint32_t maxExpLevel, 
			  uint32_t tickExpLevel)
{
    QString tempStr;
    tempStr = QString::asprintf("Exp: Set: %u total, with %u (%u/330) into level with %u left, where 1/330 = %u",
		    totalExp, (totalExp - minExpLevel), totalTick, 
		    (maxExpLevel - totalExp), tickExpLevel);
    m_messages->addMessage(MT_Player, tempStr);
}

void MessageShell::newExp(uint32_t newExp, uint32_t totalExp, 
			  uint32_t totalTick,
			  uint32_t minExpLevel, uint32_t maxExpLevel, 
			  uint32_t tickExpLevel)
{
  QString tempStr;
  uint32_t leftExp = maxExpLevel - totalExp;

  // only can display certain things if new experience is greater then 0,
  // ie. a > 1/330'th experience increment.
  if (newExp)
  {
    // calculate the number of this type of kill needed to level.
    uint32_t needKills = leftExp / newExp;

    tempStr = QString::asprintf("Exp: New: %u, %u (%u/330) into level with %u left [~%u kills]",
		    newExp, (totalExp - minExpLevel), totalTick, 
		    leftExp, needKills);
  }
  else
    tempStr = QString::asprintf("Exp: New: < %u, %u (%u/330) into level with %u left",
		    tickExpLevel, (totalExp - minExpLevel), totalTick, 
		    leftExp);
  
  m_messages->addMessage(MT_Player, tempStr);
}

void MessageShell::setAltExp(uint32_t totalExp,
			     uint32_t maxExp, uint32_t tickExp, 
			     uint32_t aaPoints)
{
  QString tempStr;
  tempStr = QString::asprintf("ExpAA: Set: %u total, with %u aapoints",
		  totalExp, aaPoints);

  m_messages->addMessage(MT_Player, tempStr);
}

void MessageShell::newAltExp(uint32_t newExp, uint32_t totalExp, 
			     uint32_t totalTick, 
			     uint32_t maxExp, uint32_t tickExp, 
			     uint32_t aapoints)
{
  QString tempStr;
  
  // only can display certain things if new experience is greater then 0,
  // ie. a > 1/330'th experience increment.
  if (newExp)
    tempStr = QString::asprintf("ExpAA: %u, %u (%u/330) with %u left",
		    newExp, totalExp, totalTick, maxExp - totalExp);
  else
    tempStr = QString::asprintf("ExpAA: < %u, %u (%u/330) with %u left",
		    tickExp, totalExp, totalTick, maxExp - totalExp);

  m_messages->addMessage(MT_Player, tempStr);
}

void MessageShell::addItem(const Item* item)
{
  uint32_t filterFlags = item->filterFlags();

  if (filterFlags == 0)
    return;

  QString prefix("Spawn");

  // first handle alert
  if (filterFlags & FILTER_FLAG_ALERT)
    filterMessage(prefix, MT_Alert, item);

  if (filterFlags & FILTER_FLAG_DANGER)
    filterMessage(prefix, MT_Danger, item);

  if (filterFlags & FILTER_FLAG_CAUTION)
    filterMessage(prefix, MT_Caution, item);

  if (filterFlags & FILTER_FLAG_HUNT)
    filterMessage(prefix, MT_Hunt, item);

  if (filterFlags & FILTER_FLAG_LOCATE)
    filterMessage(prefix, MT_Locate, item);
}

void MessageShell::delItem(const Item* item)
{
  // if it's an alert log the despawn
  if (item->filterFlags() & FILTER_FLAG_ALERT)
    filterMessage("DeSpawn", MT_Alert, item);
}

void MessageShell::killSpawn(const Item* item)
{
  // if it's an alert log the kill
  if (item->filterFlags() & FILTER_FLAG_ALERT)
    filterMessage("Died", MT_Alert, item);

  // if this is the player spawn, note the place of death
  if (item->id() != m_player->id())
    return;

  QString message;
  
  // use appropriate format depending on coordinate ordering
  if (!showeq_params->retarded_coords)
    message = "Died in zone '%1' at %2,%3,%4";
  else
    message = "Died in zone '%1' at %3,%2,%4";
  
  m_messages->addMessage(MT_Player, 
			 message.arg(m_zoneMgr->shortZoneName())
			 .arg(item->x()).arg(item->y()).arg(item->z()));
}

void MessageShell::filterMessage(const QString& prefix, MessageType type,
				 const Item* item)
{
  QString message;
  QString spawnInfo;

  // try to get a Spawn
  const Spawn* spawn = spawnType(item);

  // extra info if it is a spawn
  if (spawn)
    spawnInfo = QString::asprintf(" LVL %d, HP %d/%d", 
		      spawn->level(), spawn->HP(), spawn->maxHP());

  // use appropriate format depending on coordinate ordering
  if (!showeq_params->retarded_coords)
    message = "%1: %2/%3/%4 at %5,%6,%7%8";
  else
    message = "%1: %2/%3/%4 at %6,%5,%7%8";
  
  m_messages->addMessage(type, message.arg(prefix).arg(item->transformedName())
			 .arg(item->raceString()).arg(item->classString())
			 .arg(item->x()).arg(item->y()).arg(item->z())
			 .arg(spawnInfo));
}
