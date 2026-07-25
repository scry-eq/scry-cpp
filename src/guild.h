/*
 *  guild.h
 *  Copyright 2001 Fee (fee@users.sourceforge.net). All Rights Reserved.
 *  Copyright 2002-2003, 2009, 2019 by the respective ShowEQ Developers
 *
 *  Contributed to ShowEQ by fee (fee@users.sourceforge.net)
 *  for use under the terms of the GNU General Public License,
 *  incorporated herein by reference.
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

#ifndef _GUILD_H_
#define _GUILD_H_

#include <QObject>
#include <QString>
#include <QVector>
#include <vector>
#include <map>

#include "everquest.h"

// Target-neutral guild-in-zone row: (guildId, serverId) -> name, already decoded
// off the wire by the backend's own parser. The daemon never parses guild wire
// bytes in C++ — see GuildMgr::learnGuilds().
struct GuildInZoneEntry
{
  uint32_t guildId = 0;
  uint32_t serverId = 0;
  QString  name;
};

//------------------------------
// GuildMgr
class GuildMgr : public QObject
{
  Q_OBJECT

 public:

  GuildMgr(QString, QObject* parent = 0, const char* name = 0);

  ~GuildMgr();

  QString guildIdToName(uint16_t, uint16_t);

  // Learn id->name mappings from already-decoded rows (in wire order). New keys
  // are inserted, each firing guildTagUpdated so spawns re-tag; the list is
  // persisted once if anything changed. The wire is decoded in the backend's own
  // parser (eql: seq_backend_eql::guild_in_zone), never here.
  void learnGuilds(const QVector<GuildInZoneEntry>& rows);

 public slots:
  void readGuildList();
  void guildList2text(QString);
  void listGuildInfo();
  void writeGuildList();

 signals:
  void guildTagUpdated(uint32_t);

 private:
  std::map<uint32_t, QString> m_guildList;

  QString guildsFileName;

};

#endif // _GUILD_H_
