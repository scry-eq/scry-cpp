/*
 *  mapcore.cpp
 *  Portions Copyright 2001-2007 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2001-2007, 2019 by the respective ShowEQ Developers
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

// Author: Zaphod (dohpaz@users.sourceforge.net)
//    Many parts derived from existing ShowEQ/SINS map code

//
// NOTE: Trying to keep this file ShowEQ/Everquest independent to allow it
// to be reused for other Show{} style projects.  Any existing ShowEQ/EQ
// dependencies will be migrated out.
//

//#define DEBUGMAPLOAD

#include "mapcore.h"
#include "diagnosticmessages.h"

#include <cerrno>

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>
#include <QByteArray>
#include "tomlpreferences.h"

extern TomlPreferences* pSEQPrefs;

//----------------------------------------------------------------------
// MapParameters
MapParameters::MapParameters(const MapData& mapData)
  : m_mapData(&mapData),
    m_curPlayer(0, 0, 0),
    m_screenLength(600, 600)
{
  m_zoomDefault = 1;
  m_screenCenter = QPoint(300, 300);
  m_zoomMapLength = QSize(100, 100);
  m_panOffsetX = 0;
  m_panOffsetY = 0;
  m_ratio = 1.0;

  // calculate fixed point inverse ratio using the defiend qFormat 
  // for calculate speed purposes (* is faster than /)
  m_ratioIFixPt = fixPtToFixed<int, double>((1.0 / m_ratio), qFormat);

  m_targetPoint = MapPoint(0, 0, 0);
  m_targetPointSet = false;

  m_gridResolution = 500;
  m_gridTickColor = SeqColor(225, 200, 75);
  m_gridLineColor = SeqColor(75, 200, 75);
  m_backgroundColor = SeqColor(0, 0, 0);

  m_headRoom = 75;
  m_floorRoom = 75;

  m_mapLineStyle = tMap_Normal;
  m_showBackgroundImage = true;
  m_showLocations = true;
  m_showLines = true;
  m_showGridLines = true;
  m_showGridTicks = true;

  reset();
}

MapParameters::~MapParameters()
{
}

void MapParameters::reset()
{
  m_zoom = m_zoomDefault;
  m_panOffsetX = 0;
  m_panOffsetY = 0;
}

void MapParameters::reAdjust(MapPoint* targetPoint)
{
  if (targetPoint)
  {
    m_targetPoint = *targetPoint;
    m_targetPointSet = true;
  }
  else
  {
    m_targetPoint.setPoint(0, 0, 0);
    m_targetPointSet = false;
  }

  reAdjust();
}

void MapParameters::reAdjust()
{
  // get the map length
  const QSize& mapSize = m_mapData->size();

  if (m_zoom > 32)
    m_zoom = 32;

  // calculate zoomed map size
  int pxrat = ((mapSize.width()) / (m_zoom));
  int pyrat = ((mapSize.height()) / (m_zoom));

  // if it's a bit small, zoom out
  if ((pxrat <= 2) || (pyrat <= 2))
  {
     if (m_zoom > 1) 
     {
       m_zoom /= 2; 
       if (m_zoom == 1) 
	 clearPan();

       // recalculate zoomed map size
       pxrat = ((mapSize.width()) / (m_zoom));
       pyrat = ((mapSize.height()) / (m_zoom));
     }
  }

  // save zoomed map size
  m_zoomMapLength.setWidth(pxrat);
  m_zoomMapLength.setHeight(pyrat);

  double xrat = (double)m_screenLength.width() / m_zoomMapLength.width();
  double yrat = (double)m_screenLength.height() / m_zoomMapLength.height();

  int xoff = 0;
  int yoff = 0;

  if (xrat < yrat) 
  {
    m_zoomMapLength.setHeight((int)(m_screenLength.height() / xrat));
    if (m_zoom <= 1)
      yoff = (m_zoomMapLength.height() - pyrat) >> 1;
  } 
  else if (yrat) 
  {
    m_zoomMapLength.setWidth((int)(m_screenLength.width() / yrat));
    if (m_zoom <= 1)
      xoff = (m_zoomMapLength.width() - pxrat) >> 1;
  }

  // calculate the scaling ratio to use
  m_ratio = (double)m_zoomMapLength.width() / (double)m_screenLength.width();

  // calculate fixed point inverse ratio using the defined qFormat 
  // for calculate speed purposes (* is faster than /)
  m_ratioIFixPt = fixPtToFixed<int, double>((1.0 / m_ratio), qFormat);

  int iZMaxX, iZMinX, iZMaxY, iZMinY;

  // center on the target based on target
  if (m_targetPointSet)
  {
    iZMaxX = m_targetPoint.x() + m_panOffsetX +( m_zoomMapLength.width() >> 1);
    iZMinX = m_targetPoint.x() + m_panOffsetX - (m_zoomMapLength.width() >> 1);
    
    iZMaxY = m_targetPoint.y() + m_panOffsetY + (m_zoomMapLength.height() >> 1);
    iZMinY = m_targetPoint.y() + m_panOffsetY - (m_zoomMapLength.height() >> 1);
  }
  else
  {
    iZMaxX = m_panOffsetX + (m_zoomMapLength.width() >> 1);
    iZMinX = m_panOffsetX - (m_zoomMapLength.width() >> 1);
    
    iZMaxY = m_panOffsetY + (m_zoomMapLength.height() >> 1);
    iZMinY = m_panOffsetY - (m_zoomMapLength.height() >> 1);
  }

  // try not to have blank space on the sides
  if (iZMinX < m_mapData->minX())
  {
    iZMaxX -= (iZMinX - m_mapData->minX());
    iZMinX -= (iZMinX - m_mapData->minX());
  }
  if (iZMinY < m_mapData->minY())
  {
    iZMaxY -= (iZMinY - m_mapData->minY());
    iZMinY -= (iZMinY - m_mapData->minY());
  }

  if (iZMaxX > m_mapData->maxX())
  {
    iZMinX -= iZMaxX - m_mapData->maxX();
    iZMaxX -= iZMaxX - m_mapData->maxX();
  }
  if (iZMaxY > m_mapData->maxY())
  {
    iZMinY -= (iZMaxY - m_mapData->maxY());
    iZMaxY -= (iZMaxY - m_mapData->maxY());
  }

  // calculate the new screen center
  m_screenCenter.setX((((iZMaxX + xoff) * m_screenLength.width()) / 
		       m_zoomMapLength.width()));
  m_screenCenter.setY((((iZMaxY + yoff) * m_screenLength.height()) / 
		       m_zoomMapLength.height()));

  // calculate screen bounds
  m_screenBounds = QRect(int((m_screenCenter.x() - m_screenLength.width()) * 
			     m_ratio),
			 int((m_screenCenter.y() - m_screenLength.height()) * 
			     m_ratio),
			 int(m_screenLength.width() * m_ratio),
			 int(m_screenLength.height() * m_ratio));


  // adjust pre-calculate player offsets
  m_curPlayerOffset.setX(calcXOffset(m_curPlayer.x()));
  m_curPlayerOffset.setY(calcYOffset(m_curPlayer.y()));
}


void MapParameters::setPlayer(const MapPoint& pos)
{ 
  m_curPlayer = pos; 

  // re-calculate precomputed player head/floor room
  m_playerHeadRoom = m_curPlayer.z() + m_headRoom;
  m_playerFloorRoom = m_curPlayer.z() - m_floorRoom;

  m_curPlayerOffset.setX(calcXOffset(m_curPlayer.x()));
  m_curPlayerOffset.setY(calcYOffset(m_curPlayer.y()));
}

void MapParameters::setHeadRoom(int16_t headRoom)
{ 
  m_headRoom = headRoom; 

  // re-calculate precomputed player head/floor room
  m_playerHeadRoom = m_curPlayer.z() + m_headRoom;
}

void MapParameters::setFloorRoom(int16_t floorRoom) 
{ 
  m_floorRoom = floorRoom; 

  // re-calculate precomputed player head/floor room
  m_playerFloorRoom = m_curPlayer.z() - m_floorRoom;
}

bool MapParameters::isLayerVisible(uint8_t layerNum) const
{
    return m_layerVisibility & (1 << layerNum);
}

void MapParameters::setLayerVisibility(uint8_t layerNum, bool isVisible)
{
    if (isVisible)
        m_layerVisibility |= (1 << layerNum);
    else
        m_layerVisibility &= ~(1 << layerNum);
}


//----------------------------------------------------------------------
// MapCommon
MapCommon::~MapCommon()
{
}

//----------------------------------------------------------------------
// MapLineL
MapLineL::MapLineL()
  : MapCommon(), m_z(0), m_heightSet(false)
{
}

MapLineL::MapLineL(const QString& name,
		   const QString& color,
		   uint32_t size)
  : MapCommon(name, color),
    QVector<QPoint>(size),
    m_z(0),
    m_heightSet(false)
{
}

MapLineL::MapLineL(const QString& name,
		   const QString& color,
		   uint32_t size,
		   int16_t z)
  : MapCommon(name, color),
    QVector<QPoint>(size),
    m_z(z),
    m_heightSet(true)
{
}

MapLineL::~MapLineL()
{
}

void MapLineL::calcBounds()
{
  if (isEmpty())
  {
    m_bounds = QRect();
    return;
  }
  int minX = at(0).x();
  int maxX = minX;
  int minY = at(0).y();
  int maxY = minY;
  for (int i = 1; i < size(); ++i)
  {
    const QPoint& p = at(i);
    if (p.x() < minX) minX = p.x();
    if (p.x() > maxX) maxX = p.x();
    if (p.y() < minY) minY = p.y();
    if (p.y() > maxY) maxY = p.y();
  }
  m_bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

//----------------------------------------------------------------------
// MapLineM
MapLineM::MapLineM()
  : MapCommon()
{
}

MapLineM::MapLineM(const QString& name, const QString& color, uint32_t size)
  : MapCommon(name, color),
    MapPointArray(size)
{
}

MapLineM::MapLineM(const QString& name, const SeqColor& color, uint32_t size)
  : MapCommon(name, color),
    MapPointArray(size)
{
}

MapLineM::MapLineM(const QString& name, const QString& color, const MapPoint& point)
  : MapCommon(name, color),
    MapPointArray(1)
{
  // set the first point
  setPoint(0, point);
}

MapLineM::~MapLineM()
{
}

//----------------------------------------------------------------------
// MapLocation
MapLocation::MapLocation()
{
}

MapLocation::MapLocation(const QString& name, 
			 const QString& color, 
			 const QPoint& point)
  : MapCommon(name, color),
    MapPoint(point),
    m_heightSet(false)
{
}

MapLocation::MapLocation(const QString& name, 
			 const QString& color, 
			 const MapPoint& point)
  : MapCommon(name, color),
    MapPoint(point),
    m_heightSet(true)
{
}

MapLocation::MapLocation(const QString& name, 
			 const QString& color, 
			 int16_t x, 
			 int16_t y)
  : MapCommon(name, color),
    MapPoint(x, y, 0),
    m_heightSet(false)
{
}

MapLocation::MapLocation(const QString& name, 
			 const QString& color, 
			 int16_t x, 
			 int16_t y, 
			 int16_t z)
  : MapCommon(name, color),
    MapPoint(x, y, z),
    m_heightSet(true)
{
}

MapLocation::MapLocation(const QString& name,
			 const SeqColor& color,
			 int16_t x,
			 int16_t y,
			 int16_t z)
  : MapCommon(name, color),
    MapPoint(x, y, z),
    m_heightSet(true)
{
}

MapLocation::~MapLocation()
{
}

//----------------------------------------------------------------------
// MapAggro
MapAggro::~MapAggro()
{
}

//----------------------------------------------------------------------
// MapLayer
MapLayer::MapLayer()
{
  clear();
}

MapLayer::~MapLayer()
{
  clear();
}

void MapLayer::clear()
{

  m_mapLoaded = false;
  m_fileName = QString();

  qDeleteAll(m_lLines);
  m_lLines.clear();

  qDeleteAll(m_mLines);
  m_mLines.clear();

  qDeleteAll(m_locations);
  m_locations.clear();
}


//----------------------------------------------------------------------
// MapData
MapData::MapData()
{
  // clear the structure
  clear();
}

MapData::~MapData()
{

  qDeleteAll(m_mapLayers);
  m_mapLayers.clear();

  qDeleteAll(m_aggros);
  m_aggros.clear();
}

void MapData::clear()
{
  m_zoneLongName = "";
  m_zoneShortName = "";
  m_minX = -50;
  m_maxX = 50;
  m_minY = -50;
  m_maxY = 50;
  updateBounds();

  qDeleteAll(m_mapLayers);
  m_mapLayers.clear();

  qDeleteAll(m_aggros);
  m_aggros.clear();

  m_zoneZEM = 75;

  m_heightHintAbove = 0;
  m_heightHintBelow = 0;
}

MapLayer* MapData::mapLayer(uint8_t layerNum)
{
    if (layerNum < m_mapLayers.count())
        return m_mapLayers[layerNum];

    return NULL;
}

void MapData::loadMap(const QString& fileName, bool import)
{
  int16_t mx, my, mz;
  uint numPoints;
  int16_t globHeight=0;
  bool globHeightSet = false;
  int filelines = 1;  // number of lines in map file
  QString name;
  QString color;
  uint16_t rangeVal;
  uint32_t linePoints;
  uint32_t specifiedLinePoints;
  MapLineL* currentLineL = NULL;
  MapLineM* currentLineM = NULL;
  MapLayer* layer = NULL;

  // clear any existing map data (if not importing)
  if (!import)
    clear();

  /* Kind of stupid to try a non-existant map, don't you think? */
  if (fileName.contains("/.map") != 0)
    return;

  
  QFile mapFile(fileName);

  if (!mapFile.open(QIODevice::ReadOnly))
  {
    seqWarn("Error opening map file '%s'!", fileName.toLatin1().data());

    return;
  }

  layer = new MapLayer();

  // note the file name
  layer->setFileName(fileName);
    
  // allocate memory in a QByteArray to hold the entire file contents
  QByteArray textData(mapFile.size() + 1, '\0');
  
  // read the file as one big chunk
  mapFile.read(textData.data(), textData.size());
  
  // construct a regex to deal with either style line termination
  QRegularExpression lineTerm("[\r\n]{1,2}");

  // split the data into lines at the line termination. Use explicit
  // size to exclude textData's +1 NUL pad — leaks through as a stray
  // NUL-only "line" otherwise (Qt5 dropped it implicitly, Qt6 doesn't).
  QStringList lines =
      QString::fromUtf8(textData.constData(), mapFile.size())
          .split(lineTerm, Qt::SkipEmptyParts);


  // start iterating over the lines
  QStringList::Iterator lit = lines.begin();

  filelines = 1;

