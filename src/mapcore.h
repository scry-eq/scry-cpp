/*
 *  mapcore.h
 *  Portions Copyright 2001-2003 Zaphod (dohpaz@users.sourceforge.net).
 *  Copyright 2001-2004, 2019 by the respective ShowEQ Developers
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
//

// ZBTEMP: Note: Currently hardcoded to use int16_t type for point data, 
// should migrate this to a compiler define/typedef based value that 
// can be defined differently depending on the needs of other projects
//
#ifndef _MAPCORE_H
#define _MAPCORE_H

#include <cstdlib>
#include <cstdio>

#include <QString>
#include <QList>
#include <QVector>
#include <QPoint>
#include <QRect>

#include "seqcolor.h"

#include "mapcolors.h"
#include "point.h"
#include "pointarray.h"

///////////////////////////////////////////
// forward declarations
class MapData;
class MapLine;
class MapLocation;
class MapImage;
class MapLayer;

///////////////////////////////////////////
// type definitions
typedef Point3D<int16_t> MapPoint;
typedef Point3DArray<int16_t> MapPointArray;

//----------------------------------------------------------------------
// constants

//----------------------------------------------------------------------
// MapCommon
class MapCommon
{
 public:
  MapCommon() {}
  MapCommon(const QString& name, const QString& color)
    : m_name(name), m_colorName(color), m_color(SeqColor(color)) {}
  MapCommon(const QString& name, const SeqColor& color)
    : m_name(name), m_color(color) {}
  virtual ~MapCommon();

  const QString& name() const { return m_name; }
  const SeqColor& color() const { return m_color; }
  const SeqColor& origColor() const { return m_origColor; }
  QString colorName() const;

  void setName(const QString& name) { m_name = name; }
  void setColor(const QString& color) { m_color = SeqColor(color); }
  void setOrigColor(const SeqColor& color) { m_origColor = color; }


 private:
  QString m_name;
  QString m_colorName;
  SeqColor m_color;
  SeqColor m_origColor;
};

inline QString MapCommon::colorName() const
{
  // if a color name was specified, return it
  if (!m_colorName.isEmpty())
    return m_colorName;

  // otherwise return the string form of a SeqColor
  return m_color.name();
}

//----------------------------------------------------------------------
// MapLineL
class MapLineL : public MapCommon, public QVector<QPoint>
{
 public:
  MapLineL();
  MapLineL(const QString& name, const QString& color, uint32_t size);
  MapLineL(const QString& name, const QString& color, uint32_t size, int16_t z);
  virtual ~MapLineL();

  int16_t z() const { return m_z; }
  bool heightSet() const { return m_heightSet; }
  const QRect& boundingRect() const { return m_bounds; }

  void setZPos(uint16_t z)
    {  m_z = z; m_heightSet = true; }
  void calcBounds();

 private:
  int16_t m_z;
  bool m_heightSet;
  QRect m_bounds;
};

//----------------------------------------------------------------------
// MapLineM
class MapLineM : public MapCommon, public MapPointArray
{
 public:
  MapLineM();
  MapLineM(const QString& name, const QString& color, uint32_t size);
  MapLineM(const QString& name, const SeqColor& color, uint32_t size);
  MapLineM(const QString& name, const QString& color, const MapPoint& point);
  virtual ~MapLineM();

  const QRect& boundingRect() const { return m_bounds; }
  void calcBounds() { m_bounds = MapPointArray::boundingRect(); }

 private:
  QRect m_bounds;
};

//----------------------------------------------------------------------
// MapLocation
class MapLocation : public MapCommon, public MapPoint
{
 public:
  MapLocation();
  MapLocation(const QString& name, const QString& color, const QPoint& point);
  MapLocation(const QString& name, const QString& color, const MapPoint& point);
  MapLocation(const QString& name, const QString& color, 
	      int16_t x, int16_t y);
  MapLocation(const QString& name, const QString& color, 
	      int16_t x, int16_t y, int16_t z);
  MapLocation(const QString& name, const SeqColor& color, 
	      int16_t x, int16_t y, int16_t z);
  virtual ~MapLocation();
  bool heightSet() const { return m_heightSet; }

 private:
  bool m_heightSet;
};

//----------------------------------------------------------------------
// MapAggro
class MapAggro
{
 public:
  MapAggro();
  MapAggro(const QString& name, uint16_t range) : m_name(name), m_range(range) {}
  virtual ~MapAggro();

  const QString& name() { return m_name; }
  uint16_t range() { return m_range; }

  void setName(const QString& name) { m_name = name; }
  void setRange(uint16_t range);
 private:
  QString m_name;
  uint16_t m_range;
};

//----------------------------------------------------------------------
// MapLayer
class MapLayer
{
  public:
    MapLayer();
    ~MapLayer();
    void clear();
    QList<MapLineL*>& lLines() { return m_lLines; }
    QList<MapLineM*>& mLines() { return m_mLines; }
    QList<MapLocation*>& locations() { return m_locations; }
    void setFileName(QString fileName) { m_fileName = fileName; }
    QString fileName() const { return m_fileName; }
    bool mapLoaded() const { return m_mapLoaded; }
    void setMapLoaded(bool mapLoaded) { m_mapLoaded = mapLoaded; }

  private:
    QList<MapLineL*> m_lLines;
    QList<MapLineM*> m_mLines;
    QList<MapLocation*> m_locations;
    QString m_fileName;
    bool m_mapLoaded;

};

//----------------------------------------------------------------------
// MapData
class MapData
{
 public:
  // constructor/destructor
  MapData();
  ~MapData();

  // map loading/clearing
  void clear();
  void loadMap(const QString& fileName, bool import = false);
  void loadSOEMap(const QString& fileName, bool import = false);

  // accessors
  const QString& zoneShortName() const { return m_zoneShortName; }
  const QString& zoneLongName() const { return m_zoneLongName; }
  const QRect& boundingRect() const { return m_boundingRect; }
  const QSize& size() const { return m_size; }
  uint8_t zoneZEM() const { return m_zoneZEM; }
  int16_t minX() const { return m_minX; }
  int16_t minY() const { return m_minY; }
  int16_t maxX() const { return m_maxX; }
  int16_t maxY() const { return m_maxY; }
  MapLayer* mapLayer(uint8_t layerNum);
  uint8_t numLayers() const { return m_mapLayers.count(); }
  QList<MapAggro*>& aggros() { return m_aggros; }
  bool isAggro(const QString& name, uint16_t* range) const;
  uint16_t heightHintAbove() const { return m_heightHintAbove; }
  uint16_t heightHintBelow() const { return m_heightHintBelow; }

  // make sure map is big enough, returns true if size modified
  bool checkPos(int16_t x, int16_t y);
  void quickCheckPos(int16_t x, int16_t y);
  void updateBounds();

 private:
  int16_t m_minX;
  int16_t m_minY;
  int16_t m_maxX;
  int16_t m_maxY;
  QRect m_boundingRect;
  QSize m_size;
  QString m_zoneLongName;
  QString m_zoneShortName;
  QVector<MapLayer*> m_mapLayers;
  QList<MapAggro*> m_aggros;
  uint8_t m_zoneZEM;
  uint16_t m_heightHintAbove;
  uint16_t m_heightHintBelow;
};

inline
bool MapData::checkPos(int16_t x, int16_t y)
{
  bool flag = false;

#if defined(MAP_DEBUG)
  printf("in x: %i, in y: %i, max(%i,%i) Min(%i,%i)\n", x, y, m_maxX, m_maxY, m_minX, m_minY);
#endif /* MAP_DEBUG */

  if (x > m_maxX)
  {
    m_maxX = x;
    flag = true;
  }
  if (y > m_maxY)
  {
    m_maxY = y;
    flag = true;
  }
  if (x < m_minX)
  {
    m_minX = x;
    flag = true;
  }
  if (y < m_minY)
  {
    m_minY = y;
    flag = true;
  }

  // update the boundary information if bounds changed
  if (flag)
    updateBounds();

  return flag;
}

