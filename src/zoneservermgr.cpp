#include "zoneservermgr.h"
#include "everquest.h"

#include <cstring>

ZoneServerMgr::ZoneServerMgr(QObject* parent)
    : QObject(parent)
{
}

void ZoneServerMgr::zoneServerInfo(const uint8_t* data)
{
    if (!data) return;
    const auto* info = reinterpret_cast<const zoneServerInfoStruct*>(data);

    // host[128] is NUL-padded; defensive strnlen so a non-terminated
    // payload doesn't read past the struct.
    const size_t hostLen = ::strnlen(info->host, sizeof(info->host));
    applyZoneServerInfo(
        QString::fromLatin1(info->host, static_cast<int>(hostLen)), info->port);
}

void ZoneServerMgr::applyZoneServerInfo(const QString& host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_hasInfo = true;
    emit zoneServerChanged(m_host, m_port);
}
