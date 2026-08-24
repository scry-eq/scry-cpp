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
#include "fixpt.h"

///////////////////////////////////////////
// forward declarations
class MapData;
class MapLine;
class MapLocation;
class MapParameters;
class MapImage;
class MapLayer;

///////////////////////////////////////////
// type definitions
typedef Point3D<int16_t> MapPoint;
typedef Point3DArray<int16_t> MapPointArray;

///////////////////////////////////////////
// enumerated types
enum MapLineStyle
{
  tMap_Normal,
  tMap_DepthFiltered,
  tMap_FadedFloors,
};


//----------------------------------------------------------------------
// constants

//----------------------------------------------------------------------
// MapParameters
class MapParameters
{
 public:
  // q format used for map fixed point math
  enum { qFormat = 14 };

 public:
  MapParameters(const MapData& mapData);
  ~MapParameters();

  // get methods
  const MapPoint& player() const { return m_curPlayer; }
  const QPoint& playerOffset() const { return m_curPlayerOffset; }
  int playerXOffset() const { return m_curPlayerOffset.x(); }
  int playerYOffset() const { return m_curPlayerOffset.y(); }
  int16_t playerHeadRoom() const { return m_playerHeadRoom; }
  int16_t playerFloorRoom() const { return m_playerFloorRoom; }
  const QSize& screenLength() const { return m_screenLength; }
  int screenLengthX() const { return m_screenLength.width(); }
  int screenLengthY() const { return m_screenLength.height(); }
  const QRect& screenBounds() const { return m_screenBounds; }
  const QPoint& screenCenter() const { return m_screenCenter; }
  int screenCenterX() const { return m_screenCenter.x(); }
  int screenCenterY() const { return m_screenCenter.y(); }
  const QSize& zoomMapLength() const { return m_zoomMapLength; }
  int zoomMapLengthX() const { return m_zoomMapLength.width(); }
  int zoomMapLengthY() const { return m_zoomMapLength.height(); }
  int panOffsetX() const { return m_panOffsetX; }
  int panOffsetY() const { return m_panOffsetY; }
  int zoom() const { return m_zoom; }
  int zoomDefault() const { return m_zoomDefault; }
  double ratio() const { return m_ratio; }
  double ratioX() const { return m_ratio; }
  double ratioY() const { return m_ratio; } 
  int ratioIFixPt() const { return m_ratioIFixPt; }
  bool isLayerVisible(uint8_t layerNum) const;

  int gridResolution() const { return m_gridResolution; }
  const SeqColor& gridLineColor() const { return m_gridLineColor; }
  const SeqColor& gridTickColor() const { return m_gridTickColor; }
  const SeqColor& backgroundColor() const { return m_backgroundColor; }
  int16_t headRoom() const { return m_headRoom; }
  int16_t floorRoom() const { return m_floorRoom; }
  MapLineStyle mapLineStyle() { return m_mapLineStyle; }
  bool fadeFloors() const { return (m_mapLineStyle == tMap_FadedFloors); }
  bool depthFiltering() const { return (m_mapLineStyle == tMap_DepthFiltered); }
  bool showBackgroundImage() const { return m_showBackgroundImage; }
  bool showLocations() const { return m_showLocations; }
  bool showLines() const { return m_showLines; }
  bool showGridLines() const { return m_showGridLines; }
  bool showGridTicks() const { return m_showGridTicks; }

  // utility methods
  int calcXOffset (int mapCoordinate) const;
  int invertXOffset (int worldX) const;
  int calcYOffset (int mapCoordinate) const;
  int invertYOffset (int worldY) const;

  int calcXOffsetI(int mapCoordinate) const;
  int calcYOffsetI(int mapCoordinate) const;

  // set/adjust methods
  void setPlayer(const MapPoint& pos);

  void setGridResolution(int res);
  void increaseGridResolution(void);
  void decreaseGridResolution(void);
  void setZoomDefault(int zoom);
  bool setZoom(int zoom);
  bool zoomIn();
  bool zoomOut();
  void panX(int xPan);
  void panY(int yPan);
  void panXY(int xPan, int yPan);
  void clearPan();
  void setPan(int xPan, int yPan);
  void setPanX(int xPan);
  void setPanY(int yPan);
  void setScreenSize(const QSize& size);
  void setBackgroundColor(const SeqColor& color) { m_backgroundColor = color; }
  void setMapLineStyle(MapLineStyle style) { m_mapLineStyle = style; }
  void setShowBackgroundImage(bool val) { m_showBackgroundImage = val; }
  void setShowLocations(bool val) { m_showLocations = val; }
  void setShowLines(bool val) { m_showLines = val; }
  void setShowGridLines(bool val) { m_showGridLines = val; }
  void setShowGridTicks(bool val) { m_showGridTicks = val; }
  void setGridLineColor(const SeqColor& color) { m_gridLineColor = color; }
  void setGridTickColor(const SeqColor& color) { m_gridTickColor = color; }
  void setHeadRoom(int16_t headRoom);
  void setFloorRoom(int16_t floorRoom);
  void setLayerVisibility(uint8_t layerNum, bool isVisible);
  
  void reAdjust(MapPoint* targetPoint);
  void reAdjust();
  
