/*
 *  datetimemgr.h
 *  Copyright 2003 Zaphod (dohpaz@users.sourceforge.net). All Rights Reserved.
 *  Copyright 2019 by the respective ShowEQ Developers
 *
 *  Contributed to ShowEQ by Zaphod (dohpaz@users.sourceforge.net) 
 *  for use under the terms of the GNU General Public License, 
 *  incorporated herein by reference.
 *
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

#ifndef _DATETIMEMGR_H_
#define _DATETIMEMGR_H_

#include <sys/types.h>
#include <cstdint>

#include <QObject>
#include <QDateTime>
#include <QTimer>

struct timeOfDayStruct;

class DateTimeMgr : public QObject
{
 Q_OBJECT

 public:
  DateTimeMgr(QObject* parent = 0, const char* name = 0);
  const QDateTime& eqDateTime() const;
  const QDateTime& updatedDateTime();
  int updateFrequency();
  void setUpdateFrequency(int seconds); // 3 seconds = 1 EQ minute
  void applyTimeOfDay(uint32_t year, uint32_t month, uint32_t day,
                      uint32_t wireHour, uint32_t minute);

 public slots:
  void timeOfDay(const uint8_t* tday);
  void update();

 signals:
  void syncDateTime(const QDateTime& dt);
  void updatedDateTime(const QDateTime& dt);
  // Raw shared-event fields. wireHour stays 1..24 for ordered shadow
  // comparison; syncDateTime carries the public seq.v1 0..23 value.
  void decodedTimeOfDay(uint32_t year, uint32_t month, uint32_t day,
                        uint32_t wireHour, uint32_t minute);

 protected:

 private:
  int m_updateFrequency;
  QTimer* m_timer;
  QDateTime m_eqDateTime;
  QDateTime m_refDateTime;
};

inline const QDateTime& DateTimeMgr::eqDateTime() const 
{ 
  return m_eqDateTime; 
}

inline const QDateTime& DateTimeMgr::updatedDateTime() 
{
  update(); 
  return m_eqDateTime; 
}

inline int DateTimeMgr::updateFrequency() 
{ 
  return m_updateFrequency / 1000; 
}

#endif // _DATETIMEMGR_H_
