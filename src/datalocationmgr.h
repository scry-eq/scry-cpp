/*
 *  datalocationmgr.h
 *  Copyright 2003 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2019 by the respective ShowEQ Developers
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

#ifndef _DATALOCATIONMGR_H_
#define _DATALOCATIONMGR_H_

#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QSet>

class DataLocationMgr
{
 public:
  DataLocationMgr(const QString& homeSubDir);
  // Same as above, but substitutes pkgDataOverride for the compile-time
  // PKGDATADIR slot. Used by --config-dir so a build-tree conf/ dir can
  // stand in for the install path without clobbering the user dir.
  DataLocationMgr(const QString& homeSubDir, const QString& pkgDataOverride);
  ~DataLocationMgr();
  bool setupUserDirectory();
  QFileInfo findExistingFile(const QString& subdir, const QString& filename,
			     bool caseSensitive = false, 
			     bool preferUser = true) const;
  QFileInfo findWriteFile(const QString& subdir, const QString& filename,
			  bool caseSensitive = true,
			  bool preferUser = true) const;

  QDir pkgDataDir(const QString& subdir) const;
  QDir userDataDir(const QString& subdir) const;

 protected:
  QFileInfo findFile(const QString& dir1, const QString& dir2,
		     const QString& subdir, const QString& filename,
		     bool caseSensitive = false) const;
  QFileInfo findFile(const QDir& dir, const QString& filename, 
		     bool caseSensitive = false, bool writable = false) const;
  QFileInfo findWriteFile(const QString& dir1, const QString& dir2,
			  const QString& subdir, const QString& filename,
			  bool caseSensitive = false) const;
  QDir findOrMakeSubDir(const QString& dir, const QString& subdir) const;

  QString m_pkgData;
  QString m_userData;
  // Pre-rename root (SEQ_LEGACY_DATA_NAMESPACE): READ-ONLY fallback for
  // findExistingFile. Empty disables it. Warn once per file — a silent
  // fallback makes the break untestable.
  QString m_legacyUserData;
  mutable QSet<QString> m_legacyReported;
};

#endif // _DATALOCATIONMGR_H_