#ifdef DEBUGMAPLOAD
  seqDebug("Zone info line: %s", (const char*)(*lit));
#endif

  QString fieldSep = ",";

  // split the line into fields
  QStringList fields = lit->split(fieldSep);

  size_t count = fields.count();
  if (!count)
  {
    seqWarn("Error, no fields in first line of map file '%s'",
	    fileName.toLatin1().data());
    return;
  }
  
  if (count < 2)
  {
    seqWarn("Error, too few fields in first line of map file '%s'",
	    fileName.toLatin1().data());
    return;
  }

  // start iterator over the fields
  QStringList::Iterator fit = fields.begin();

  m_zoneLongName = (*fit++);
  m_zoneShortName = (*fit++);

  if (count > 2)
  {
    m_size.setWidth((*fit++).toInt());
    m_size.setHeight((*fit++).toInt());
  }

  // start looping at the next map line
  for (++lit; lit != lines.end(); ++lit)
  {
    // increment line count
    filelines++;
     
#ifdef DEBUGMAPLOAD
    seqWarn("Map line %d: %s", filelines, (const char*)*lit);
#endif

    // split the line into fields
    fields = lit->split(fieldSep);

    // skip empty lines
    if (fields.isEmpty())
      continue;

    // entry type is the first character of the line
    QChar entryType = fields.first().at(0);

    // pop the entry type off the front of the fields list
    fields.pop_front();

    // start at the first argument to the entry
    fit = fields.begin();

    // get the field count
    count = fields.count();

    bool ok;

    switch (entryType.toLatin1())
    {
    case 'M':
      {
#ifdef DEBUGMAPLOAD
	seqDebug("M record  [%d] [%d fields]: %s", 
		 filelines, count, (const char*)*lit);
#endif
	
	if (count < 3)
	{
	  seqWarn("Error reading M line %d on map '%s'! %d is too few fields",
		  filelines, fileName.toLatin1().data(), count);
	  continue;
	}
	
	// calculate the number of line points
	linePoints = ((count - 3) / 3);
	
	// only bother going forward if there will be enough line points
        if (linePoints < 2)
	{
	  seqWarn("M Line %d in map '%s' only had %d points, not loading.",
		  filelines, fileName.toLatin1().data(), linePoints );
	  continue;
	}
	
	// Line Name
	name = (*fit++);
	
	// Line Color
	color = (*fit++);
	if (color.isEmpty()) 
	  color = "#7F7F7F";
	
	// Number of points
	specifiedLinePoints = (*fit++).toUInt(&ok);
	if (!ok) 
	{
	  seqWarn("Error reading number of points on line %d in map '%s'!",
		  filelines, fileName.toLatin1().data());
	  continue;
	}
	
	if ((specifiedLinePoints != linePoints) && (specifiedLinePoints != 0))
	{
	  seqWarn("M Line %d in map '%s' has %d points as opposed to the %d points it specified!", 
		  filelines, fileName.toLatin1().data(), linePoints, specifiedLinePoints);
	}
	
	// create an M line
	currentLineM = new MapLineM(name, color, linePoints);

	numPoints = 0;
	while ((fit != fields.end()) && (numPoints < linePoints))
        {
	  // X coord
	  mx = (*fit++).toShort();
	  my = (*fit++).toShort();
	  mz = (*fit++).toShort();

	  // set the point data
	  currentLineM->setPoint(numPoints, mx, my, mz);
	  
	  // increment point count
	  numPoints++;
	}
	
	// calculate the XY bounds of the line
	currentLineM->calcBounds();
	
	// get the bounding rect
	const QRect& bounds = currentLineM->boundingRect();
	
	// adjust map boundaries based on the bounding rect
	quickCheckPos(bounds.left(), bounds.top());
	quickCheckPos(bounds.right(), bounds.bottom());
	
	// add it to the list of L lines
	layer->mLines().append(currentLineM);
      }
      break;
	
    case 'L':
      {
#ifdef DEBUGMAPLOAD
	seqDebug("L record  [%d] [%d fields] %s", 
		filelines, count, (const char*)*lit);
#endif
	
	if (count < 3)
        {
	  seqWarn("Error reading L line %d on map '%s'! %d is too few fields",
		  filelines, fileName.toLatin1().data(), count);
	  continue;
	}

	// calculate the number of line points
	linePoints = ((count - 3) >> 1);
	
	// only bother going forward if there will be enough line points
	if (linePoints < 2)
	{
	  seqWarn("L Line %d in map '%s' only had %d points, not loading.",
		  filelines, fileName.toLatin1().data(), linePoints);
	  continue;
	}
	
	// Line Name
	name = (*fit++);
	
	// Line Color
	color = (*fit++);
	if (color.isEmpty()) 
	  color = "#7F7F7F";
	
	// Number of points
	specifiedLinePoints = (*fit++).toUInt(&ok);
	if (!ok) 
	{
	  seqWarn("Error reading number of points on line %d in map '%s'!",
		  filelines, fileName.toLatin1().data());
	  continue;
	}
	
	if ((specifiedLinePoints != linePoints) && (specifiedLinePoints != 0))
	{
	   seqWarn("L Line %d in map '%s' has %d points as opposed to the %d points it specified!", 
		  filelines, fileName.toLatin1().data(), linePoints, specifiedLinePoints);
	}
	
	// create the appropriate style L line depending on if the global 
	// height has been set
	if (globHeightSet)
	  currentLineL = new MapLineL(name, color, linePoints, globHeight);
	else
	  currentLineL = new MapLineL(name, color, linePoints);

	numPoints = 0;
	
	// keep going until we run out of fields... 
	while ((fit != fields.end()) && (numPoints < linePoints))
        {
	  // X coord
	  mx = (*fit++).toShort();
	  
	  // Y coord
	  my = (*fit++).toShort();
	  
	  // store the point
	  (*currentLineL)[numPoints] = QPoint(mx, my);
	  
	  // increment point count
	  numPoints++;
	}
	
	// calculate the XY bounds of the line
	currentLineL->calcBounds();
	
	// get the bounding rect
	const QRect& bounds = currentLineL->boundingRect();
	
	// adjust map boundaries based on the bounding rect
	quickCheckPos(bounds.left(), bounds.top());
	quickCheckPos(bounds.right(), bounds.bottom());
	
	// add it to the list of L lines
	layer->lLines().append(currentLineL);
      }
      break;

    case 'P':
      {
#ifdef DEBUGMAPLOAD
	seqWarn("P record [%d] [%d fields]: %s", 
		filelines, count, (const char*)*lit);
#endif

	if (count < 4)
        {
	  seqWarn("Error reading P line %d on map '%s'! %d is too few fields",
		  filelines, fileName.toLatin1().data(), count);
	  continue;
	}

	name = (*fit++); // Location name
	color = (*fit++); // Location color
	mx = (*fit++).toShort();
	my = (*fit++).toShort();

	if (count == 5)
	{
	  mz = (*fit++).toShort();
	  
	  // add the appropriate style Location depending on if the global height is set
	  layer->locations().append(new MapLocation(name, color, mx, my, mz));
	}
	  
	// add the appropriate style Location depending on if the global 
	// height has been set
	if (globHeightSet)
	  layer->locations().append(new MapLocation(name, color, mx, my, globHeight));
	else
	  layer->locations().append(new MapLocation(name, color, mx, my));
	
	// adjust map boundaries
	quickCheckPos(mx, my);

	// Brewall P-lines use 8 comma-separated fields (count==7 after
	// pop_front): x, y, z, r, g, b, size, name. The last field (index 6)
	// may carry a height-filter hint such as "Height_Filter:_20/20".
	// Take the first hint found per map load; ignore duplicates.
	if (count == 7 && m_heightHintAbove == 0) {
	    const QString brewallName = fields.at(6).trimmed();
	    static const QRegularExpression hfRe(
		R"(^Height_Filter:_(\d+)/(\d+))",
		QRegularExpression::CaseInsensitiveOption);
	    const QRegularExpressionMatch hfm = hfRe.match(brewallName);
	    if (hfm.hasMatch()) {
		m_heightHintAbove = hfm.captured(1).toUShort();
		m_heightHintBelow = hfm.captured(2).toUShort();
	    }
	}
      }
      break;

    case 'A':  //Creates aggro ranges.
      {
#ifdef DEBUGMAPLOAD
	seqWarn("A record  [%d] [%d fields]: %s",
		filelines, count, (const char*)*lit);
#endif
	
	if (count < 2)
        {
	  seqWarn("Line %d in map '%s' has an A record with too few fields (%d)!",
		  filelines, fileName.toLatin1().data(), count);
	  break;
	}
	
	name = (*fit++);
	if (name.isEmpty()) 
        {
	  seqWarn("Line %d in map '%s' has an A marker with no Name expression!", 
		  filelines, fileName.toLatin1().data());
	  break;
	}
	rangeVal = (*fit++).toUShort();
	if (!rangeVal) 
        {
	  seqWarn("Line %d in map '%s' has an A marker with no or 0 Range radius!", 
		  filelines, fileName.toLatin1().data());
	  break;
	}
	
	// create and add a new aggro object
	m_aggros.append(new MapAggro(name, rangeVal));
	
	break;
      case 'H':  //Sets global height for L lines.
#ifdef DEBUGMAPLOAD
	seqDebug("H record [%d] [%d fields]: %s", 
		filelines, count, (const char*)*lit);
#endif
	
	if (count < 1)
        {
	  seqWarn("Line %d in map '%s' has an H record with too few fields (%d)!", 
		  filelines, fileName.toLatin1().data(), count);
	  break;
	}
	
	// get global height
	globHeight = (*fit++).toShort(&ok);
	if (!ok) 
        {
	  seqWarn("Line %d in map '%s' has an H marker with invalid Z!", 
		  filelines, fileName.toLatin1().data());
	  break;
	}
	globHeightSet = true;
      }
      break;

    case 'Z':  // Quick and dirty ZEM implementation
      {
#ifdef DEBUGMAPLOAD
	seqWarn("Z record [%d] [%d fields]: %s", 
		filelines, count, (const char*)*lit);
#endif
	
	if (count < 1)
        {
	  seqWarn("Line %d in map '%s' has a Z record with too few fields (%d)!", 
		  filelines, fileName.toLatin1().data(), count);
	  break;
	}
	
	m_zoneZEM = (*fit++).toUShort(&ok);
	if (!ok) 
        {
	  seqWarn("Line %d in map '%s' has an Z marker with invalid ZEM!", 
		  filelines, fileName.toLatin1().data());
	  break;
	}
#ifdef DEBUGMAPLOAD
	seqDebug("ZEM set to %d", m_zoneZEM);
#endif
      }
      break;

    }
  }

  // calculate the bounding rect
  updateBounds();

  m_mapLayers.append(layer);
  layer->setMapLoaded(true);

  seqInfo("Loaded map: '%s'", fileName.toLatin1().data());
}