  void reset();
 private:
  const MapData* m_mapData;
  MapPoint m_curPlayer;
  QPoint m_curPlayerOffset;
  int16_t m_playerHeadRoom; 
  int16_t m_playerFloorRoom;
  QSize m_screenLength;
  QRect m_screenBounds;
  QPoint m_screenCenter;
  QSize m_zoomMapLength;
  uint32_t m_zoom;
  uint32_t m_zoomDefault;
  int m_panOffsetX;
  int m_panOffsetY;
  double m_ratio;
  int m_ratioIFixPt;

  // configuration parameters
  int m_gridResolution;
  SeqColor m_gridLineColor;
  SeqColor m_gridTickColor;
  SeqColor m_backgroundColor;
  int16_t m_headRoom;
  int16_t m_floorRoom;

  MapPoint m_targetPoint;
  bool m_targetPointSet;

  MapLineStyle m_mapLineStyle;
  bool m_showBackgroundImage; 
  bool m_showLocations;
  bool m_showLines;
  bool m_showGridLines;
  bool m_showGridTicks;

  uint32_t m_layerVisibility;
};

inline 
int MapParameters::calcXOffset (int mapCoordinate) const
{ 
  return screenCenterX() - (int)(mapCoordinate / m_ratio); 
}

inline 
int MapParameters::invertXOffset (int worldX) const
{
  return (int)rint((screenCenterX() - worldX) * m_ratio); 
}

inline 
int MapParameters::calcYOffset (int mapCoordinate) const
{ 
  return screenCenterY() - (int)(mapCoordinate / m_ratio); 
}

inline 
int MapParameters::invertYOffset (int worldY) const
{ 
  return (int)rint((screenCenterY() - worldY) * m_ratio); 
}

inline 
int MapParameters::calcXOffsetI(int mapCoordinate) const
{ 
  return screenCenterX() - 
    fixPtMulII(m_ratioIFixPt, qFormat, mapCoordinate);
}

inline 
int MapParameters::calcYOffsetI(int mapCoordinate) const
{ 
  return screenCenterY() - 
    fixPtMulII(m_ratioIFixPt, qFormat, mapCoordinate);
}

inline 
void MapParameters::setGridResolution(int res) 
{
  if ((res >= 50) && (res <= 1500))
    m_gridResolution = res; 
}

inline 
void MapParameters::increaseGridResolution(void)
{
  if (m_gridResolution <= 100)
    m_gridResolution = 50;
  else
    m_gridResolution -= 100;
  reAdjust();
}

inline 
void MapParameters::decreaseGridResolution(void)
{
  if (m_gridResolution >= 1500)
    return;
  
  m_gridResolution += 100;
  reAdjust();
}

inline 
bool MapParameters::setZoom(int zoom)
{
  if ((zoom <= 32) &&
      (zoom >= 1))
  {
    m_zoom = zoom;
    reAdjust();
    return true;
  }
  
  return false;
}

inline 
void MapParameters::setZoomDefault(int zoom)
{
  if ((zoom <= 32) &&
      (zoom >= 1))
    m_zoomDefault = zoom;
}

inline 
bool MapParameters::zoomIn() 
{ 
  if (m_zoom < 32) 
  {
    m_zoom *= 2; 
    reAdjust();
    return true;
  }
  return false;
}

inline 
bool MapParameters::zoomOut() 
{ 
  if (m_zoom > 1) 
  {
    m_zoom /= 2; 
    if (m_zoom == 1) 
      clearPan();
    
    reAdjust();
    
    return true;
  }
  return false;
}

inline 
void MapParameters::panX(int xPan) 
{ 
  m_panOffsetX += m_zoomMapLength.width() / xPan;
  reAdjust();
}

inline 
void MapParameters::panY(int yPan) 
{ 
  m_panOffsetY += m_zoomMapLength.height() / yPan; 
  reAdjust();
}

inline 
void MapParameters::panXY(int xPan, int yPan)
{
  m_panOffsetX += m_zoomMapLength.width() / xPan;
  m_panOffsetY += m_zoomMapLength.height() / yPan;
  reAdjust();
}

inline 
void MapParameters::clearPan() 
{ 
  m_panOffsetX = 0; 
  m_panOffsetY = 0; 
  reAdjust();
}

inline 
void MapParameters::setPan(int xPan, int yPan) 
{ 
  m_panOffsetX = xPan; 
  m_panOffsetY = yPan; 
  reAdjust();
}

inline 
void MapParameters::setPanX(int xPan)
{
  m_panOffsetX = xPan;
  reAdjust();
}

inline 
void MapParameters::setPanY(int yPan)
{
  m_panOffsetY = yPan;
  reAdjust();
}

inline 
void MapParameters::setScreenSize(const QSize& size) 
{ 
  m_screenLength = size; 
  reAdjust();
}

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

  // map loading/clearing/saving
  void clear();
  void loadMap(const QString& fileName, bool import = false);
  void loadSOEMap(const QString& fileName, bool import = false);
  void saveMap(const QString& fileName, const uint8_t layerNum) const;
  void saveSOEMap(const QString& fileName, const uint8_t layerNum) const;
  void createNewLayer();

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