inline
void MapData::quickCheckPos(int16_t x, int16_t y)
{
  // quick, no-nonsense checking of the bounds, for batch use.
  // call updateBounds() after finished with the batch
  if (x > m_maxX)
    m_maxX = x;
  if (y > m_maxY)
    m_maxY = y;
  if (x < m_minX)
      m_minX = x;
  if (y < m_minY)
    m_minY = y;
}

inline
void MapData::updateBounds()
{
  // update the boundary information
  m_boundingRect =  QRect(QPoint(m_minX, m_minY), QPoint(m_maxX, m_maxY));
  m_size.setWidth(m_boundingRect.width());
  m_size.setHeight(m_boundingRect.height());
}

//----------------------------------------------------------------------
// assorted utility functions
inline bool inRect(const QRect& rect, 
		   const int16_t& x, 
		   const int16_t& y)
{
  return ((rect.left() <= x) && (rect.right() >= x) &&
	  (rect.top() <= y) && (rect.bottom() >= y));
}

inline bool inRoom(const int16_t& headRoom, 
		   const int16_t& floorRoom, 
		   const int16_t& z)
{
  return ((z <= headRoom) &&
	  (z >= floorRoom));
}

inline unsigned short getMapConvertColorIndex(const unsigned short r, const unsigned short g,
        const unsigned short b)
{
    unsigned short index = floor(r/80) + floor(g/80)*4 + floor(b/80)*16;
    if (index == 0)
        return 63;
    else
        return index;
}

inline QString getMapConvertColor(const unsigned short r, const unsigned short g,
        const unsigned short b)
{
    //adjust, convert, and return colors based on Razzle's original mapconvert script
    //This adjusts the SOE map colors to colors that work for SEQs default color scheme

    QString sColor[64] = {
        #define X(a,b) a,
        SEQMAP_COLOR_TABLE
        #undef X
    };

    unsigned short color = floor(r/80) + floor(g/80)*4 + floor(b/80)*16;

    if (color == 0) return sColor[63];

    return sColor[color];
}


#endif // _MAPCORE_H
