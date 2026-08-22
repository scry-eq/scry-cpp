#pragma once

#include <cstdint>

#include <QObject>
#include <QString>

struct zoneServerInfoStruct;

// Tracks the last-observed OP_ZoneServerInfo (world stream, S>C) so
// SessionAdapter can publish + resync the EQ zone server endpoint to
// connected web clients on each world->zone handoff. Cleared when the
// daemon detects a new EQ session (handled by SessionAdapter, not here).
class ZoneServerMgr : public QObject
{
    Q_OBJECT
public:
    explicit ZoneServerMgr(QObject* parent = nullptr);

    bool       hasInfo() const { return m_hasInfo; }
    QString    host()    const { return m_host; }
    quint16    port()    const { return m_port; }
    void applyZoneServerInfo(const QString& host, quint16 port);

public slots:
    void zoneServerInfo(const uint8_t* data);

signals:
    // Fires whenever an OP_ZoneServerInfo arrives. Always emits — even
    // if host/port match the previous (consumers decide whether the
    // duplicate matters).
    void zoneServerChanged(const QString& host, quint16 port);

private:
    bool    m_hasInfo = false;
    QString m_host;
    quint16 m_port = 0;
};
