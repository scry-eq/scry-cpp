/*
 *  guildshell.h
 *  Copyright 2004-2007 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2004-2007, 2013, 2016, 2019 by the respective ShowEQ Developers
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

#include "guildshell.h"
#include "netstream.h"
#include "util.h"
#include "zonemgr.h"
#include "everquest.h"
#include "diagnosticmessages.h"

#include "seq-bridge-cxx/lib.h"

#include <QDateTime>
#include <QTextStream>


//----------------------------------------------------------------------
// diagnostic defines
// #define GUILDSHELL_DIAG 1

//----------------------------------------------------------------------
static const QString guildRanks[] = { "M", "O", "L", "?" };
static const QString bankRanks[] = { " ", "B" };
static const QString altRanks[] = { " ", "A" };
static const QString memberRanks[] = { "P", " " };

//----------------------------------------------------------------------
// GuildMember implementation
GuildMember::GuildMember(NetStream& netStream)
{
  // name
  m_name = netStream.readLPText ();

  // 4 byte level
  m_level = uint8_t(netStream.readUInt32NC());

  // 4 byte banker flag (0 = no, 1 = banker, 2 = alt, 3 = alt banker)
  m_banker = uint8_t(netStream.readUInt32NC());
  if (m_banker > 1)
  {
      m_alt = 1;
  }
  else
  {
      m_alt = 0;
  }
  m_banker = m_banker % 2;

  // 4 byte class
  m_class = uint8_t(netStream.readUInt32NC());

  // 4 byte rank (0 = member, 1 = officer, 2 = leader)
  m_guildRank = netStream.readUInt32NC();

  // 4 byte date/time for last on
  m_lastOn = time_t(netStream.readUInt32NC());

  // 1 byte guild tribute on/off (0 = off, 1 = on)
  m_guildTributeOn = netStream.readUInt8();

  // 1 byte guild trophy on/off (0 = off, 1 = on) Added 4/29/14
  m_guildTrophyOn = netStream.readUInt8();

  // 4 byte guild tribute total donated
  m_guildTributeDonated = netStream.readUInt32NC();

  // 4 byte guild tribute last donation timestamp
  m_guildTributeLastDonation = time_t(netStream.readUInt32NC());

  // 1 byte prospective member? flag (0=prospective, 1=full member) ??
  m_fullmember = netStream.readUInt8();

  // public note
  m_publicNote = netStream.readLPText();

  // 2 byte zoneInstance and zoneId for current zone
  m_zoneInstance = 0;
  m_zoneId = 0;

  // Unknown
  netStream.skipBytes(6);
}

GuildMember::GuildMember(const GuildRosterEntry& e)
  : m_name(e.name),
    m_banker(e.banker),
    m_level(e.level),
    m_class(e.classVal),
    m_classMask(e.classMask),
    m_guildRank(e.guildRank),
    m_lastOn(time_t(e.lastOn)),
    m_guildTributeOn(0),
    m_guildTrophyOn(0),
    m_guildTributeDonated(0),
    m_guildTributeLastDonation(0),
    m_alt(e.alt),
    m_fullmember(e.fullMember),
    m_publicNote(e.publicNote),
    m_zoneId(e.zoneId),
    // No backend sends a roster instance id; zoneId alone locates a member.
    m_zoneInstance(0)
{
}

GuildMember::~GuildMember()
{
}

void GuildMember::update(const GuildMemberUpdate* gmu)
{
  m_zoneId = gmu->zoneId;
  m_zoneInstance = gmu->zoneInstance;
  m_lastOn = gmu->lastOn;
#ifdef GUILDSHELL_DIAG
  QDateTime dt;
  QString dateFormat("ddd MMM dd hh:mm:ss yyyy");
  dt.setTime_t(m_lastOn);

  QString zone;
  zone = QString::number(m_zoneId);
  if (zoneInstance())
    zone += ":" + QString::number(m_zoneInstance);

  seqDebug("GuildShell: updated zone for member (member: %s, zone: %s, last on: %s", (const char*) m_name, (const char*) zone, (const char*) dt.toString(dateFormat));
#endif
}

QString GuildMember::classString() const
{
  return ::classString(m_class);
}

const QString& GuildMember::guildRankString() const
{
  if (m_guildRank <= 2)
    return guildRanks[m_guildRank];
  else
    return guildRanks[3]; // return the unknown rank character
}
 
const QString& GuildMember::bankRankString() const
{
    if (m_banker > 0)
    {
        return bankRanks[1];
    }
    else
    {
        return bankRanks[0];
    }
}

const QString& GuildMember::altRankString() const
{
    if (m_alt > 0)
    {
        return altRanks[1];
    }
    else
    {
        return altRanks[0];
    }
}

const QString& GuildMember::memberRankString() const
{
    if (m_fullmember > 0)
    {
        return memberRanks[1];
    }
    else
    {
        return memberRanks[0];
    }
}

//----------------------------------------------------------------------
// GuildShell implementation
GuildShell::GuildShell(ZoneMgr* zoneMgr, QObject* parent, const char* name)
  : QObject(parent),
    m_maxNameLength(0),
    m_zoneMgr(zoneMgr)
{
    setObjectName(name);
}

GuildShell::~GuildShell()
{
    qDeleteAll(m_members);
    m_members.clear();
}

QString GuildShell::zoneString(uint16_t zoneid) const
{
  if (zoneid == 0)
    return "";
   else if (zoneid == 65535)
    return "Anonymous";
  else
    return m_zoneMgr->zoneNameFromID(zoneid);
}

void GuildShell::dumpMembers(QTextStream& out)
{
  QDateTime dt;
  GuildMemberDictIterator it(m_members);
  GuildMember* member;

  QString format("%1 %2  %3 %4%5%6%7  %8  %9");
  QString dateFormat("ddd MMM dd hh:mm:ss yyyy");

  // calculate the maximum class name width
  int maxClassNameLength = 0;
  for (uint8_t i = 1; i <= PLAYER_CLASSES; i++)
    if (classString(i).length() > maxClassNameLength)
      maxClassNameLength = classString(i).length();

  out << "Guild has " << m_members.count() << " members: " << Qt::endl;

  int nameFieldWidth = - m_maxNameLength;
  int classFieldWidth = - maxClassNameLength;

  out << format.arg("Members", nameFieldWidth)
    .arg("Lv", 2).arg("Class", classFieldWidth)
    .arg("R", 1)
    .arg("B", 1)
    .arg("A", 1)
    .arg("P", 1)
    .arg("Last On", -24)
    .arg("Zone", -18);
  out << " Public Note" << Qt::endl;

  QString zone;
  while (it.hasNext())
  {
    it.next();
    member = it.value();

    dt.setSecsSinceEpoch(member->lastOn());
    zone = zoneString(member->zoneId());
    if (member->zoneInstance())
      zone += ":" + QString::number(member->zoneInstance());
    out << format.arg(member->name(), nameFieldWidth)
      .arg(member->level(), 2).arg(member->classString(), classFieldWidth)
      .arg(member->guildRankString(), 1)
      .arg(member->bankRankString(), 1)
      .arg(member->altRankString(), 1)
      .arg(member->memberRankString(), 1)
      .arg(dt.toString(dateFormat), -24)
      .arg(zone, -18);

    out << " " << member->publicNote() << Qt::endl;
  }
}


void GuildShell::setRoster(uint32_t guildId, const QVector<GuildRosterEntry>& rows)
{
  // Same shape as guildMemberList() below — the roster is authoritative and
  // replaces wholesale — but from already-decoded rows, so a backend whose wire
  // diverges from the stock struct decodes in its own parser and still lands
  // here. Signals fire in the same order, so subscribers can't tell them apart.
  emit cleared();
  qDeleteAll(m_members);
  m_members.clear();
  m_maxNameLength = 0;
  m_guildId = guildId;

  for (const GuildRosterEntry& e : rows)
  {
    if (e.name.isEmpty())
      continue;   // keyed by name; an unnamed row would collide on ""
    GuildMember* member = new GuildMember(e);
    m_members.insert(member->name(), member);
    if (size_t(member->name().length()) > m_maxNameLength)
      m_maxNameLength = member->name().length();
    emit added(member);
  }

  // Count only — a roster is a list of character names, so nothing identifying
  // goes to the log.
  seqInfo("GuildShell: roster for guild %u — %d members",
          m_guildId, int(m_members.count()));
  emit loaded();
}

void GuildShell::guildMOTD(const uint8_t* data, size_t len)
{
  // OP_GuildMOTD S>C. Decoded in Rust (the backend's own guild_motd parser),
  // never a C++ struct cast. The packet carries no guild id — setMotd stamps it
  // from the roster this shell tracks.
  auto m = seq::rust::decode_guild_motd(rust::Slice<const uint8_t>{data, len});
  if (!m.ok)
    return;
  setMotd(QString::fromLatin1(m.message.data(), int(m.message.size())),
          QString::fromLatin1(m.sender.data(), int(m.sender.size())));
}

void GuildShell::guildRoster(const uint8_t* data, size_t len)
{
  // OP_GuildMemberList S>C. Decoded in Rust (seq-decode's stock-layout parser);
  // an empty result is a decode failure — don't wipe a good roster over it.
  auto rows = seq::rust::decode_guild_roster(rust::Slice<const uint8_t>{data, len});
  if (rows.empty())
    return;

  QVector<GuildRosterEntry> out;
  out.reserve(int(rows.size()));
  for (const auto& r : rows)
  {
    GuildRosterEntry e;
    e.name       = QString::fromLatin1(r.name.data(), int(r.name.size()));
    e.level      = (uint8_t)r.level;
    e.classVal   = r.primary_class;
    e.classMask  = r.class_mask;   // 0 on Live (single-class)
    e.guildRank  = r.rank;
    e.lastOn     = r.last_on;
    e.banker     = r.banker;
    e.alt        = r.alt;
    e.fullMember = r.full_member;
    e.publicNote = QString::fromLatin1(r.public_note.data(), int(r.public_note.size()));
    e.zoneId     = r.zone_id;      // 0 on Live (roster carries no zone)
    out.append(e);
  }
  setRoster(rows[0].guild_id, out);
}

void GuildShell::setMotd(const QString& message, const QString& sender)
{
  m_motd = message;
  m_motdSender = sender;
  m_motdSet = true;
  // Length only — the MOTD text is user content and may name characters.
  seqInfo("GuildShell: MOTD for guild %u — %d chars", m_guildId, message.length());
  emit motdChanged();
}

void GuildShell::guildMemberList(const uint8_t* data, size_t len)
{
  // clear out any existing member data
  emit cleared();
  qDeleteAll(m_members);
  m_members.clear();

  m_maxNameLength = 0;

  // construct a netstream object on the data
  NetStream gml(data, len);
  
  // read the player name from the front of the stream
  QString player = gml.readLPText ();


  gml.skipBytes(4); // added 1/12/2013
  gml.skipBytes(4); // added 11/16/2016
  gml.skipBytes(1);

  // read the player count from the stream
  [[maybe_unused]] uint32_t count = gml.readUInt32NC();

#ifdef GUILDSHELL_DIAG
  seqDebug("Guild has %d members:", count);
#endif

  GuildMember* member;

#ifdef GUILDSHELL_DIAG
  QDateTime dt;
#endif // GUILDSHELL_DIAG

  // iterate over the data until we reach the end of it
  while (!gml.end())
  {
    // create a new guildmember initializing it from the NetStream
    member = new GuildMember(gml);

    // insert the new member into the dictionary
    m_members.insert(member->name(), member);

    // check for new longest member name
    if (static_cast<size_t>(member->name().length()) > m_maxNameLength)
      m_maxNameLength = member->name().length();

    emit added(member);

#ifdef GUILDSHELL_DIAG
    dt.setSecsSinceEpoch(member->lastOn());
    seqDebug("%-64s\t%d\t%s\t%d\t%s\t'%s'\t%s:%d",
	     (const char*)member->name(),
	     member->level(),
	     (const char*)classString(member->classVal()),
	     member->guildRank(), 
	     (const char*)dt.toString(),
	     (const char*)member->publicNote(),
	     (const char*)m_zoneMgr->zoneNameFromID(member->zoneId()),
	     member->zoneInstance());
#endif	     
  }

  emit loaded();

#ifdef GUILDSHELL_DIAG
  seqDebug("Finished processing %d guildmates. %d chars in longest name.", 
	   m_members.count(), m_maxNameLength);
#endif // 
}

void GuildShell::guildMemberUpdate(const uint8_t* data, size_t len)
{
  const GuildMemberUpdate* gmu = (const GuildMemberUpdate*)data;

  QString memberName = QString::fromUtf8(gmu->name);

  // find the member
  GuildMember* member = m_members[memberName];

  // update the guild members information
  if (member)
  {
    member->update(gmu);
    emit updated(member);
  }
  else
  {
#ifdef GUILDSHELL_DIAG
    seqDebug("GuildShell::guildMemberUpdate(): Failed to find '%s'(%d)!",
	     (const char*)memberName, memberName.length());
#if GUILDSHELL_DIAG > 1
    seqDebug("%d in members dict.", m_members.count());
    GuildMemberDictIterator it(m_members); // See QDictIterator
    for( ; it.current(); ++it )
	seqDebug("'%s'(%d): '%s'(%d)", 
		 (const char*)it.currentKey(), it.currentKey().length(),
		 (const char*)it.current()->name(), it.current()->name().length());
#endif // GUILDSHELL_DIAG > 1
#endif // GUILDSHELL_DIAG
  }
}