void MapData::loadSOEMap(const QString& fileName, bool import)
{
  int16_t x1, y1, z1;
  int16_t x2, y2, z2;
  MapPoint src, dest, oldSrc;
  uint8_t r, g, b;
  uint32_t numPoints = 0;
  uint32_t checkPoint = 0;
  int filelines = 1;  // number of lines in map file
  QString name;
  MapLineM* currentLineM = 0;
  size_t count;
  MapLayer* layer = NULL;

  // if the same map is already loaded, don't reload it.
  for (int i = 0; i < m_mapLayers.count(); ++i)
  {
    if (m_mapLayers[i]->mapLoaded() && m_mapLayers[i]->fileName().toLower() == fileName.toLower())
        return;
  }


  // clear any existing map data if not importing
  if (!import)
    clear();

  /* Kind of stupid to try a non-existant map, don't you think? */
  if (fileName.contains("/.txt") != 0)
    return;

  QFile mapFile(fileName);

  if (!mapFile.open(QIODevice::ReadOnly))
  {
    seqWarn("Error opening map file '%s'!", fileName.toLatin1().data());

    return;
  }

  layer = new MapLayer();

  // note the file name 
  layer->setFileName(fileName);

  // allocate memory in a QByteArray to hold the entire file contents
  QByteArray textData(mapFile.size() + 1, '\0');

  // read the file as one big chunk
  mapFile.read(textData.data(), textData.size());

  // construct a regex to deal with either style line termination
  QRegularExpression lineTerm("[\r\n]{1,2}");

  // split the data into lines at the line termination. Use explicit
  // size to exclude textData's +1 NUL pad — leaks through as a stray
  // NUL-only "line" otherwise (Qt5 dropped it implicitly, Qt6 doesn't).
  QStringList lines =
      QString::fromUtf8(textData.constData(), mapFile.size())
          .split(lineTerm, Qt::SkipEmptyParts);


  // start iterating over the lines
  QStringList::Iterator lit = lines.begin();

  filelines = 1;

  QRegularExpression fieldSep(",\\s*");

  // split the line into fields
  QStringList fields;
  QStringList::Iterator fit;

  // use the file base name as the zone long/short name, it isn't perfect,
  // but neither is this file format
  QFileInfo fileInfo(fileName);
  QRegularExpression reStripTrailer("_[1-9]");
  
  m_zoneLongName = fileInfo.baseName().remove(reStripTrailer);
  m_zoneShortName = m_zoneLongName;

  // start looping at the next map line
  for (; lit != lines.end(); ++lit)
  {
    // increment line count
    filelines++;
     
#ifdef DEBUGMAPLOAD
    seqDebug("Map line %d: %s", filelines, (const char*)*lit);
#endif

    // entry type is the first character of the line
    QChar entryType = (*lit).at(0);

    // remove the entryType and the leading space
    (*lit).remove(0, 2);

    // split the line into fields
    fields = lit->split(fieldSep);

    // skip empty lines
    if (fields.isEmpty())
      continue;

    // start at the first argument to the entry
    fit = fields.begin();

    // get the field count
    count = fields.count();

    switch (entryType.toLatin1())
    {
    case 'L':
      {
#ifdef DEBUGMAPLOAD
	seqDebug("L record  [%d] [%d fields]: %s", 
		filelines, count, (const char*)*lit);
#endif
	
	if (count != 9)
	{
	  seqWarn("Error reading L line %d on map '%s'! %d is an incorrect field count!",
		  filelines, fileName.toLatin1().data(), count);
	  continue;
	}

	x1 = -int16_t((*fit++).toFloat());
	y1 = -int16_t((*fit++).toFloat());
	z1 = int16_t((*fit++).toFloat());
	x2 = -int16_t((*fit++).toFloat());
	y2 = -int16_t((*fit++).toFloat());
	z2 = int16_t((*fit++).toFloat());
	r = (*fit++).toUShort();
	g = (*fit++).toUShort();
	b = (*fit).toUShort();
	
	if (!currentLineM || 
	    !currentLineM->point(checkPoint).isEqual(x2, y2, z2) ||
        (
         ((currentLineM->color().r != r) ||
          (currentLineM->color().g != g) ||
          (currentLineM->color().b != b)) &&
         ((currentLineM->origColor().r != r) ||
          (currentLineM->origColor().g != g) ||
          (currentLineM->origColor().b != b))
        ))
	{
	  numPoints = 0;

	  // create an M line (start with 2 points because of SOE's lame
	  // format).
      unsigned short map_color_index = getMapConvertColorIndex(r, g, b);
      SeqColor lineColor(pSEQPrefs->getPrefString("MapColor" + QString::number(map_color_index),
              "MapColors", getMapConvertColor(r, g, b)));
	  currentLineM = new MapLineM("soe", lineColor, 2);
      currentLineM->setOrigColor(SeqColor(uint8_t(r), uint8_t(g), uint8_t(b)));

	  // set the first point
	  currentLineM->setPoint(numPoints++, x2, y2, z2);
	  
	  // set the second point
	  currentLineM->setPoint(checkPoint = numPoints++, x1, y1, z1);
	
	  // add it to the list of M lines
	  layer->mLines().append(currentLineM);
	}
	else 
	{
	  // if necessary, add room for a point
	  if (static_cast<uint32_t>(currentLineM->size()) < (numPoints+1))
	    currentLineM->resize(numPoints+1);

	  // add the point
	  currentLineM->setPoint(checkPoint = numPoints++, x1, y1, z1);
	}

	// calculate the XY bounds of the line
	currentLineM->calcBounds();
	
	// get the bounding rect
	const QRect& bounds = currentLineM->boundingRect();
	
	// adjust map boundaries based on the bounding rect
	quickCheckPos(bounds.left(), bounds.top());
	quickCheckPos(bounds.right(), bounds.bottom());
      }
      break;

    case 'P':
      {
#ifdef DEBUGMAPLOAD
	seqDebug("P record [%d] [%d fields]: %s", 
		filelines, count, (const char*)*lit);
#endif
	
	if (count < 8)
	{
	  seqWarn("Error reading P line %d on map '%s'! %d is an incorrect field count!",
		  filelines, fileName.toLatin1().data(), count);
	  continue;
	}

	// get all the fields
	x1 = -int16_t((*fit++).toFloat());
	y1 = -int16_t((*fit++).toFloat());
	z1 = int16_t((*fit++).toFloat());
	r = (*fit++).toUShort();
	g = (*fit++).toUShort();
	b = (*fit++).toUShort();
	fit++; // skip unknown
	// UPSTREAM (Xerxes/legacy): labels can contain the field separator,
	// e.g. "Nerissa_(Armor,Roam)", so rejoin remaining fields as the name.
	name.clear();
	for (; fit != fields.end(); ++fit)
	  name += (name.isEmpty() ? QString() : QString(",")) + *fit;

	// convert underscores to spaces.
	name.replace("_", " ");

	// add it to the list of locations
    unsigned short map_color_index = getMapConvertColorIndex(r, g, b);
    SeqColor lineColor(pSEQPrefs->getPrefString("MapColor" + QString::number(map_color_index),
            "MapColors", getMapConvertColor(r, g, b)));
    MapLocation* loc = new MapLocation(name, lineColor, x1, y1, z1);
    loc->setOrigColor(SeqColor(uint8_t(r), uint8_t(g), uint8_t(b)));
	layer->locations().append(loc);
	
	// adjust map boundaries
	quickCheckPos(x1, y1);
      }
      break;

    }
  }

  // calculate the bounding rect
  updateBounds();

  m_mapLayers.append(layer);
  layer->setMapLoaded(true);

  seqInfo("Loaded SOE map: '%s'", fileName.toLatin1().data());
}

