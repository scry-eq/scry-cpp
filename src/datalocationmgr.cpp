/*
 *  datalocationmgr.cpp
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

#include "datalocationmgr.h"
#include "diagnosticmessages.h"

#include <cstdio>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

// Under sudo, $HOME (and QDir::homePath) resolve to /root, not the invoking
// user's home — so a bare homeSubDir like ".scry" would silently land in
// /root/.scry instead of the user's actual data dir. SUDO_USER is set by
// sudo to the original username; use it to reconstruct the right path.
static QString invokingUserHome()
{
  const QByteArray sudoUser = qgetenv("SUDO_USER");
  if (!sudoUser.isEmpty() && qgetenv("USER") == "root")
    return QStringLiteral("/home/") + QString::fromLocal8Bit(sudoUser);
  return QDir::homePath();
}

DataLocationMgr::DataLocationMgr(const QString& homeSubDir)
  : DataLocationMgr(homeSubDir, QStringLiteral(PKGDATADIR))
{
}

DataLocationMgr::DataLocationMgr(const QString& homeSubDir,
                                 const QString& pkgDataOverride)
{
  m_pkgData = pkgDataOverride;

  // create the user directory object. Absolute paths are taken verbatim;
  // bare names are resolved against the invoking user's home (sudo-aware
  // — see invokingUserHome above).
  if (QDir::isAbsolutePath(homeSubDir))
    m_userData = homeSubDir;
  else
    m_userData = invokingUserHome() + "/" + homeSubDir;

  // Read-only fallback to the pre-rename root. An absolute homeSubDir (tests)
  // opts out, as does a legacy namespace resolving to the same place.
  const QString legacySubDir = QStringLiteral(SEQ_LEGACY_DATA_NAMESPACE);
  if (!legacySubDir.isEmpty() && !QDir::isAbsolutePath(homeSubDir) &&
      legacySubDir != homeSubDir)
    m_legacyUserData = invokingUserHome() + "/" + legacySubDir;
}

DataLocationMgr::~DataLocationMgr()
{
}

bool DataLocationMgr::setupUserDirectory()
{
  QFileInfo userDataDirInfo(m_userData);

  // does the user data directory exist?
  if (!userDataDirInfo.exists())
  {
    // no, then attempt to create it. Use mkpath (not mkdir) so a nested
    // namespace root like ~/.scry/eql creates its ~/.scry parent too —
    // Live's flat ~/.scry is unaffected (mkpath makes the single leaf).
    if (!QDir().mkpath(m_userData))
    {
      seqWarn("Failed to create '%s'\n", QDir(m_userData).absolutePath().toLatin1().data());
      return false;
    }
  }

  return true;
}

QFileInfo DataLocationMgr::findExistingFile(const QString& subdir, 
					  const QString& filename,
					  bool caseSensitive, 
					  bool preferUser) const
{
  // find the file using the preferred search ordering
  QFileInfo fileInfo;
  if (preferUser)
    fileInfo = findFile(m_userData, m_pkgData, subdir, filename, caseSensitive);
  else
    fileInfo = findFile(m_pkgData, m_userData, subdir, filename, caseSensitive);

  // Not in the current roots — try the pre-rename root.
  if (!fileInfo.exists() && !m_legacyUserData.isEmpty())
  {
    QDir dir(m_legacyUserData);
    if (dir.exists() && dir.cd(subdir))
    {
      const QFileInfo legacyInfo = findFile(dir, filename, caseSensitive, false);
      if (legacyInfo.exists())
      {
        const QString path = legacyInfo.absoluteFilePath();
        if (!m_legacyReported.contains(path))
        {
          m_legacyReported.insert(path);
          seqWarn("legacy data dir: read '%s' from pre-rename root '%s'; "
                  "move it under '%s' to drop the fallback.",
                  path.toLatin1().data(),
                  m_legacyUserData.toLatin1().data(),
                  m_userData.toLatin1().data());
        }
        return legacyInfo;
      }
    }
  }

  return fileInfo;
}

QFileInfo DataLocationMgr::findWriteFile(const QString& subdir, 
				       const QString& filename,
				       bool caseSensitive, 
				       bool preferUser) const
{
  if (preferUser)
    return findWriteFile(m_userData, m_pkgData, subdir, filename, 
			 caseSensitive);
  else
    return findWriteFile(m_pkgData, m_userData, subdir, filename,
			 caseSensitive);
}

QDir DataLocationMgr::pkgDataDir(const QString& subdir) const
{
  return findOrMakeSubDir(m_pkgData, subdir);
}

QDir DataLocationMgr::userDataDir(const QString& subdir) const
{
  return findOrMakeSubDir(m_userData, subdir);
}

QFileInfo DataLocationMgr::findFile(const QString& dir1, const QString& dir2,
				    const QString& subdir, 
				    const QString& filename,
				    bool caseSensitive) const
{
  // start out with an empty/invalid QFileInfo object
  QFileInfo fileInfo;

  // create a QDir object on the preferred data directory
  QDir dir(dir1);

  // if the preferred data directory exists and the cd into its subdir works, 
  // then attempt to find the file in the user directory.
  if (dir.exists() && dir.cd(subdir))
    fileInfo = findFile(dir, filename, caseSensitive, false);
  
  // check if a file has been found yet
  if (!fileInfo.exists())
  {
    // no file found yet, try the secondary data dir
    dir.setPath(dir2);
    
    // if the secondary data directory exists and the cd into its subdir 
    // works, then attempt to find the file in the user directory.
    if (dir.exists() && dir.cd(subdir))
      fileInfo = findFile(dir, filename, caseSensitive, false);
  }

  // return fileInfo
  return fileInfo;
}


QFileInfo DataLocationMgr::findFile(const QDir& dir, const QString& filename,
				    bool caseSensitive, bool writable) const
{
  // perform a case insensitive match if requested
  if (!caseSensitive)
  {
    // create a case insensitive regex; anchoredPattern wraps with \A...\z
    // to preserve QRegExp::exactMatch's whole-string semantics.
    QRegularExpression regex(QRegularExpression::anchoredPattern(filename),
                             QRegularExpression::CaseInsensitiveOption);

    // construct the filterspec to choose only the files with matching access
    QDir::Filters filterSpec;
    if (!writable)
      filterSpec = QDir::Files|QDir::Readable;
    else
      filterSpec = QDir::Files|QDir::Writable;
    
    // get all the matching file info entries
    const QFileInfoList fileInfoList = dir.entryInfoList(filterSpec);
    if (!fileInfoList.isEmpty())
    {
      //QFileInfoListIterator it(fileInfoList);
      QListIterator<QFileInfo> it(fileInfoList);
      QFileInfo fi;
      // iterate over the matches
      while (it.hasNext())
      {
          fi = it.next();
	// if found a match, then return it.
	if (regex.match(fi.fileName()).hasMatch())
	  return fi;
      }
    }
  }

  // if doing a case sensitive match or there were no matches, 
  // just return the file info object
  return QFileInfo(dir, filename);
}

QFileInfo DataLocationMgr::findWriteFile(const QString& dir1, 
					 const QString& dir2,
					 const QString& subdir, 
					 const QString& filename,
					 bool caseSensitive) const 
{
  // start out with an empty/invalid QFileInfo object
  QFileInfo fileInfo;

  // create a QDir object on the preferred data directory
  QDir dir(dir1);

  // if the preferred data directory exists and the cd into its subdir works, 
  // then attempt to find the file in it..
  if (dir.exists() && dir.cd(subdir))
    fileInfo = findFile(dir, filename, caseSensitive, true);
  
  // check if a writable file has been found yet, if so, return it.
  if (fileInfo.exists())
    return fileInfo;

  // ok, try to make it
  dir = findOrMakeSubDir(dir1, subdir);

  QFileInfo dirInfo(dir.absolutePath());
  
  // see if we found or made the subdir and it is writable...
  if (dirInfo.exists() && dirInfo.isDir() && dirInfo.isWritable())
    return QFileInfo(dir, filename);
  
  // no file found, nor able to make one under the preferred directory, 
  // so try the secondary data dir
  dir.setPath(dir2);
  
  // if the secondary data directory exists and the cd into its subdir 
  // works, then attempt to find the file in it.
  if (dir.exists() && dir.cd(subdir))
    fileInfo = findFile(dir, filename, caseSensitive, true);

  // check if a writable file has been found yet, if so, return it.
  if (fileInfo.exists())
    return fileInfo;

  // ok, try to make it
  dir = findOrMakeSubDir(dir2, subdir);

  dirInfo.setFile(dir.absolutePath());

  // see if we found or made the subdir and it is writable...
  if (dirInfo.exists() && dirInfo.isDir() && dirInfo.isWritable())
    return QFileInfo(dir, filename);

  // ok, last ditch desperation if the fileInfo doesn't exist yet 
  // which means neither directory exist/is writable, so try /tmp, /temp,
  // or /var/tmp
  dir = QDir::root();
  if (!dir.cd("tmp"))
    if (!dir.cd("temp"))
      if (!dir.cd("/var/tmp"))
          return QFileInfo(dir, filename); // can't catch a break

  // ok, try to make the subdirectory
  dir = findOrMakeSubDir(dir.absolutePath(), subdir);
  dirInfo.setFile(dir.absolutePath());

  // see if we found or made the subdir and it is writable...
  if (dirInfo.exists() && dirInfo.isDir() && dirInfo.isWritable())
    return QFileInfo(dirInfo.dir(), filename);

  // ok, just try and let the user drop it in whatever "tmp" dir was found
  fileInfo.setFile(dir, filename);

  // return fileInfo
  return fileInfo;
}

QDir DataLocationMgr::findOrMakeSubDir(const QString& dir,
				       const QString& subdir) const
{
  QDir dirDir(dir);
  
  // if the data directory exists, and the cd into its subdir works,
  // then just return it.
  if (dirDir.exists() && dirDir.cd(subdir))
    return dirDir;

  // if the parent directory doesn't exist, attempt to create it.
  if (!dirDir.exists())
    dirDir.mkdir(".");

  // attempt to create the directory;
  if (!dirDir.exists(subdir))
    dirDir.mkdir("./" + subdir);

  // attempt to cd into the directory that was theoretically just created.
  if (dirDir.cd(subdir))
    return dirDir;

  // return a QFileInfo, the caller can use its methods to determine the
  // directories status
  return QDir(QFileInfo(dirDir, subdir).absolutePath());
}
