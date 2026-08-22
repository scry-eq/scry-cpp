/*
 *  zonemgr.h
 *  Copyright 2001 Zaphod (dohpaz@users.sourceforge.net). All Rights Reserved.
 *  Copyright 2002-2012, 2019 by the respective ShowEQ Developers
 *
 *  Contributed to ShowEQ by Zaphod (dohpaz@users.sourceforge.net)
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

#ifndef ZONEMGR_H
#define ZONEMGR_H

#include <QObject>
#include <functional>
#include <optional>
#include <utility>
#include <vector>
#include <QString>

#include "point.h"

//----------------------------------------------------------------------
// forward declarations
struct ClientZoneEntryStruct;
struct ServerZoneEntryStruct;
struct charProfileStruct;
struct zoneChangeStruct;
struct newZoneStruct;
struct zonePointsStruct;
struct zonePointStruct;
struct dzSwitchInfo;

struct EntityZonePointState {
  std::optional<uint32_t> triggerId;
  std::optional<QString> actorDefinition;
  float x = 0;
  float y = 0;
  float z = 0;
  float heading = 0;
  std::optional<uint16_t> destinationZoneId;
  std::optional<uint16_t> destinationInstanceId;
};

class ZoneMgr : public QObject
{
  Q_OBJECT

 public:
  ZoneMgr(QObject* parent = 0, const char* name =0);
  virtual ~ZoneMgr();

  QString zoneNameFromID(uint16_t zoneId);
  QString zoneLongNameFromID(uint16_t zoneId);
  bool isZoning() const { return m_zoning; }
  const QString& shortZoneName() const { return m_shortZoneName; }
  const QString& longZoneName() const { return m_longZoneName; }
  const Point3D<int16_t>& safePoint() const { return m_safePoint; }
  float zoneExpMultiplier() { return m_zone_exp_multiplier; }
  const QString& zoneFile() const { return m_zoneFile; }
  float safeX() const { return m_safeX; }
  float safeY() const { return m_safeY; }
  float safeZ() const { return m_safeZ; }
  const zonePointStruct* zonePoint(uint32_t zoneTrigger);
  uint32_t dzID() { return m_dzID; }
  const Point3D<int16_t>& dzPoint() const { return m_dzPoint; }
  QString dzLongName() { return m_dzLongName; }
  uint32_t dzType() { return m_dzType; }

  // Target-neutral primitive for the eql backend (EqlDispatch): set the active
  // zone directly from OP_NewZone's decoded short/long names and emit
  // zoneResolved. Unused on live/test (they drive zones through
  // zoneNew/zoneChange). NOTE: this deliberately does NOT emit zoneChanged —
  // eql delivers the zone name AFTER the bulk spawn list + player profile (a
  // fresh box per zone-in), so a zoneChanged here would clear the just-loaded
  // spawns and reset the just-set identity. zoneResolved drives only the map /
  // filter / web, never the clear/reset slots. See eqldispatch.cpp.
  void setZoneByName(const QString& shortName, const QString& longName);
  void applyEntityZonePoints(std::vector<EntityZonePointState> points)
  { m_entityZonePoints = std::move(points); emit entityZonePointsChanged(); }
  const std::vector<EntityZonePointState>& entityZonePoints() const
  { return m_entityZonePoints; }
  // Raise the zoning flag without decoding a zone-change struct. For backends
  // whose zone-change packet carries no zone id (eql's is a client-only
  // position request), so only the flag is knowable here; names resolve at
  // zone-in via setZoneByName, which lowers it again.
  void beginZoning();
  void applyLifecycleTransition(const QString& characterName,
                                bool hasZoneId, uint32_t zoneId,
                                bool hasInstanceId, uint32_t instanceId,
                                bool confirmed);
  void applyLifecycleZone(const QString& shortName, const QString& longName);
  void applyLifecycleEnvironment(const QString& zoneFile,
                                 float experienceMultiplier,
                                 float safeX, float safeY, float safeZ);
  void setRustLifecycleProbe(std::function<bool()> probe)
  { m_rustLifecycleProbe = std::move(probe); }
  void setRustProfileAcceptedProbe(std::function<bool()> probe)
  { m_rustProfileAcceptedProbe = std::move(probe); }

 public slots:
  void saveZoneState(void);
  void restoreZoneState(void);

  // Packet handlers — public so the wiring TU can take their address for
  // EQPacketStream::on()/seqBind. Kept as slots for moc/signal compatibility.
  void zoneEntryClient(const uint8_t* zsentry, size_t, uint8_t);
  void zonePlayer(const uint8_t* zsentry, size_t len);
  void zoneChange(const uint8_t* zoneChange, size_t, uint8_t);
  void zoneNew(const uint8_t* zoneNew, size_t, uint8_t);
  void zonePoints(const uint8_t* zp, size_t, uint8_t);
  void dynamicZonePoints(const uint8_t *data, size_t len, uint8_t);
  void dynamicZoneInfo(const uint8_t *data, size_t len, uint8_t);

 protected slots:
  int32_t fillProfileStruct(charProfileStruct *player, const uint8_t *data, size_t len, bool checkLen);

 signals:
  void zoneBegin();
  void zoneBegin(const QString& shortZoneName);
  void zoneBegin(const ClientZoneEntryStruct* zsentry, size_t len, uint8_t dir);
  void playerProfile(const charProfileStruct* player);
  // Fields intentionally outside the Phase-4 lifecycle contract. In Rust
  // mode these continue through the legacy profile parser without allowing
  // that parser to own reset/identity/zone lifecycle state.
  void playerProfileSupplement(const charProfileStruct* player);
  void zoneChanged(const QString& shortZoneName);
  void zoneChanged(const zoneChangeStruct*, size_t, uint8_t);
  // Backend-neutral marker for a transition whose destination is not known
  // yet. EQL emits this from beginZoning(); Live/Test use zoneChanged.
  void zoneTransitionStarted();
  void zoneEnd(const QString& shortZoneName, const QString& longZoneName);
  // eql-only: the authoritative current-zone name (from OP_NewZone) is now
  // known. Drives map load / filter overlay / web ZoneChanged envelope WITHOUT
  // the spawn-clear + player-reset that ride on zoneChanged. Never emitted on
  // live/test.
  void zoneResolved(const QString& shortZoneName);
  void entityZonePointsChanged();
  
 private:
  void adoptLiveZoneNames(const QString& shortName, const QString& longName);
  std::function<bool()> m_rustLifecycleProbe;
  std::function<bool()> m_rustProfileAcceptedProbe;
  QString m_longZoneName;
  QString m_shortZoneName;
  bool m_zoning;
  std::vector<EntityZonePointState> m_entityZonePoints;
  Point3D<int16_t>  m_safePoint;
  float m_zone_exp_multiplier;
  QString m_zoneFile;
  float m_safeX = 0;
  float m_safeY = 0;
  float m_safeZ = 0;
  size_t m_zonePointCount;
  zonePointStruct* m_zonePoints;
  Point3D<int16_t>  m_dzPoint;
  uint32_t m_dzID;
  QString m_dzLongName;
  uint32_t m_dzType;
};

#endif // ZONEMGR
