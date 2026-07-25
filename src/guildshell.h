/*
 *  guildshell.h
 *  Copyright 2004 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2005-2006, 2014, 2019 by the respective ShowEQ Developers
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

#ifndef _GUILDSHELL_H_
#define _GUILDSHELL_H_

#include <cstdint>
#include <ctime>

#include <QString>
#include <QObject>
#include <QHash>
#include <QTextStream>

//----------------------------------------------------------------------
// forward declarations
class QTextStream;

class NetStream;
class ZoneMgr;

struct GuildMemberUpdate;

//----------------------------------------------------------------------
// GuildMember
// Target-neutral roster row: the caller has already decoded the wire, so no
// Legends types reach core. Mirrors what seq-backend-eql's roster parser
// recovers; see GuildShell::setRoster().
struct GuildRosterEntry
{
  QString  name;
  uint8_t  level = 0;
  // Backends that send a class id put it here directly; eql sends a multiclass
  // bitmask and the caller resolves its lowest set bit.
  uint8_t  classVal = 0;
  uint32_t classMask = 0;   // 0 where the backend has no multiclass concept
  uint32_t guildRank = 0;   // 0 member, 1 officer, 2 leader
  uint32_t lastOn = 0;      // unix seconds, 0 = never
  uint8_t  banker = 0;
  uint8_t  alt = 0;
  uint32_t fullMember = 0;
  QString  publicNote;
  uint16_t zoneId = 0;      // 0 = offline
};

class GuildMember
{
 public:
  GuildMember(NetStream& netStream);
  // Build from already-decoded fields (the eql path, whose wire diverges from
  // the stock struct the NetStream ctor above walks).
  explicit GuildMember(const GuildRosterEntry& e);
  ~GuildMember();

  void update(const GuildMemberUpdate* gmu);

  const QString& name() const { return m_name; }
  uint8_t level() const { return m_level; }
  uint8_t classVal() const { return m_class; }
  QString classString() const;
  uint32_t guildRank() const { return m_guildRank; }
  // EQL multiclass bitmask (bit N = class N); 0 on backends without one.
  uint32_t classMask() const { return m_classMask; }
  const QString& guildRankString() const;
  uint32_t bankRank() const { return m_banker; }
  const QString& bankRankString() const;
  uint32_t altRank() const { return m_alt; }
  const QString& altRankString() const;
  uint32_t memberRank() const { return m_fullmember; }
  const QString& memberRankString() const;
  time_t lastOn() const { return m_lastOn; }
  const QString& publicNote() const { return m_publicNote; }
  uint16_t zoneId() const { return m_zoneId; }
  uint16_t zoneInstance() const { return m_zoneInstance; }

 protected:
  QString m_name;
  uint8_t m_banker; // 0 = no, 1 = banker
  uint8_t m_level;
  uint8_t m_class;
  uint32_t m_classMask = 0; // EQL multiclass bitmask; 0 elsewhere
  uint32_t m_guildRank; // 0 = member, 1 = officer, 2 = leader
  time_t m_lastOn;
  uint8_t m_guildTributeOn; // 0 = off, 1 = on
  uint8_t m_guildTrophyOn; // 0 = off, 1 = on
  uint8_t m_guildTributeDonated;
  time_t m_guildTributeLastDonation;
  uint32_t m_alt; // 0 = no, 1 = alt
  uint32_t m_fullmember; // 0 = prospect, 1 = fullmember
  QString m_publicNote;
  uint16_t m_zoneId;
  uint16_t m_zoneInstance;
};

//----------------------------------------------------------------------
// GuildMemberDict
typedef QHash<QString, GuildMember*> GuildMemberDict;
typedef QHashIterator<QString, GuildMember*> GuildMemberDictIterator;

//----------------------------------------------------------------------
// GuildShell
class GuildShell : public QObject
{
  Q_OBJECT
 public:
  GuildShell(ZoneMgr* zoneMgr, QObject* parent = 0, const char* name = 0);
  ~GuildShell();
  const GuildMemberDict& members() { return m_members; }
  size_t maxNameLength() { return m_maxNameLength; }

  void dumpMembers(QTextStream& out);
  
  QString zoneString(uint16_t zoneid) const;

  // Neutral roster-apply primitive: replace the whole roster from already-decoded
  // rows. Used by backends whose wire diverges from the stock struct the
  // guildMemberList() slot below walks (eql). Emits the same cleared/added/loaded
  // signals, so downstream sees no difference. Unused on live/test.
  void setRoster(uint32_t guildId, const QVector<GuildRosterEntry>& rows);
  uint32_t guildId() const { return m_guildId; }

  // Neutral MOTD-apply primitive: the message + who set it, already decoded off
  // the wire (the packet carries no guild id — it's the player's own guild). The
  // guild id is taken from the roster this shell already tracks. Emits
  // motdChanged(). Unused on live/test.
  void setMotd(const QString& message, const QString& sender);
  const QString& motd() const { return m_motd; }
  const QString& motdSender() const { return m_motdSender; }
  bool hasMotd() const { return m_motdSet; }

 public slots:
  void guildMemberList(const uint8_t* data, size_t len);
  void guildMemberUpdate(const uint8_t* data, size_t len);

 signals:
  void cleared();
  void loaded();
  void added(const GuildMember* gm);
  void removed(const GuildMember* gm);
  void updated(const GuildMember* gm);
  void motdChanged();

 protected:

  GuildMemberDict m_members;
  size_t m_maxNameLength;
  ZoneMgr* m_zoneMgr;
  uint32_t m_guildId = 0;
  QString m_motd;
  QString m_motdSender;
  // Distinguishes "MOTD received, empty" from "no MOTD packet yet".
  bool m_motdSet = false;
};

#endif // _GUILDSHELL_H_