void MapData::saveMap(const QString& fileName, const uint8_t layerNum) const
{
#ifdef DEBUG
  qDebug ("saveMap()");
#endif /* DEBUG */
  FILE * fh;
  uint32_t i;

  if ((fh = fopen(fileName.toLatin1().data(), "w")) == NULL)
  {
    seqWarn("Error saving map '%s'!", fileName.toLatin1().data());
    return;
  }

  // write out header info
  fprintf(fh, "%s,%s,%d,%d\n", m_zoneLongName.toLatin1().data(),
          m_zoneShortName.toLatin1().data(), m_size.width(), m_size.height());

  // write out ZEM info if non-default
  if (m_zoneZEM != 75)
    fprintf(fh, "Z,%i\n", m_zoneZEM);

  // write out the L (2D) lines with possible fixed Z
  bool heightSet = false;
  int16_t lastHeightSet = 0;
  MapLineL* currentLineL;
  QList<MapLineL*>::const_iterator mlit = m_mapLayers[layerNum]->lLines().begin();
  for (mlit = m_mapLayers[layerNum]->lLines().begin();
       mlit != m_mapLayers[layerNum]->lLines().end(); ++mlit)
  {
    currentLineL = *mlit;
    // was the global height set?
    if (currentLineL->heightSet())
    {
      // if no height was set previously, or this one doesn't match the previously
      // set height, write out an H record
      if ((!heightSet) || (lastHeightSet != currentLineL->z()))
      {
	// write out an H record.
	fprintf(fh, "H,%i\n", currentLineL->z());

	// note the last height set, and that it was set
	lastHeightSet = currentLineL->z();
	heightSet = true;
      }
    }

    // write out the start of the line info
    fprintf (fh, "L,%s,%s,%lld",
            currentLineL->name().toLatin1().data(),
            currentLineL->colorName().toLatin1().data(),
            (long long)currentLineL->size());

    // write out all the 2D points in the line
    for(i = 0; i < static_cast<uint32_t>(currentLineL->size()); i++)
    {
      QPoint curQPoint = currentLineL->at(i);
      fprintf (fh, ",%d,%d", curQPoint.x(), curQPoint.y());
    }

    // terminate the line
    fprintf (fh, "\n");
  }

  // write out the M (3D) lines
  MapLineM* currentLineM;
  QList<MapLineM*>::const_iterator mmit = m_mapLayers[layerNum]->mLines().begin();
  for (mmit = m_mapLayers[layerNum]->mLines().begin();
       mmit != m_mapLayers[layerNum]->mLines().end(); ++mmit)
  {
    currentLineM = *mmit;
    // write out the start of the line info
    fprintf (fh, "M,%s,%s,%lld",
            currentLineM->name().toLatin1().data(),
            currentLineM->colorName().toLatin1().data(),
            (long long)currentLineM->size());

    // write out all the 3D points in the line
    for(i = 0; i < static_cast<uint32_t>(currentLineM->size()); i++)
    {
      MapPoint curMPoint = currentLineM->point(i);

      fprintf (fh, ",%d,%d,%d",
              curMPoint.x(), curMPoint.y(), curMPoint.z());
    }
    // terminate the line
    fprintf (fh, "\n");
  }

  // write out location information
  QList<MapLocation*>::const_iterator lit = m_mapLayers[layerNum]->locations().begin();
  for(; lit != m_mapLayers[layerNum]->locations().end(); ++lit)
  {
    MapLocation* currentLoc = *lit;

    if (!currentLoc->heightSet())
      fprintf (fh, "P,%s,%s,%d,%d\n",
              currentLoc->name().toLatin1().data(),
              currentLoc->colorName().toLatin1().data(),
              currentLoc->x(),
              currentLoc->y());
    else
      fprintf (fh, "P,%s,%s,%d,%d,%d\n",
              currentLoc->name().toLatin1().data(),
              currentLoc->colorName().toLatin1().data(),
              currentLoc->x(),
              currentLoc->y(),
              currentLoc->z());
  }

  // write out aggro information
  QList<MapAggro*>::const_iterator ait = m_aggros.begin();
  for (; ait != m_aggros.end() && *ait != NULL; ++ait)
  {
    MapAggro* currentAggro = *ait;

    fprintf (fh, "A,%s,%d\n",
	     currentAggro->name().toLatin1().data(), currentAggro->range());
  }

#ifdef DEBUGMAP
  seqDebug("saveMap() - map '%s' saved with %d L lines, %d M lines, %d locations",
          fileName.toLatin1().data(),
          m_mapLayers[layerNum].m_lLines().count(), m_mapLayers[layerNum].m_mLines().count(),
          m_mapLayers[layerNum].m_locations().count());
#endif
  
  fclose (fh);

  seqInfo("Saved map: '%s'", fileName.toLatin1().data());
}

