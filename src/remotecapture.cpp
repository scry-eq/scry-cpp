/*
 *  remotecapture.cpp
 *  Copyright 2000-2024 by the respective ShowEQ Developers
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

#include <cerrno>
#include <cstring>
#include <ctime>
#include <vector>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "remotecapture.h"
#include "packetcapture.h"   // IP_ADDRESS_TYPE / MAC_ADDRESS_TYPE, DLT_EN10MB, AUTOMATIC_CLIENT_IP
#include "diagnosticmessages.h"

//----------------------------------------------------------------------
// SEQA v2 wire protocol (mirrors seq-agent's src/proto.rs, the normative spec).
// All ints little-endian. Every message carries a uniform length-prefixed
// envelope, so an unknown type is skipped by its length instead of desyncing
// the stream:
//   Envelope: magic[2]="SQ" ver(1)=2 type(1) len(u32) payload[len]
//     type 1 Frame       (agent->daemon) ts_micros(u64) origlen(u32) data[len-12]
//     type 2 ClientHello (daemon->agent) filter[len]
//     type 3 Hello       (agent->daemon) link_type(i32) snaplen(u32) filter[len-8]
// The captured length is len-12 rather than a field of its own, so it cannot
// contradict the bytes that follow.
namespace {
constexpr uint8_t SEQ_VERSION = 2;
constexpr uint8_t SEQ_TYPE_FRAME = 1;
constexpr uint8_t SEQ_TYPE_CLIENT_HELLO = 2;
constexpr uint8_t SEQ_TYPE_HELLO = 3;
constexpr size_t SEQ_ENVELOPE_LEN = 8;
constexpr size_t SEQ_FRAME_PREFIX = 12;   // ts_micros + origlen
constexpr size_t SEQ_HELLO_PREFIX = 8;    // link_type + snaplen
constexpr uint32_t SEQ_MAX_CAPLEN = 262144;   // reject absurd frame lengths
constexpr uint32_t SEQ_MAX_PAYLOAD = SEQ_MAX_CAPLEN + SEQ_FRAME_PREFIX;

inline uint32_t rdU32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
inline uint64_t rdU64(const uint8_t* p)
{
    return (uint64_t)rdU32(p) | ((uint64_t)rdU32(p + 4) << 32);
}
} // namespace

RemoteCaptureThread::RemoteCaptureThread(const QString& agentTarget)
    : m_port(9099),
      m_linkType(0),
      m_sock(-1),
      m_stop(false),
      m_started(false)
{
    // Split "host:port" on the LAST colon so bare IPv4 / hostnames work; an
    // explicit port overrides the 9099 default. (IPv6 literals would need
    // bracket syntax — out of scope for v1.)
    const int colon = agentTarget.lastIndexOf(':');
    if (colon > 0)
    {
        m_host = agentTarget.left(colon);
        const uint16_t p = agentTarget.mid(colon + 1).toUShort();
        if (p != 0)
            m_port = p;
    }
    else
    {
        m_host = agentTarget;
    }
}

RemoteCaptureThread::~RemoteCaptureThread()
{
    stop();
}

void RemoteCaptureThread::startOffline(const char*, int)
{
    seqWarn("RemoteCapture: offline playback is not supported by the remote "
            "agent source (use --replay / --replay-pcap instead)");
}

void RemoteCaptureThread::start(const char* device, const char* host,
        bool realtime, uint8_t address_type)
{
    // Compute the filter to request before the reader thread reads it.
    setFilter(device, host, realtime, address_type, 0, 0);

    m_pcache_closed = false;
    m_stop.store(false);

    seqInfo("RemoteCapture: connecting to seq-agent at %s:%u",
            m_host.toUtf8().constData(), m_port);

    pthread_create(&m_tid, NULL, loop, (void*)this);
    m_started = true;
}

void RemoteCaptureThread::stop()
{
    if (!m_started)
        return;

    m_stop.store(true);

    pthread_mutex_lock(&m_pcache_mutex);
    m_pcache_closed = true;
    pthread_mutex_unlock(&m_pcache_mutex);

    // Break the reader out of a blocking recv(); it owns and closes the fd.
    const int fd = m_sock.load();
    if (fd >= 0)
        ::shutdown(fd, SHUT_RDWR);

    pthread_join(m_tid, NULL);
    m_started = false;
}

void* RemoteCaptureThread::loop(void* param)
{
    ((RemoteCaptureThread*)param)->run();
    return NULL;
}

void RemoteCaptureThread::run()
{
    int backoffMs = 500;
    while (!m_stop.load())
    {
        const int fd = connectToAgent();
        if (fd < 0)
        {
            if (m_stop.load())
                break;
            backoffSleep(backoffMs);
            backoffMs = (backoffMs < 5000) ? backoffMs * 2 : 5000;
            continue;
        }

        m_sock.store(fd);
        if (sendClientHello(fd) && readHello(fd))
        {
            seqInfo("RemoteCapture: connected to agent %s:%u (link_type %d)",
                    m_host.toUtf8().constData(), m_port, m_linkType);
            backoffMs = 500;   // healthy link; reset the reconnect backoff
            const uint64_t got = pumpFrames(fd);   // returns on EOF / error / stop
            seqInfo("RemoteCapture: received %llu frames from agent",
                    (unsigned long long)got);
        }

        m_sock.store(-1);
        ::close(fd);

        if (m_stop.load())
            break;
        seqWarn("RemoteCapture: agent link down; reconnecting");
        backoffSleep(backoffMs);
        backoffMs = (backoffMs < 5000) ? backoffMs * 2 : 5000;
    }
}

int RemoteCaptureThread::connectToAgent()
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const QByteArray host = m_host.toUtf8();
    const QByteArray port = QByteArray::number(m_port);

    struct addrinfo* res = NULL;
    const int grc = getaddrinfo(host.constData(), port.constData(), &hints, &res);
    if (grc != 0)
    {
        seqWarn("RemoteCapture: cannot resolve %s: %s",
                host.constData(), gai_strerror(grc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* ai = res; ai != NULL; ai = ai->ai_next)
    {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd >= 0)
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}

bool RemoteCaptureThread::sendClientHello(int fd)
{
    const QByteArray filter = m_filter.toUtf8();
    if ((uint32_t)filter.size() > SEQ_MAX_PAYLOAD)
        return false;

    QByteArray msg;
    msg.append("SQ", 2);
    msg.append((char)SEQ_VERSION);
    msg.append((char)SEQ_TYPE_CLIENT_HELLO);
    const uint32_t len = (uint32_t)filter.size();
    for (int i = 0; i < 4; ++i)
        msg.append((char)((len >> (8 * i)) & 0xff));
    msg.append(filter);

    size_t off = 0;
    while (off < (size_t)msg.size())
    {
        if (m_stop.load())
            return false;
        const ssize_t w = ::send(fd, msg.constData() + off, msg.size() - off, MSG_NOSIGNAL);
        if (w > 0)
            off += (size_t)w;
        else if (w < 0 && errno == EINTR)
            continue;
        else
            return false;
    }
    return true;
}

// Read one envelope + its payload. `payload` is reused across calls so the
// frame loop doesn't reallocate per packet.
bool RemoteCaptureThread::readMsg(int fd, uint8_t& type, std::vector<uint8_t>& payload)
{
    uint8_t env[SEQ_ENVELOPE_LEN];
    if (!readFull(fd, env, sizeof(env)))
        return false;
    if (env[0] != 'S' || env[1] != 'Q')
    {
        seqWarn("RemoteCapture: bad SEQA magic from agent");
        return false;
    }
    if (env[2] != SEQ_VERSION)
    {
        seqWarn("RemoteCapture: unsupported SEQA version %u (expected %u)", env[2], SEQ_VERSION);
        return false;
    }
    type = env[3];
    const uint32_t len = rdU32(env + 4);
    if (len > SEQ_MAX_PAYLOAD)
    {
        seqWarn("RemoteCapture: SEQA payload %u out of range; dropping link", len);
        return false;
    }
    payload.resize(len);
    return len == 0 || readFull(fd, payload.data(), len);
}

bool RemoteCaptureThread::readHello(int fd)
{
    std::vector<uint8_t> payload;
    uint8_t type = 0;

    // Anything ahead of the Hello is skipped by its envelope length rather than
    // treated as fatal — that tolerance is why the envelope exists.
    do
    {
        if (!readMsg(fd, type, payload))
            return false;
        if (type != SEQ_TYPE_HELLO)
            seqDebug("RemoteCapture: skipping SEQA type %u before hello", type);
    } while (type != SEQ_TYPE_HELLO);

    if (payload.size() < SEQ_HELLO_PREFIX)
    {
        seqWarn("RemoteCapture: SEQA hello shorter than its header");
        return false;
    }
    m_linkType = (int32_t)rdU32(payload.data());

    // The decode pipeline strips a fixed 14-byte Ethernet header per frame, so
    // anything but DLT_EN10MB decodes to garbage (same constraint as the offline
    // pcap path). Warn loudly rather than silently mis-decode.
    if (m_linkType != DLT_EN10MB)
        seqWarn("RemoteCapture: agent link_type %d is not Ethernet (DLT_EN10MB=%d)"
                "; decode offsets assume a 14-byte Ethernet header and will be wrong",
                m_linkType, DLT_EN10MB);
    return true;
}

uint64_t RemoteCaptureThread::pumpFrames(int fd)
{
    std::vector<uint8_t> payload;
    uint64_t frames = 0;
    while (!m_stop.load())
    {
        uint8_t type = 0;
        if (!readMsg(fd, type, payload))
            return frames;
        if (type != SEQ_TYPE_FRAME)
            continue;   // a type this build predates; already consumed

        if (payload.size() < SEQ_FRAME_PREFIX)
        {
            seqWarn("RemoteCapture: SEQA frame shorter than its header; dropping link");
            return frames;
        }
        const uint64_t tsMicros = rdU64(payload.data());
        const size_t caplen = payload.size() - SEQ_FRAME_PREFIX;
        if (caplen == 0)
            continue;
        enqueue(payload.data() + SEQ_FRAME_PREFIX, (uint32_t)caplen,
                (int64_t)(tsMicros / 1000));
        ++frames;
    }
    return frames;
}

bool RemoteCaptureThread::readFull(int fd, void* buf, size_t n)
{
    uint8_t* p = (uint8_t*)buf;
    size_t got = 0;
    while (got < n)
    {
        if (m_stop.load())
            return false;
        const ssize_t r = ::recv(fd, p + got, n - got, 0);
        if (r > 0)
            got += (size_t)r;
        else if (r == 0)
            return false;                      // peer closed
        else if (errno == EINTR)
            continue;
        else
            return false;                      // error (incl. shutdown by stop())
    }
    return true;
}

void RemoteCaptureThread::backoffSleep(int ms)
{
    // Sleep in short slices so stop() takes effect promptly.
    for (int slept = 0; slept < ms && !m_stop.load(); slept += 50)
    {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 50 * 1000 * 1000;         // 50ms
        nanosleep(&ts, NULL);
    }
}

void RemoteCaptureThread::setFilter(const char* device, const char* hostname,
        bool realtime, uint8_t address_type, uint16_t zone_port, uint16_t client_port)
{
    (void)device;
    (void)realtime;
    // v1: always request the "open UDP" filter and let the daemon narrow the
    // session downstream (the agent is a dumb forwarder). Port args are ignored
    // — a mid-session re-filter would need a reconnect, which we don't do.
    (void)zone_port;
    (void)client_port;

    QString f = QStringLiteral(
        "udp[0:2] > 1024 and udp[2:2] > 1024 and not port 5353 and not net 224.0.0.0/4");

    const bool haveHost = hostname && *hostname &&
                          strcmp(hostname, AUTOMATIC_CLIENT_IP) != 0;
    if (address_type == IP_ADDRESS_TYPE && haveHost)
        f += QString(" and %1 %2")
                 .arg(strchr(hostname, '/') ? "net" : "host")
                 .arg(hostname);
    else if (address_type == MAC_ADDRESS_TYPE && haveHost)
        f += QString(" and ether host %1").arg(hostname);

    // ipv4 unicast only (the 14-byte ether strip assumes this).
    f += QStringLiteral(" and ether proto 0x800 and not broadcast and not multicast");

    m_filter = f;
}

const QString RemoteCaptureThread::getFilter()
{
    return m_filter;
}
