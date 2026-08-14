/*
 *  main.h
 *  Copyright 2001-2009, 2019 by the respective ShowEQ Developers
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

#ifndef _SHOWEQ_MAIN_H
#define _SHOWEQ_MAIN_H

#ifdef __FreeBSD__
#include <sys/types.h>
#else
#include <cstdint>
#endif
#include <cstdlib>
#include <deque>

#include "tomlpreferences.h"
extern class TomlPreferences *pSEQPrefs;

#include "config.h"

//----------------------------------------------------------------------
// bogus structure that should die soon
struct ShowEQParams
{
  bool           retarded_coords; // Verant style YXZ instead of XYZ
  bool           createUnknownSpawns;
  bool           keep_selected_visible;
  bool           pvp;
  bool		 deitypvp;
  bool           walkpathrecord;
  uint32_t       walkpathlength;
  bool           systime_spawntime;
  bool           showRealName;
  
  bool           saveZoneState;
  bool           savePlayerState;
  bool           saveSpawns;
  uint32_t       saveSpawnsFrequency;
  bool           restorePlayerState;
  bool           restoreZoneState;
  bool           restoreSpawns;
  QString        saveRestoreBaseFilename;
  bool           useUpdateRadius;
  uint8_t        filterZoneDataLog;
};
 
extern struct ShowEQParams *showeq_params;

#endif