void MapData::saveSOEMap(const QString& fileName, const uint8_t layerNum) const
{
#ifdef DEBUG
  qDebug ("saveMap()");
#endif /* DEBUG */
  FILE * fh;
  uint i;

  if ((fh = fopen(fileName.toLatin1().data(), "w")) == NULL)
  {
    seqWarn("Error saving map '%s'!", fileName.toLatin1().data());
    return;
  }
  
  // write out the L (2D) lines with possible fixed Z
  uint8_t r, g, b;
  float z1;
  QString name;
  MapLineL* currentLineL;
  QList<MapLineL*>::const_iterator mlit = m_mapLayers[layerNum]->lLines().begin();
  for (; mlit != m_mapLayers[layerNum]->lLines().end(); ++mlit)
  {
    currentLineL = *mlit;
    z1 = float(currentLineL->z());

    const SeqColor& color = currentLineL->color().isValid() ? currentLineL->color() : currentLineL->origColor();
    r = color.r;
    g = color.g;
    b = color.b;

    QPoint lastQPoint = currentLineL->at(0);

    // write out all the 2D points in the line
    for(i = 1; i < static_cast<uint>(currentLineL->size()); ++i)
    {
      const QPoint& curQPoint = currentLineL->at(i);

      // write out the line info
      fprintf (fh, "L %.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %d, %d, %d\n", 
	       -float(curQPoint.x()), -float(curQPoint.y()), z1,
	       -float(lastQPoint.x()), -float(lastQPoint.y()), z1, 
	       r, g, b);
      
      lastQPoint = curQPoint;
    }

    // terminate the line
    fprintf (fh, "\n");
  }

  // write out the M (3D) lines)
  MapLineM* currentLineM;
  QList<MapLineM*>::const_iterator mmit = m_mapLayers[layerNum]->mLines().begin();
  for (; mmit != m_mapLayers[layerNum]->mLines().end(); ++mmit)
  {
    currentLineM = *mmit;
    const SeqColor& color = currentLineM->color().isValid() ? currentLineM->color() : currentLineM->origColor();
    r = color.r;
    g = color.g;
    b = color.b;

    MapPoint lastMPoint = currentLineM->point(0);

    // write out all the 3D points in the line
    for(i = 1; i < static_cast<uint>(currentLineM->size()); ++i)
    {
      const MapPoint& curMPoint = currentLineM->point(i);

      // write out the line info
      fprintf (fh, "L %.1f, %.1f, %.1f, %.1f, %.1f, %.1f, %d, %d, %d\n", 
	       -float(curMPoint.x()), -float(curMPoint.y()), float(curMPoint.z()),
	       -float(lastMPoint.x()), -float(lastMPoint.y()), float(lastMPoint.z()), 
	       r, g, b);
      
      lastMPoint = curMPoint;
    }
  }

  // write out location information
  QList<MapLocation*>::const_iterator lit = m_mapLayers[layerNum]->locations().begin();
  MapLocation* currentLoc;
  for (; lit != m_mapLayers[layerNum]->locations().end(); ++lit)
  {
    currentLoc = *lit;
    //TODO why is this zero?
    const SeqColor& color = currentLoc->color().isValid() ? currentLoc->color() : currentLoc->origColor();

    // convert spaces to underscores
    name = currentLoc->name();
    name.replace(" ", "_");

    fprintf(fh, "P %.1f, %.1f, %.1f, %d, %d, %d, 3,  %s\n",
            -float(currentLoc->x()), -float(currentLoc->y()), 
            float(currentLoc->z()),
            color.r, color.g, color.b,
            name.toLatin1().data());
  }
#ifdef DEBUGMAP
  seqDebug("saveMap() - map '%s' saved with %d L lines, %d M lines, %d locations",
          fileName.toLatin1().data(),
          m_mapLayers[layerNum]->m_lLines().count(), m_mapLayers[layerNum]->m_mLines().count(),
          m_mapLayers[layerNum]->m_locations().count());
#endif
  
  fclose (fh);

  seqInfo("Saved SOE map: '%s'", fileName.toLatin1().data());
}

