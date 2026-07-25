/*
 *  guild.cpp
 *  Copyright 2001 Fee (fee@users.sourceforge.net). All Rights Reserved.
 *  Portions Copyright 2001-2007, 2009, 2016, 2019 by the respective ShowEQ Developers
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

#include "guild.h"
#include "diagnosticmessages.h"

#include <QFile>
#include <QDataStream>
#include <QTextStream>


GuildMgr::GuildMgr(QString fn, QObject* parent, const char* name)
  : QObject(parent)
{
  setObjectName(name);
  guildsFileName = fn;

  readGuildList();
}

GuildMgr::~GuildMgr()
{
}

QString GuildMgr::guildIdToName(uint16_t guildID, uint16_t guildServerID)
{
    uint32_t key = guildServerID << 16 | guildID;
    //unguilded PCs have a guild ID of 0
    if (!guildID || !guildServerID | !m_guildList.count(key))
        return "";
    return m_guildList[key];
}


void GuildMgr::learnGuilds(const QVector<GuildInZoneEntry>& rows)
{
    // The rows arrive already decoded by the backend's own parser (both
    // OP_GuildsInZoneList and OP_NewGuildInZone reduce to this — the former a
    // list, the latter a single row). Insert only new keys, in wire order, so
    // the guildTagUpdated emissions match the old inline C++ parse exactly.
    bool need_save = false;
    for (const GuildInZoneEntry& e : rows)
    {
        const uint32_t key = e.serverId << 16 | e.guildId;
        if (!e.name.isEmpty() && !m_guildList.count(key))
        {
            m_guildList[key] = e.name;
            need_save = true;
            emit guildTagUpdated(e.guildId);
        }
    }

    if (need_save)
        writeGuildList();
}


void GuildMgr::writeGuildList()
{
  QFile guildsfile(guildsFileName);

  if (guildsfile.exists()) {
     if (!guildsfile.remove()) {
       seqWarn("GuildMgr: Could not remove old %s, unable to replace with server data!",
                guildsFileName.toLatin1().data());
        return;
     }
  }

  if(!guildsfile.open(QIODevice::WriteOnly))
    seqWarn("GuildMgr: Could not open %s for writing, unable to replace with server data!",
             guildsFileName.toLatin1().data());

  QTextStream guildDataStream(&guildsfile);

  for (auto itr = m_guildList.begin(); itr != m_guildList.end(); ++itr)
  {
     if (itr->first == 0)
         continue;
     QString line = QString::number(itr->first) + "|" + itr->second.toLatin1();
     guildDataStream << line.trimmed() << "\n";
  }

  guildsfile.close();
  seqInfo("GuildMgr: New guildsfile written");
}

void GuildMgr::readGuildList()
{
  QFile guildsfile(guildsFileName);
  m_guildList.clear();

  if (guildsfile.open(QIODevice::ReadOnly))
  {
     while (!guildsfile.atEnd())
     {
         QByteArray line = guildsfile.readLine();
         QList<QByteArray> line_parts = line.split('|');
         if (line_parts.size() != 2)
         {
             seqWarn("GuildMgr::readGuildList - skipping malformed line");
             continue;
         }
         uint32_t key = line_parts.at(0).toULong();
         QString guildName = line_parts.at(1).trimmed();
         m_guildList[key] = guildName;
         emit guildTagUpdated(key & 0x0000ffff);
     }

    guildsfile.close();
    seqInfo("GuildMgr: Guildsfile loaded");
  }
  else
    seqWarn("GuildMgr: Could not load guildsfile, %s", guildsFileName.toLatin1().data());
}

void GuildMgr::guildList2text(QString fn)
{
  QFile guildsfile(fn);
  QTextStream guildtext(&guildsfile);

    if (guildsfile.exists()) {
         if (!guildsfile.remove()) {
             seqWarn("GuildMgr: Could not remove old %s, unable to process request!",
                   fn.toLatin1().data());
           return;
        }
   }

   if (!guildsfile.open(QIODevice::WriteOnly)) {
     seqWarn("GuildMgr: Could not open %s for writing, unable to process request!",
              fn.toLatin1().data());
      return;
   }

   for (auto itr = m_guildList.begin(); itr != m_guildList.end(); ++itr)
   {
       if (itr->first == 0)
           continue;
       guildtext << itr->first << "\t" << itr->second << Qt::endl;
   }

   guildsfile.close();

   return;
}


void GuildMgr::listGuildInfo()
{
   for (auto itr = m_guildList.begin(); itr != m_guildList.end(); ++itr)
   {
     if (!itr->second.isNull())
       seqInfo("%d\t%s", itr->first, itr->second.toLatin1().data());
   }
}