void MapData::createNewLayer()
{
    MapLayer* layer = new MapLayer();
    QString fileName;

    if (m_mapLayers.count())
    {
        fileName = QString::asprintf("%s_%lld.map", m_zoneShortName.toLatin1().data(), (long long)m_mapLayers.count());
    }
    else
    {
        QDateTime now = QDateTime::currentDateTime();
        QString timestamp = now.toString("yyyyMMdd-hhmmss");
        fileName = QString::asprintf("%s.map", timestamp.toLatin1().data());
        m_zoneLongName = timestamp;
        m_zoneShortName = m_zoneLongName;
    }

    layer->setFileName(fileName);
    m_mapLayers.append(layer);
    layer->setMapLoaded(true);
    seqInfo("Create layer: '%s'", fileName.toLatin1().data());
}

bool MapData::isAggro(const QString& name, uint16_t* range) const
{
  MapAggro* aggro;
  QList<MapAggro*>::const_iterator ait = m_aggros.begin();
  for (; ait != m_aggros.end() && *ait != NULL; ++ait)
  {
    aggro = *ait;
    // does the name match this aggro?
    if (name.indexOf(aggro->name(), 0, Qt::CaseInsensitive) != -1)
    {
      if (range != NULL)
          *range = aggro->range();

      return true;
    }
  }

  return false;
}
