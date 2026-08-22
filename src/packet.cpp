/*
 *  packet.cpp
 *  Copyright 2000-2024 by the respective ShowEQ Developers
 *  Portions Copyright 2001-2004,2007 Zaphod (dohpaz@users.sourceforge.net).
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

/* Implementation of Packet class */
#include <cstdio>
#include <algorithm>
#include <unistd.h>
#include <netdb.h>

#ifdef __FreeBSD__
#include "packet.h"
#endif
#include <netinet/if_ether.h>

#include <QDir>
#include <QFile>
#include <QTimer>
#include <QFileInfo>

#include "everquest.h"
#include "packet.h"
#include "boxregistry.h"
#include "namepromoter.h"
#include "zoneserverobserver.h"
#include "packetcommon.h"
#include "packetcapture.h"
#include "remotecapture.h"
#include "packetformat.h"
#include "packetstream.h"
#include "packetinfo.h"
#include "rustsession.h"
#include "vpacket.h"
#include "everquest.h"
#include "diagnosticmessages.h"

#include <QDateTime>

//----------------------------------------------------------------------
// Macros

//#define DEBUG_PACKET
//#undef DEBUG_PACKET

// The following defines are used to diagnose packet handling behavior
// this define is used to diagnose packet processing (in dispatchPacket mostly)
//#define PACKET_PROCESS_DIAG 3 
// verbosity level 0-n

// this define is used to diagnose cache handling (in dispatchPacket mostly)
//#define PACKET_CACHE_DIAG 3 
// verbosity level (0-n)

// diagnose opcode DB issues
//#define  PACKET_OPCODEDB_DIAG 1

// diagnose structure size changes
#define PACKET_PAYLOAD_SIZE_DIAG 1

// Packet version is a unique number that should be bumped every time packet
// structure (ie. encryption) changes.  It is checked by the VPacket feature
// (currently the date of the last packet structure change)
#define PACKETVERSION  40101

//----------------------------------------------------------------------
// constants

//----------------------------------------------------------------------
// Here begins the code

namespace {

seq::rust::SessionBackend shadowBackend()
{
#if defined(SEQ_TARGET_LIVE)
  return seq::rust::SessionBackend::Live;
#elif defined(SEQ_TARGET_TEST)
  return seq::rust::SessionBackend::Test;
#elif defined(SEQ_TARGET_EQL)
  return seq::rust::SessionBackend::Eql;
#else
#error "unknown SEQ_TARGET"
#endif
}

EQPacketFlowKey packetFlowKey(const EQUDPIPPacketFormat& packet)
{
  const uint64_t source = (uint64_t(packet.getIPv4SourceN()) << 16) |
                          uint64_t(packet.getSourcePort());
  const uint64_t destination = (uint64_t(packet.getIPv4DestN()) << 16) |
                               uint64_t(packet.getDestPort());
  return source < destination ? EQPacketFlowKey{source, destination}
                              : EQPacketFlowKey{destination, source};
}

} // namespace


//----------------------------------------------------------------------
// EQPacket class methods

/* EQPacket Class - Sets up packet capturing */

////////////////////////////////////////////////////
// Constructor
EQPacket::EQPacket(const QString& opcodesToml,
		   uint16_t arqSeqGiveUp,
		   QString device,
		   QString agentTarget,
		   QString ip,
		   QString mac_address,
		   bool realtime,
           int snaplen,
           int buffersize,
		   bool sessionTrackingFlag,
		   bool recordPackets,
		   int playbackPackets,
		   int8_t playbackSpeed, 
		   QObject * parent, const char *name)
  : QObject (parent),
    m_packetCapture(NULL),
    m_vPacket(NULL),
    m_timer(NULL),
    m_busy_decoding(false),
    m_arqSeqGiveUp(arqSeqGiveUp),
    m_device(device),
    m_agent(agentTarget),
    m_ip(ip),
    m_mac(mac_address),
    m_realtime(realtime),
    m_snaplen(snaplen),
    m_buffersize(buffersize),
    m_session_tracking(sessionTrackingFlag),
    m_recordPackets(recordPackets),
    m_playbackPackets(playbackPackets),
    m_playbackSpeed(playbackSpeed)
{
  setObjectName(name);
  // create the packet type db
  m_packetTypeDB = new EQPacketTypeDB();

#ifdef PACKET_OPCODEDB_DIAG
  m_packetTypeDB->list();
#endif

  // create the world opcode db
  m_worldOPCodeDB = new EQPacketOPCodeDB(QStringLiteral("world"));

  // load the world opcode db
  if (!m_worldOPCodeDB->load(*m_packetTypeDB, opcodesToml))
    seqFatal("Error loading '%s' [[world]]!", opcodesToml.toLatin1().data());

  // de-piggyback guard: flag any mapped SZC_Match opcode still gating on a Live
  // sizeof instead of a backend-owned size override (no-op on live/test).
  m_undeclaredGateSizes += m_worldOPCodeDB->warnUndeclaredBackendGateSizes(*m_packetTypeDB);

#ifdef PACKET_OPCODEDB_DIAG
  m_worldOPCodeDB->list();
#endif


  // create the zone opcode db
  m_zoneOPCodeDB = new EQPacketOPCodeDB(QStringLiteral("zone"));

  // load the zone opcode db
  if (!m_zoneOPCodeDB->load(*m_packetTypeDB, opcodesToml))
    seqFatal("Error loading '%s' [[zone]]!", opcodesToml.toLatin1().data());

  m_undeclaredGateSizes += m_zoneOPCodeDB->warnUndeclaredBackendGateSizes(*m_packetTypeDB);

  try {
    // The pinned decoder carries the canonical catalogs. One registry is
    // shared by every per-Box Rust session created below.
    m_shadowRegistry = std::make_unique<seq::shadow::ProtocolRegistry>();
    seqInfo("Rust shadow protocol catalog: %s",
            m_shadowRegistry->contentHash(shadowBackend()).c_str());
  } catch (const std::exception& error) {
    // Shadow mode must never take down the legacy mutation path.
    seqWarn("Rust shadow disabled: protocol registry failed: %s", error.what());
  }

#ifdef PACKET_OPCODEDB_DIAG
  m_zoneOPCodeDB->list();
#endif

  
  // Setup the data streams

  // Setup client -> world stream
  m_client2WorldStream = new EQPacketStream(client2world, DIR_Client, 
					    m_arqSeqGiveUp, *m_worldOPCodeDB,
					    this, "client2world");
  connectStream(m_client2WorldStream);

  // Setup world -> client stream
  m_world2ClientStream = new EQPacketStream(world2client, DIR_Server,
					    m_arqSeqGiveUp, *m_worldOPCodeDB,
					    this, "world2client");
  connectStream(m_world2ClientStream);

  // Setup client -> zone stream
  m_client2ZoneStream = new EQPacketStream(client2zone, DIR_Client,
					  m_arqSeqGiveUp, *m_zoneOPCodeDB,
					  this, "client2zone");
  connectStream(m_client2ZoneStream);

  // Setup zone -> client stream
  m_zone2ClientStream = new EQPacketStream(zone2client, DIR_Server,
					   m_arqSeqGiveUp, *m_zoneOPCodeDB,
					   this, "zone2client");
  connectStream(m_zone2ClientStream);

  // Initialize convenient streams array
  m_streams[client2world] = m_client2WorldStream;
  m_streams[world2client] = m_world2ClientStream;
  m_streams[client2zone] = m_client2ZoneStream;
  m_streams[zone2client] = m_zone2ClientStream;

  // A capture can begin mid-zone, before world traffic creates a Box. Hooks
  // use the normalized UDP flow carried by each protocol packet so unrelated
  // unattributed sessions never share Rust correlation state.
  if (m_shadowRegistry) {
    for (EQPacketStream* stream : {m_client2WorldStream, m_world2ClientStream,
                                   m_client2ZoneStream, m_zone2ClientStream})
      installShadowHook(stream);
  }

  // Stage 2 of multibox-sessions (docs/MULTIBOX_PLAN.md): every new
  // Box gets a NamePromoter so OP_EnterWorld (world C>S, char name @
  // offset 0) fills in display_name + a stable hashed box_id. The
  // primary box re-uses the global world streams (its wiring is
  // intact); non-primary boxes get their own world stream pair so
  // their session-key handshake and decode don't collide with the
  // primary's. Zone streams stay global — same-host zone demux is
  // deferred to a later stage.
  m_boxes.setBoxCreatedHook([this](Box& box) {
    EQPacketStream* c2s = nullptr;
    if (box.is_primary) {
      // Primary box aliases the global stream pointers so the
      // per-Box wiring path (Stage 3b of MULTIBOX_PLAN.md) treats it
      // uniformly with non-primary boxes — wireBoxOpcodes just walks
      // box->{world,zone}_{c2s,s2c} regardless.
      box.world_c2s = m_client2WorldStream;
      box.world_s2c = m_world2ClientStream;
      box.zone_c2s  = m_client2ZoneStream;
      box.zone_s2c  = m_zone2ClientStream;
      c2s = m_client2WorldStream;
    } else {
      // All of this non-primary box's owned QObjects hang off one root so
      // BoxRegistry::evictStale can reclaim them with a single deleteLater
      // (see onBoxAboutToBeRemoved). The root is itself parented to
      // EQPacket so a box never evicted still gets cleaned up at shutdown.
      QObject* root = new QObject(this);
      m_boxRoots.insert(&box, root);
      box.world_c2s = new EQPacketStream(client2world, DIR_Client,
                                         m_arqSeqGiveUp, *m_worldOPCodeDB,
                                         root, "box-world-c2s");
      box.world_s2c = new EQPacketStream(world2client, DIR_Server,
                                         m_arqSeqGiveUp, *m_worldOPCodeDB,
                                         root, "box-world-s2c");
      box.zone_c2s  = new EQPacketStream(client2zone, DIR_Client,
                                         m_arqSeqGiveUp, *m_zoneOPCodeDB,
                                         root, "box-zone-c2s");
      box.zone_s2c  = new EQPacketStream(zone2client, DIR_Server,
                                         m_arqSeqGiveUp, *m_zoneOPCodeDB,
                                         root, "box-zone-s2c");
      box.world_c2s->setSessionTracking(m_session_tracking);
      box.world_s2c->setSessionTracking(m_session_tracking);
      box.zone_c2s->setSessionTracking(m_session_tracking);
      box.zone_s2c->setSessionTracking(m_session_tracking);
      // Streams are unidirectional: world_s2c parses the SessionResponse
      // (carries the key) and world_c2s parses the SessionRequest, but
      // neither sees the other's bytes. Cross-wire sessionKey so the
      // sibling can decrypt. We deliberately don't connect to EQPacket's
      // dispatchSessionKey — that broadcasts to the global four streams
      // which belong to the primary box, not this box. Same pattern for
      // zone streams.
      connect(box.world_s2c, &EQPacketStream::sessionKey,
              box.world_c2s, &EQPacketStream::receiveSessionKey);
      connect(box.world_c2s, &EQPacketStream::sessionKey,
              box.world_s2c, &EQPacketStream::receiveSessionKey);
      connect(box.zone_s2c, &EQPacketStream::sessionKey,
              box.zone_c2s, &EQPacketStream::receiveSessionKey);
      connect(box.zone_c2s, &EQPacketStream::sessionKey,
              box.zone_s2c, &EQPacketStream::receiveSessionKey);
      c2s = box.world_c2s;
    }
    // ZoneServerInfo S>C on world tells the daemon which port the box is
    // about to connect to — used to bind incoming zone-stream traffic to
    // this box. EVERY box needs one, including the primary: its zone
    // session must be identifiable so dispatchPacket can route bound
    // zone traffic to it (the global streams) and DROP unbound traffic
    // rather than letting foreign sessions interleave on those streams.
    // Parent the observers under the box root (non-primary) so they're
    // torn down with the box on eviction; the primary's hang off EQPacket.
    QObject* observerParent = m_boxRoots.value(&box, this);
    new ZoneServerObserver(&box, box.world_s2c,
                           [this] { return nowMs(); }, observerParent);
    new NamePromoter(&box, &m_boxes, c2s, observerParent);

    if (m_shadowRegistry) {
      try {
        auto session = std::make_unique<seq::shadow::Session>(
            *m_shadowRegistry, shadowBackend(), 256, 4 * 1024 * 1024,
            seq::shadow::LifecycleSelector::Shadow);
        m_shadowSessions.emplace(&box, std::move(session));
        for (EQPacketStream* stream : {box.world_c2s, box.world_s2c,
                                       box.zone_c2s, box.zone_s2c})
          installShadowHook(stream);
      } catch (const std::exception& error) {
        m_shadowDisabled.insert(&box);
        seqWarn("Rust shadow session creation failed for box %s: %s",
                qUtf8Printable(box.box_id), error.what());
      }
    }

    // Per-box opcode wiring is owned by DaemonApp::onBoxCreated (it owns
    // the per-box ManagerSets and wires each box's streams to its own
    // managers via wireBoxPipeline). Every box decodes continuously into
    // its own managers — there is no mute gate — so the active box can be
    // switched by rebinding SessionAdapter rather than clearing state.
  });

  // Reclaim a box's streams + observers when BoxRegistry::evictStale
  // retires its idle session (DaemonApp drives the periodic sweep and
  // tears down the matching ManagerSet on the same signal).
  connect(&m_boxes, &BoxRegistry::boxAboutToBeRemoved,
          this, &EQPacket::onBoxAboutToBeRemoved);

  // no client/server ports yet
  m_clientPort = 0;
  m_serverPort = 0;

  if (m_ip.isEmpty() && m_mac.isEmpty())
  {
    seqInfo("No address specified. Defaulting to client auto-detect");
    m_ip = AUTOMATIC_CLIENT_IP;
  }

  validateIP();

  if (m_playbackPackets == PLAYBACK_OFF)
  {
    // Remote scry-agent source (--agent) or a local libpcap device. Both are
    // real-time frame sources drained by the same processPackets() loop; the
    // remote one ignores the device name and dials the agent instead.
    if (!m_agent.isEmpty())
      m_packetCapture = new RemoteCaptureThread(m_agent);
    else
      m_packetCapture = new PacketCaptureThread(m_snaplen, m_buffersize);
    if (m_mac.length() == 17)
    {
      seqInfo("Listening for client MAC: %s", m_mac.toLatin1().data());

      m_packetCapture->start(m_device.toLatin1().data(),
              m_mac.toLatin1().data(),
              m_realtime, MAC_ADDRESS_TYPE );
    }
    else
    {
      if (m_detectingClient)
        seqInfo("Listening for next client seen. (you must zone for this to work!)");
      else
        seqInfo("Listening for client: %s", m_ip.toLatin1().data());

      m_packetCapture->start(m_device.toLatin1().data(),
              m_ip.toLatin1().data(),
              m_realtime, IP_ADDRESS_TYPE );
    }
    emit filterChanged();
  }
  else if (m_playbackPackets == PLAYBACK_FORMAT_TCPDUMP)
  {
    // Create the pcap object and initialize with the file input given
    m_packetCapture = new PacketCaptureThread(m_snaplen, m_buffersize);

    QString filename = pSEQPrefs->getPrefString("Filename", "VPacket");

    m_packetCapture->startOffline(filename.toLatin1().data(), m_playbackSpeed);
    seqInfo("Playing back packets from '%s' at speed '%d'",
      filename.toLatin1().data(), m_playbackSpeed);
  }

  // Flag session tracking properly on streams
  session_tracking(sessionTrackingFlag);

  // if running setuid root, then give up root access, since the PacketCapture
  // is the only thing that needed it.
  if ((geteuid() == 0) && (getuid() != geteuid()))
  {
    if (setuid(getuid()) != 0)
      seqFatal("setuid(getuid()) failed; refusing to retain root privileges");
  }

  /* Create timer object */
  m_timer = new QTimer (this);
  
  if (m_playbackPackets == PLAYBACK_OFF || 
          m_playbackPackets == PLAYBACK_FORMAT_TCPDUMP)
  {
    // Normal pcap packet handler
    connect (m_timer, SIGNAL (timeout ()), this, SLOT (processPackets ()));
  }
  else
  {
    // Special internal playback handler
    connect (m_timer, SIGNAL (timeout ()), this, SLOT (processPlaybackPackets ()));
  }
  
  /* setup VPacket */
  m_vPacket = NULL;
  
  QString section = "VPacket";
  // First param to VPacket is the filename
  // Second param is playback speed:  0 = fast as poss, 1 = 1X, 2 = 2X etc
  if (pSEQPrefs->isPreference("Filename", section))
  {
    QString filename = pSEQPrefs->getPrefString("Filename", section);

    if (m_recordPackets)
    {
      m_vPacket = new VPacket(filename.toLatin1().data(), 1, true);
      // Must appear befire next call to getPrefString, which uses a static string
      seqInfo("Recording packets to '%s' for future playback", filename.toLatin1().data());

      if (!pSEQPrefs->getPrefString("FlushPackets", section).isNull())
          m_vPacket->setFlushPacket(true);
    }
    else if (m_playbackPackets == PLAYBACK_FORMAT_SEQ)
    {
      m_vPacket = new VPacket(filename.toLatin1().data(), 1, false);
      m_vPacket->setCompressTime(pSEQPrefs->getPrefInt("CompressTime", section, 0));
      m_vPacket->setPlaybackSpeed(m_playbackSpeed);

      seqInfo("Playing back packets from '%s' at speed '%d'", filename.toLatin1().data(),

              m_playbackSpeed);
    }
  }
  else
  {
    m_recordPackets = 0;
    m_playbackPackets = PLAYBACK_OFF;
  }
}

//helper function to verify specified IP is a valid IP or hostname, and/or
//to set up auto detection.
void EQPacket::validateIP()
{
  struct in_addr  ia;
  struct hostent *he;

  if (m_ip.isEmpty() || m_ip == AUTOMATIC_CLIENT_IP)
  {
    /* Substitute "special" IP which is interpreted
       to set up a different filter for picking up new sessions */
    inet_aton (AUTOMATIC_CLIENT_IP, &ia);
  }
  else if (inet_aton (m_ip.toLatin1().data(), &ia) == 0)
  {
    he = gethostbyname(m_ip.toLatin1().data());
    if (he)
    {
        memcpy (&ia, he->h_addr_list[0], he->h_length);
    }
    else
    {
        // If the IP or host is invalid, default to auto-detect, rather
        // than immediately exiting (the previous behavior)
        seqWarn("Invalid address or hostname: %s", m_ip.toLatin1().data());
        seqWarn("Defaulting to client auto-detect");
        m_ip = AUTOMATIC_CLIENT_IP;
        inet_aton (AUTOMATIC_CLIENT_IP, &ia);
    }
  }
  m_client_addr = ia.s_addr;
  m_ip = inet_ntoa(ia);

  m_detectingClient = (m_ip == AUTOMATIC_CLIENT_IP);

}

////////////////////////////////////////////////////
// Destructor
EQPacket::~EQPacket()
{

  if (m_packetCapture != NULL)
  {
    // stop any packet capture 
    m_packetCapture->stop();

    // delete the object
    delete m_packetCapture;
  }

  flushAllShadowSessions(seq::shadow::FlushReason::Shutdown);

  // try to close down VPacket cleanly
  if (m_vPacket != NULL)
  {
    // make sure any data is flushed to the file
    m_vPacket->Flush();

    // delete VPacket
    delete m_vPacket;
  }

  if (m_timer != NULL)
  {
    // make sure the timer is stopped
    m_timer->stop();

    // delete the timer
    delete m_timer;
  }

  resetEQPacket();

  delete m_client2WorldStream;
  delete m_world2ClientStream;
  delete m_client2ZoneStream;
  delete m_zone2ClientStream;

  if (m_packetTypeDB)
  {
    delete m_packetTypeDB;
  }
  if (m_zoneOPCodeDB)
  {
    delete m_zoneOPCodeDB;
  }
  if (m_worldOPCodeDB)
  {
    delete m_worldOPCodeDB;
  }
}

/* Start the timer to process packets */
void EQPacket::start (int delay)
{
#ifdef DEBUG_PACKET
   qDebug ("start()");
#endif /* DEBUG_PACKET */
   m_timer->start (delay);
}

/* Stop the timer to process packets */
void EQPacket::stop (void)
{
#ifdef DEBUG_PACKET
   qDebug ("stop()");
#endif /* DEBUG_PACKET */
   m_timer->stop ();
}

/* Reads packets and processes waiting packets */
void EQPacket::processPackets (void)
{
  /* Make sure we are not called while already busy */
  if (m_busy_decoding)
     return;

  /* Set flag that we are busy decoding */
  m_busy_decoding = true;
  
  unsigned char buffer[BUFSIZ]; 
  short size;
  
  /* fetch them from pcap */
  while ((size = m_packetCapture->getPacket(buffer)))
  {
    /* Now.. we know the rest is an IP udp packet concerning the
     * host in question, because pcap takes care of that.
     */

    // Offline (--replay-pcap) playback: stamp dispatch with the packet's
    // ORIGINAL capture time so EventLogger / --list-events reconstruct the
    // real capture timeline instead of replay wall-clock. Live capture leaves
    // m_currentPacketTimeMs at 0 (EventLogger then uses wall-clock, unchanged).
    if (m_playbackPackets == PLAYBACK_FORMAT_TCPDUMP)
      m_currentPacketTimeMs = m_packetCapture->lastCaptureMs();

    /* Now we assume its an everquest packet */
    if (m_recordPackets)
    {
      time_t now = time(NULL);
      m_vPacket->Record((const char *) buffer, size, now, PACKETVERSION);
    }
      
    dispatchPacket (size - sizeof (struct ether_header),
		  (unsigned char *) buffer + sizeof (struct ether_header) );
  }

  /* Clear decoding flag */
  m_busy_decoding = false;

  // Offline (--replay-pcap) playback: once the reader thread hit EOF and the
  // queue is fully drained, playback is complete. Mirror processPlaybackPackets
  // (the .vpk path) so a pcap replay quits at EOF — driving --record-golden and
  // flushing --opcode-stats exactly like --replay does. Guarded on the tcpdump
  // format so live capture (PLAYBACK_OFF) never trips it.
  if (m_playbackPackets == PLAYBACK_FORMAT_TCPDUMP &&
      m_packetCapture->offlinePlaybackComplete())
  {
    seqInfo("End of pcap playback reached. Playback Finished!");
    stop();
    flushAllShadowSessions(seq::shadow::FlushReason::ReplayEnd);
    emit playbackFinished();
  }
}

////////////////////////////////////////////////////
// Reads packets and processes waiting packets from playback file
void EQPacket::processPlaybackPackets (void)
{
#ifdef DEBUG_PACKET
//   qDebug ("processPackets()");
#endif /* DEBUG_PACKET */
  /* Make sure we are not called while already busy */
  if (m_busy_decoding)
    return;

  /* Set flag that we are busy decoding */
  m_busy_decoding = true;

  unsigned char  buffer[8192];
  int            size;

  /* in packet playback mode fetch packets from VPacket class */
  time_t now;
  int timein = mTime();

  long version = PACKETVERSION;
  // Set when Playback() reports nothing left, i.e. the loop ended by draining
  // rather than by exhausting its wallclock budget.
  bool drained = false;

  // decode packets from the playback buffer
  do
  {
    size = m_vPacket->Playback((char *) buffer, sizeof(buffer), &now, &version);
    
    if (size)
    {
      if (PACKETVERSION == version)
      {
	// Stamp the dispatch with the packet's *recorded* time so EventLogger
	// (and any other consumer) can regenerate a timeline from a .vpk that
	// matches the original capture, instead of replay wall-clock.
	m_currentPacketTimeMs = (qint64) now * 1000;
	dispatchPacket ( size - sizeof (struct ether_header),
		       (unsigned char *) buffer + sizeof (struct ether_header)
		       );
      }
      else
      {
	seqWarn("Error:  The version of the packet stream has " \
		 "changed since '%s' was recorded - disabling playback",
		 m_vPacket->getFileName());

	// stop the timer, nothing more can be done...
	stop();

	break;
      }
    }
    else
    {
      // Playback returned nothing: the source is genuinely drained for now.
      drained = true;
      break;
    }
  } while ( (mTime() - timein) < 100);

  // Only conclude playback when the loop actually drained the source. The
  // `while` above is a WALLCLOCK budget, so it can also exit with packets still
  // pending — and endOfData() may already be true at that point (the reader has
  // consumed the file while dispatch lags behind). Treating that as finished
  // quits mid-tail, losing however many packets the budget cut off, which
  // varies run to run. Recording a 300MB capture lost ~0.1% of its envelopes
  // this way, intermittently, producing a golden that was a strict prefix of a
  // good one.
  if (drained && m_vPacket->endOfData())
  {
    seqInfo("End of playback file '%s' reached."
	    "Playback Finished!",
	    m_vPacket->getFileName());

    // stop the timer, nothing more can be done...
    stop();
    flushAllShadowSessions(seq::shadow::FlushReason::ReplayEnd);
    emit playbackFinished();
  }

  /* Clear decoding flag */
  m_busy_decoding = false;
}

qint64 EQPacket::nowMs(void) const
{
  // During --replay m_currentPacketTimeMs is the packet's recorded time, so
  // BoxRegistry identity/routing timestamps are reproducible (deterministic
  // goldens); it's 0 in live capture, where wall-clock is correct.
  return m_currentPacketTimeMs ? m_currentPacketTimeMs
                               : QDateTime::currentMSecsSinceEpoch();
}

void EQPacket::installShadowHook(EQPacketStream* stream)
{
  if (!stream) return;
  stream->setApplicationPacketHook(
      [this](EQStreamID streamId, uint8_t direction, uint16_t opcode,
             const uint8_t* payload, size_t payloadSize, int64_t timestamp,
             EQPacketFlowKey flowKey, uintptr_t attributionToken) {
        decodeShadowApplication(streamId, direction, opcode, payload,
                                payloadSize, timestamp, flowKey,
                                attributionToken);
      },
      [this] { return int64_t(nowMs()); });
}

seq::shadow::Session* EQPacket::temporaryShadowSession(EQPacketFlowKey flowKey)
{
  if (!m_shadowRegistry || !flowKey.isValid()) return nullptr;
  auto found = m_temporaryShadowSessions.find(flowKey);
  if (found != m_temporaryShadowSessions.end()) {
    found->second.lastUsed = ++m_temporaryShadowClock;
    return found->second.disabled ? nullptr : found->second.session.get();
  }

  if (m_temporaryShadowSessions.size() >= kMaxTemporaryShadowSessions)
    evictOldestTemporaryShadowSession();

  try {
    TemporaryShadowSession temporary;
    temporary.session = std::make_unique<seq::shadow::Session>(
        *m_shadowRegistry, shadowBackend(), 256, 4 * 1024 * 1024,
        seq::shadow::LifecycleSelector::Shadow);
    temporary.lastUsed = ++m_temporaryShadowClock;
    auto [it, inserted] =
        m_temporaryShadowSessions.emplace(flowKey, std::move(temporary));
    return inserted ? it->second.session.get() : nullptr;
  } catch (const std::exception& error) {
    seqWarn("Temporary Rust shadow session creation failed: %s", error.what());
    return nullptr;
  }
}

void EQPacket::evictOldestTemporaryShadowSession()
{
  if (m_temporaryShadowSessions.empty()) return;
  auto oldest = std::min_element(
      m_temporaryShadowSessions.begin(), m_temporaryShadowSessions.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.second.lastUsed < rhs.second.lastUsed;
      });
  if (!oldest->second.disabled && oldest->second.session) {
    try {
      oldest->second.session->flush(seq::shadow::FlushReason::Shutdown);
    } catch (const std::exception& error) {
      seqWarn("Temporary Rust shadow eviction flush failed: %s", error.what());
    }
  }
  m_temporaryShadowSessions.erase(oldest);
}

void EQPacket::decodeShadowApplication(
    EQStreamID stream, uint8_t direction, uint16_t opcode,
    const uint8_t* payload, size_t payloadSize, int64_t timestamp,
    EQPacketFlowKey flowKey, uintptr_t attributionToken)
{
  const bool world = stream == client2world || stream == world2client;
  Box* box = reinterpret_cast<Box*>(attributionToken);
  seq::shadow::Session* session = nullptr;

  if (box) {
    auto attributed = m_shadowSessions.find(box);
    if (attributed == m_shadowSessions.end() ||
        m_shadowDisabled.count(box) != 0)
      return;

    // A mid-zone flow can precede the world handshake that creates its Box.
    // Adopt that state only while the Box session is still cold. Once it has
    // decoded an application packet, merging two state machines would require
    // replaying one side, so finalize the temporary state instead.
    auto temporary = m_temporaryShadowSessions.find(flowKey);
    if (temporary != m_temporaryShadowSessions.end()) {
      if (!temporary->second.disabled && temporary->second.session &&
          attributed->second->recordCount() == 0) {
        attributed->second = std::move(temporary->second.session);
        seqInfo("Rust shadow adopted pre-attribution flow for box %s",
                qUtf8Printable(box->box_id));
      } else if (!temporary->second.disabled && temporary->second.session) {
        try {
          temporary->second.session->flush(seq::shadow::FlushReason::Reset);
        } catch (const std::exception& error) {
          seqWarn("Temporary Rust shadow reset failed during attribution: %s",
                  error.what());
        }
        seqWarn("Rust shadow could not merge pre-attribution flow into warm box %s; "
                "temporary correlation was finalized",
                qUtf8Printable(box->box_id));
      }
      m_temporaryShadowSessions.erase(temporary);
      attributed = m_shadowSessions.find(box);
    }
    session = attributed->second.get();
  } else {
    session = temporaryShadowSession(flowKey);
  }

  if (!session) return;
  try {
    session->decode(
        world ? seq::shadow::Stream::World : seq::shadow::Stream::Zone,
        opcode,
        direction == DIR_Server
            ? seq::shadow::Direction::ServerToClient
            : seq::shadow::Direction::ClientToServer,
        payload, payloadSize, timestamp);
  } catch (const std::exception& error) {
    if (box) {
      m_shadowDisabled.insert(box);
      seqWarn("Rust shadow disabled for box %s after decode error: %s",
              qUtf8Printable(box->box_id), error.what());
    } else {
      auto temporary = m_temporaryShadowSessions.find(flowKey);
      if (temporary != m_temporaryShadowSessions.end())
        temporary->second.disabled = true;
      seqWarn("Temporary Rust shadow disabled after decode error: %s",
              error.what());
    }
  }
}

const seq::shadow::Session* EQPacket::shadowSession(const Box* box) const
{
  const auto it = m_shadowSessions.find(box);
  return it == m_shadowSessions.end() ? nullptr : it->second.get();
}

void EQPacket::flushShadowSession(const Box* box,
                                  seq::shadow::FlushReason reason)
{
  if (!box || m_shadowDisabled.count(box) != 0) return;
  const auto it = m_shadowSessions.find(box);
  if (it == m_shadowSessions.end()) return;
  try {
    it->second->flush(reason);
  } catch (const std::exception& error) {
    m_shadowDisabled.insert(box);
    seqWarn("Rust shadow disabled for box %s after flush error: %s",
            qUtf8Printable(box->box_id), error.what());
  }
}

void EQPacket::flushAllShadowSessions(seq::shadow::FlushReason reason)
{
  for (auto& entry : m_temporaryShadowSessions) {
    if (entry.second.disabled || !entry.second.session) continue;
    try {
      entry.second.session->flush(reason);
    } catch (const std::exception& error) {
      entry.second.disabled = true;
      seqWarn("Temporary Rust shadow disabled after flush error: %s",
              error.what());
    }
  }
  for (const auto& entry : m_shadowSessions)
    flushShadowSession(entry.first, reason);
}

/////////////////////////////////////////////////////////
// Connect the given stream's signals to the proper slots
void EQPacket::disconnectReconTaps()
{
  // Undo the decodedPacket -> decoded{Zone,World}Packet signal relays that
  // connectStream() installed for the four global streams (both arities).
  // rawPacket relays stay — pcap/raw logging is not session-filtered.
  for (EQPacketStream* s : {m_client2ZoneStream, m_zone2ClientStream}) {
    disconnect(s,
      SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)),
      this,
      SIGNAL(decodedZonePacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)));
    disconnect(s,
      SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)),
      this,
      SIGNAL(decodedZonePacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)));
  }
  for (EQPacketStream* s : {m_client2WorldStream, m_world2ClientStream}) {
    disconnect(s,
      SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)),
      this,
      SIGNAL(decodedWorldPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)));
    disconnect(s,
      SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)),
      this,
      SIGNAL(decodedWorldPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)));
  }
}

void EQPacket::connectStream(EQPacketStream* stream)
{
  // Packet logging
  switch (stream->streamID())
  {
    case zone2client:
    case client2zone:
    {
      // Zone server stream
      connect(stream,
        SIGNAL(rawPacket(const uint8_t*, size_t, uint8_t, uint16_t)),
        this,
        SIGNAL(rawZonePacket(const uint8_t*, size_t, uint8_t, uint16_t)));

      connect(stream,
        SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)),
        this,
        SIGNAL(decodedZonePacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)));

      connect(stream,
        SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)),
        this,
        SIGNAL(decodedZonePacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)));
    }
    break;
    case world2client:
    case client2world:
    {
      // World server stream
      connect(stream,
        SIGNAL(rawPacket(const uint8_t*, size_t, uint8_t, uint16_t)),
        this,
        SIGNAL(rawWorldPacket(const uint8_t*, size_t, uint8_t, uint16_t)));

      connect(stream,
        SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)),
        this,
        SIGNAL(decodedWorldPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*)));

      connect(stream,
        SIGNAL(decodedPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)),
        this,
        SIGNAL(decodedWorldPacket(const uint8_t*, size_t, uint8_t, uint16_t, const EQPacketOPCode*, bool)));
    }
    break;
    default :
    {
      return;
    }
  }

  // Debugging
  connect(stream,
      SIGNAL(cacheSize(int, int)),
      this,
      SIGNAL(cacheSize(int, int)));
  connect(stream,
      SIGNAL(seqReceive(int, int)),
      this,
      SIGNAL(seqReceive(int, int)));
  connect(stream,
      SIGNAL(seqExpect(int, int)),
      this,
      SIGNAL(seqExpect(int, int)));
  connect(stream,
      SIGNAL(numPacket(int, int)),
      this,
      SIGNAL(numPacket(int, int)));

  // Session handling
  connect(stream,
      SIGNAL(sessionTrackingChanged(uint8_t)),
      this,
      SIGNAL(sessionTrackingChanged(uint8_t)));
  connect(stream,
      SIGNAL(lockOnClient(in_port_t, in_port_t, in_addr_t)),
      this,
      SLOT(lockOnClient(in_port_t, in_port_t, in_addr_t)));
  connect(stream,
      SIGNAL(closing(uint32_t, EQStreamID)),
      this,
      SLOT(closeStream(uint32_t, EQStreamID)));
  connect(stream,
      SIGNAL(sessionKey(uint32_t, EQStreamID, uint32_t)),
      this,
      SLOT(dispatchSessionKey(uint32_t, EQStreamID, uint32_t)));
  connect(stream,
      SIGNAL(maxLength(int, int)),
      this,
      SIGNAL(maxLength(int, int)));
}

////////////////////////////////////////////////////
// This function decides the fate of the Everquest packet 
// and dispatches it to the correct packet stream for handling function
void EQPacket::dispatchPacket(int size, unsigned char *buffer)
{
#ifdef DEBUG_PACKET
  qDebug ("EQPacket::dispatchPacket()");
#endif /* DEBUG_PACKET */

  // Create an object to parse the packet
  EQUDPIPPacketFormat packet(buffer, size, false);

  dispatchPacket(packet);

  // signal a new packet. This has to be at the end so that the session is
  // filled in if possible, so that it can report on crc errors properly
  emit newPacket(packet);
}

void EQPacket::dispatchPacket(EQUDPIPPacketFormat& packet)
{
  packet.setCaptureTimeMs(nowMs());
  packet.setFlowKey(packetFlowKey(packet));
  packet.setAttributionToken(0);

  // Detect client by world server port traffic...
  const in_port_t srcPortHost = packet.getSourcePort();
  const in_port_t dstPortHost = packet.getDestPort();
  const bool srcIsWorld =
      (srcPortHost >= WorldServerGeneralMinPort &&
       srcPortHost <= WorldServerGeneralMaxPort);
  const bool dstIsWorld =
      (dstPortHost >= WorldServerGeneralMinPort &&
       dstPortHost <= WorldServerGeneralMaxPort);

  // Stage 1 of multibox-sessions (docs/MULTIBOX_PLAN.md): observe
  // every distinct EQ client on the wire. Runs alongside the legacy
  // single-shot m_detectingClient lock — primary box (first seen)
  // still feeds the existing decode pipeline; the registry is
  // observational until later stages split per-box decode.
  if (srcIsWorld || dstIsWorld) {
    const in_addr_t client_ip   = srcIsWorld ? packet.getIPv4DestN()
                                             : packet.getIPv4SourceN();
    const in_port_t client_port = htons(srcIsWorld ? dstPortHost
                                                   : srcPortHost);
    const in_port_t server_port = htons(srcIsWorld ? srcPortHost
                                                   : dstPortHost);
    const in_addr_t server_ip = srcIsWorld ? packet.getIPv4SourceN()
                                          : packet.getIPv4DestN();
    m_boxes.observe(client_ip, server_ip, client_port, server_port, nowMs());
  }

  if (m_detectingClient && srcIsWorld)
  {
    m_ip = packet.getIPv4DestA();
    m_client_addr = packet.getIPv4DestN();
    m_detectingClient = false;
    emit clientChanged(m_client_addr);
    seqInfo("Client Detected: %s", m_ip.toLatin1().data());
  }
  else if (m_detectingClient && dstIsWorld)
  {
    m_ip = packet.getIPv4SourceA();
    m_client_addr = packet.getIPv4SourceN();
    m_detectingClient = false;
    emit clientChanged(m_client_addr);
    seqInfo("Client Detected: %s", m_ip.toLatin1().data());
  }

  // Dispatch based on known streams
  if ((packet.getDestPort() == ChatServerPort) ||
      (packet.getSourcePort() == ChatServerPort))
  {
    // Drop chat server traffic
    return;
  }
  else if ((packet.getDestPort() == WorldServerChatPort) ||
      (packet.getSourcePort() == WorldServerChatPort))
  {
    // Drop cross-server chat traffic
    return;
  }
  else if ((packet.getDestPort() == WorldServerChat2Port) ||
      (packet.getSourcePort() == WorldServerChat2Port))
  {
    // Drop email and cross-game chat traffic
    return;
  }
  else if ((packet.getDestPort() == WorldServerUCSPort) ||
      (packet.getSourcePort() == WorldServerUCSPort))
  {
    // EQ Legends cross-zone/global chat rides a separate XOR-obfuscated SOE
    // (UCS) session with no cleartext SOE framing — the zone streams would
    // choke on it. Decode + emit it ourselves instead of dropping.
    decodeUCSPacket(packet);
    return;
  }
  else if (((packet.getDestPort() >= LoginServerMinPort) &&
      (packet.getDestPort() <= LoginServerMaxPort)) ||
      ((packet.getSourcePort() >= LoginServerMinPort) &&
      (packet.getSourcePort() <= LoginServerMaxPort)))
  {
    // Drop login server traffic
    return;
  }
  else if ((packet.getDestPort() >= WorldServerGeneralMinPort &&
            packet.getDestPort() <= WorldServerGeneralMaxPort) ||
           (packet.getSourcePort() >= WorldServerGeneralMinPort &&
            packet.getSourcePort() <= WorldServerGeneralMaxPort))
  {
    // World server traffic. Route to the global streams as today so
    // ZoneServerMgr (the only world opcode handler wired via on()) keeps
    // firing on every world handshake regardless of which Box it
    // belongs to — single-client multi-zone fixtures rely on this.
    // ADDITIONALLY mirror non-primary boxes' traffic into their
    // per-box streams so each box's NamePromoter (Stage 2 of
    // docs/MULTIBOX_PLAN.md) can decrypt and read OP_EnterWorld with
    // its own session key. Primary's NamePromoter runs on the global
    // stream; first OP_EnterWorld wins and subsequent ones are
    // ignored (the early-out in NamePromoter::onDecodedPacket).
    // Route world traffic to the OWNING box's world streams by the full
    // 5-tuple. Each box's world session has a unique ephemeral client port
    // even same-host, so this never mixes sessions. This matters because
    // the primary box's world streams ALIAS the global ones: if we instead
    // dumped every box's world traffic onto the globals (by client_ip, the
    // same for all same-host boxes), the primary's ZoneServerObserver and
    // NamePromoter would see — and steal — other boxes' handshakes. observe()
    // created the box for this 5-tuple at the top of dispatchPacket, so a
    // miss here is unexpected (and simply drops, harming nothing).
    const bool srcIsServer = srcIsWorld;
    const in_addr_t client_ip = srcIsServer ? packet.getIPv4DestN()
                                            : packet.getIPv4SourceN();
    const in_port_t client_port = htons(srcIsServer ? dstPortHost
                                                    : srcPortHost);
    const in_port_t server_port = htons(srcIsServer ? srcPortHost
                                                    : dstPortHost);
    Box* box = m_boxes.lookupByWorld(client_ip, client_port, server_port);
    if (box)
    {
      packet.setAttributionToken(reinterpret_cast<uintptr_t>(box));
      EQPacketStream* s = srcIsServer ? box->world_s2c : box->world_c2s;
      if (s) s->handlePacket(packet);
    }
  }
  else
  {
    // Anything else we assume is zone server traffic. Per-box demux
    // (Stage 3a of docs/MULTIBOX_PLAN.md): bind the 5-tuple to a Box
    // on first sighting, then route to that box's per-box zone streams.
    // Primary box (and any unbound traffic) falls through to the global
    // streams as today so the legacy decode pipeline keeps working.
    //
    // Direction: zone packets don't have a port-range identifier; we
    // look up the box by client_ip first (registered when its world
    // handshake was observed). If the source IP matches a known
    // non-primary box's client_ip, this is C>S; otherwise S>C.
    const in_addr_t src_ip = packet.getIPv4SourceN();
    const in_addr_t dst_ip = packet.getIPv4DestN();
    const in_port_t src_port = htons(packet.getSourcePort());
    const in_port_t dst_port = htons(packet.getDestPort());

    auto pickBox = [&](Box*& box, bool& srcIsClient) -> bool {
      // Try the already-bound (client_ip, client_port, server_port)
      // first.
      box = m_boxes.lookupBoundZone(src_ip, src_port, dst_port);
      if (box) { srcIsClient = true; return true; }
      box = m_boxes.lookupBoundZone(dst_ip, dst_port, src_port);
      if (box) { srcIsClient = false; return true; }
      // Unbound 5-tuple: try expected-zone-server match. The
      // SessionRequest's destination is the zone server (the side that
      // matches `expected_zone_server_port`), the source is the new
      // ephemeral client port.
      box = m_boxes.lookupByExpectedZone(src_ip, dst_ip, dst_port);
      if (box) {
        box->zone_client_port = src_port;
        box->zone_server_port_bound = dst_port;
        srcIsClient = true;
        return true;
      }
      box = m_boxes.lookupByExpectedZone(dst_ip, src_ip, src_port);
      if (box) {
        box->zone_client_port = dst_port;
        box->zone_server_port_bound = src_port;
        srcIsClient = false;
        return true;
      }
      // Nobody CLAIMED this port. Normally that means OP_ZoneServerInfo never
      // decoded — its id rotates every patch, and while it is unmapped no box
      // ever announces a port, so with >1 box on the wire every zone packet
      // falls through to the drop below and the whole zone stream goes dark.
      // A zone session always opens with a client SessionRequest moments after
      // the world handshake, so bind that to the client's most recently
      // world-active unbound box. Weaker than the announced-port match (two
      // boxes zoning inside the same window can bind to each other's session),
      // which is why it runs ONLY after the expected-zone lookups miss.
      if (packet.getNetOpCode() == OP_SessionRequest &&
          m_boxes.hasClient(src_ip)) {
        box = m_boxes.lookupByRecentWorld(src_ip, dst_ip, nowMs(),
                                          kZoneBindWindowMs);
        if (box) {
          box->zone_client_port = src_port;
          box->zone_server_port_bound = dst_port;
          srcIsClient = true;
          // Before the first announcement there is nothing to match: that
          // session's OP_ZoneServerInfo predates us. Only a miss AFTER one
          // has landed is suspicious.
          if (m_boxes.anyZoneServerAnnounced()) {
            seqWarn("BoxRegistry: bound zone session %s:%u by world recency — "
                    "no box announced port %u, though others have "
                    "(stale mapping, or two boxes zoning at once?)",
                    qUtf8Printable(packet.getIPv4SourceA()),
                    packet.getSourcePort(), packet.getDestPort());
          } else {
            seqInfo("BoxRegistry: bound zone session %s:%u by world recency — "
                    "no OP_ZoneServerInfo seen yet, expected for the first "
                    "zone session of a run (port %u)",
                    qUtf8Printable(packet.getIPv4SourceA()),
                    packet.getSourcePort(), packet.getDestPort());
          }
          return true;
        }
      }
      return false;
    };

    Box* box = nullptr;
    bool srcIsClient = false;
    const bool matched = pickBox(box, srcIsClient);
    if (matched && box && box->zone_c2s && box->zone_s2c) {
      // Mark zone-stream activity. observe() only stamps last_seen on
      // WORLD traffic (login / zone handshakes); a box that's settled into
      // a zone talks exclusively on the zone stream, so without this its
      // last_seen would freeze at zone-in and BoxRegistry::evictStale would
      // reap an actively-playing box. Stamp the box that owns this session.
      box->last_seen_ms = nowMs();
      ++box->packet_count;
      // Route to THIS box's zone streams. The primary box's zone streams
      // alias the global ones, so its bound session still decodes through
      // the global pipeline — but now keyed on the box's session, not on
      // m_client_addr (which is identical for every same-host box).
      EQPacketStream* s = srcIsClient ? box->zone_c2s : box->zone_s2c;
      packet.setAttributionToken(reinterpret_cast<uintptr_t>(box));
      s->handlePacket(packet);
      return;
    }

    // Unmatched zone traffic. With a SINGLE box (or none) there's no other
    // session to confuse it with, so fall back to the global streams as the
    // legacy single-client pipeline always did — this keeps mid-session
    // captures decoding even when their OP_ZoneServerInfo (and thus the
    // zone binding) was never seen.
    //
    // With MULTIPLE same-host boxes we DROP instead: every box shares one
    // client_ip, so an unidentified session routed to the shared global
    // streams would interleave foreign-session packets and corrupt fragment
    // reassembly (the buffer-overflow crash). A box's own session binds via
    // ZoneServerObserver → lookupByExpectedZone on its SessionRequest, after
    // which lookupBoundZone routes it precisely; anything still unmatched is
    // undecodable here and is safe to drop.
    if (m_boxes.size() <= 1) {
      if (packet.getIPv4SourceN() == m_client_addr) {
        m_client2ZoneStream->handlePacket(packet);
      } else {
        m_zone2ClientStream->handlePacket(packet);
      }
      return;
    }

    // Dropping is silent by construction and looks identical to "the daemon
    // decodes nothing" from the outside — the failure mode that hid an
    // unmapped OP_ZoneServerInfo for a whole patch cycle. Warn once per
    // distinct 5-tuple (capped), but only for traffic involving a client we
    // actually track, so the unrelated UDP a broad capture filter sweeps up
    // stays quiet.
    if (!m_boxes.hasClient(src_ip) && !m_boxes.hasClient(dst_ip)) return;
    // ...and only for a peer that could actually be a zone server. An EQ
    // client also opens SOE sessions to auxiliary services on other subnets;
    // those are unbindable by design, not a symptom.
    const in_addr_t peer_ip = m_boxes.hasClient(src_ip) ? dst_ip : src_ip;
    if (!m_boxes.looksLikeZoneServer(peer_ip)) return;

    const quint64 tupleKey = (quint64(src_ip) << 32) ^ (quint64(dst_ip) << 16)
                             ^ (quint64(src_port) << 16) ^ quint64(dst_port);
    if (m_unboundZoneWarned.size() < kMaxUnboundZoneWarnings &&
        !m_unboundZoneWarned.contains(tupleKey)) {
      m_unboundZoneWarned.insert(tupleKey);
      seqWarn("dropping unbound zone traffic %s:%u -> %s:%u (%zu boxes, none "
              "bound or expecting this port). Zone decode will be EMPTY for "
              "this session — check that OP_ZoneServerInfo is mapped.",
              qUtf8Printable(packet.getIPv4SourceA()), packet.getSourcePort(),
              qUtf8Printable(packet.getIPv4DestA()), packet.getDestPort(),
              m_boxes.size());
    }
  }
} /* end dispatchPacket() */

////////////////////////////////////////////////////
// EQ Legends UCS (cross-zone chat) — forward one raw UDP payload from the
// port-9877 session to MessageShell, which runs the keyless XOR + parse in
// Rust (seq::rust::decode_ucs_chat) and resolves channel names. We deliberately
// bypass the stream/CRC/reassembly path: UCS data packets carry no cleartext
// SOE framing. Direction is derived from the client addr; only the inbound
// (server->client) side carries chat and MessageShell gates on it.
void EQPacket::decodeUCSPacket(EQUDPIPPacketFormat& packet)
{
  const uint8_t* raw = packet.getUDPPayload();
  uint32_t rawLen = packet.getUDPPayloadLength();
  if ((raw == NULL) || (rawLen < 12))
    return;

  uint8_t dir = (packet.getIPv4SourceN() == m_client_addr) ?
    DIR_Client : DIR_Server;

  // Identify WHICH client's UCS session this is (the non-server side), so the
  // channel-mask cache is keyed per client — a client's UCS seed is constant
  // across its zone (box) switches, and distinct clients have distinct seeds.
  in_addr_t clientAddr = (dir == DIR_Server) ? packet.getIPv4DestN()
                                             : packet.getIPv4SourceN();

  emit ucsChatData(raw, (size_t)rawLen, dir, clientAddr);
}

////////////////////////////////////////////////////
// Reclaim a non-primary box's owned objects when the registry evicts it.
void EQPacket::onBoxAboutToBeRemoved(Box* box)
{
  if (!box || box->is_primary) return;   // primary aliases the globals

  for (EQPacketStream* stream : {box->world_c2s, box->world_s2c,
                                 box->zone_c2s, box->zone_s2c}) {
    if (stream) stream->setApplicationPacketHook({}, {});
  }
  flushShadowSession(box, seq::shadow::FlushReason::Shutdown);
  m_shadowSessions.erase(box);
  m_shadowDisabled.erase(box);

  // Drop the stream pointers first. The registry frees the Box right after
  // this returns; nulling here means a stray lookup before the deleteLater
  // lands can't hand a dead pointer to dispatchPacket. The streams + the
  // box's ZoneServerObserver/NamePromoter all hang off the root, so one
  // deleteLater unwinds the whole subtree (and its signal connections) on
  // the next event-loop pass — safe because the box is already gone from
  // the registry, so no further packet can route to these streams.
  box->world_c2s = box->world_s2c = box->zone_c2s = box->zone_s2c = nullptr;
  if (QObject* root = m_boxRoots.take(box))
    root->deleteLater();
}

////////////////////////////////////////////////////
// Handle zone2client stream closing
void EQPacket::closeStream(uint32_t sessionId, EQStreamID streamId)
{
  // If this is the zone server session closing, reset the pcap filter to
  // a non-exclusive form. Live capture only — offline pcap replay
  // (PLAYBACK_FORMAT_TCPDUMP) must never mutate the filter mid-file or it
  // drops the remaining packets it hasn't read yet; it reads the whole
  // capture through the single static filter startOffline() installed.
  if ((streamId == zone2client || streamId == client2zone) &&
         m_playbackPackets == PLAYBACK_OFF)
  {
    m_packetCapture->setFilter(m_device.toLatin1().data(), m_ip.toLatin1().data(),
            m_realtime, IP_ADDRESS_TYPE, 0, 0);
    emit filterChanged();
  }

  // Pass the close onto the streams
  m_client2WorldStream->close(sessionId, streamId, m_session_tracking);
  m_world2ClientStream->close(sessionId, streamId, m_session_tracking);
  m_client2ZoneStream->close(sessionId, streamId, m_session_tracking);
  m_zone2ClientStream->close(sessionId, streamId, m_session_tracking);

  // If we just closed the zone server session, unlatch the client port
  if (streamId == zone2client || streamId == client2zone)
  {
    unlatchClientPort();

    seqInfo("EQPacket: SessionDisconnect detected, awaiting next zone session,  pcap filter: EQ Client %s",
            (m_ip == AUTOMATIC_CLIENT_IP) ? "auto-detect" : m_ip.toLatin1().data());
  }
}

// Unlatch a locked-on client port, so the next client is detected correctly
void EQPacket::unlatchClientPort()
{
    m_clientPort = 0;
    m_serverPort = 0;
    emit clientPortLatched(m_clientPort);
}


////////////////////////////////////////////////////
// Locks onto a specific client port (for session tracking)
void EQPacket::lockOnClient(in_port_t serverPort, in_port_t clientPort, in_addr_t clientAddr)
{
  m_serverPort = serverPort;
  m_clientPort = clientPort;
  m_client_addr = clientAddr;

  in_addr ia = inet_makeaddr(ntohl(m_client_addr), ntohl(m_client_addr));

  // Live capture only — see closeStream(): offline replay must not re-narrow
  // the pcap filter to the world client port, which would drop zone traffic
  // on dynamic ports for the rest of the file.
  if (m_playbackPackets == PLAYBACK_OFF)
  {
    if (m_mac.length() == 17)
    {
      m_packetCapture->setFilter(m_device.toLatin1().data(),
              m_mac.toLatin1().data(),
              m_realtime,
              MAC_ADDRESS_TYPE, 0,
              m_clientPort);
      emit filterChanged();
    }
    else
    {
      m_packetCapture->setFilter(m_device.toLatin1().data(),
              m_ip.toLatin1().data(),
              m_realtime,
              IP_ADDRESS_TYPE, 0,
              m_clientPort);
      emit filterChanged();
    }
  }

  // Wanted this message even if we're running on playback...
  if (m_mac.length() == 17)
  {
    seqInfo("EQPacket: SessionRequest detected, pcap filter: EQ Client %s, Client port %d. Server port %d",
      m_mac.toLatin1().data(), m_clientPort, m_serverPort);
  }
  else
  {
    seqInfo("EQPacket: SessionRequest detected, pcap filter: EQ Client %s, Client port %d. Server port %d",
      inet_ntoa(ia), m_clientPort, m_serverPort);
  }

  emit clientPortLatched(m_clientPort);
}

void EQPacket::dispatchSessionKey(uint32_t sessionId, EQStreamID streamid,
  uint32_t sessionKey)
{
  m_client2WorldStream->receiveSessionKey(sessionId, streamid, sessionKey);
  m_world2ClientStream->receiveSessionKey(sessionId, streamid, sessionKey);
  m_client2ZoneStream->receiveSessionKey(sessionId, streamid, sessionKey);
  m_zone2ClientStream->receiveSessionKey(sessionId, streamid, sessionKey);
}

///////////////////////////////////////////
//EQPacket::dispatchWorldChatData  
// note this dispatch gets just the payload
void EQPacket::dispatchWorldChatData (size_t len, uint8_t *data, 
				      uint8_t dir)
{
#ifdef DEBUG_PACKET
  qDebug ("dispatchWorldChatData()");
#endif /* DEBUG_PACKET */
  if (len < 10)
    return;
  
  uint16_t opCode = eqntohuint16(data);

  switch (opCode)
  {
  default:
    seqDebug("%04x - %d (%s)", opCode, len,
	    ((dir == DIR_Server) ? 
	     "WorldChatServer --> Client" : "Client --> WorldChatServer"));
  }
}

///////////////////////////////////////////
// Returns the current playback speed
int EQPacket::playbackSpeed(void)
{
  if (m_vPacket)
    return m_vPacket->playbackSpeed();
  else
    return m_packetCapture->getPlaybackSpeed();
}

///////////////////////////////////////////
// Set the packet playback speed
void EQPacket::setPlayback(int speed)
{
  if (m_vPacket)
  {
    m_vPacket->setPlaybackSpeed(speed);
  }
  else
  {
    m_packetCapture->setPlaybackSpeed(speed);
  }
    
  QString string("");
    
  if (speed == 0)
    string = QString::asprintf("Playback speed set Fast as possible");
  else if (speed < 0)
     string = QString::asprintf("Playback paused (']' to resume)");
  else
     string = QString::asprintf("Playback speed set to %d", speed);

  emit stsMessage(string, 5000);

  emit resetPacket(m_client2WorldStream->packetCount(), client2world);
  emit resetPacket(m_world2ClientStream->packetCount(), world2client);
  emit resetPacket(m_client2ZoneStream->packetCount(), client2zone);
  emit resetPacket(m_zone2ClientStream->packetCount(), zone2client);

  emit playbackSpeedChanged(speed);
}

///////////////////////////////////////////
// Increment the packet playback speed
void EQPacket::incPlayback(void)
{
  int x;

  if (m_vPacket)
  {
    x = m_vPacket->playbackSpeed();
  }
  else
  {
    x = m_packetCapture->getPlaybackSpeed();
  }
    
  switch(x)
  {
	// if we were paused go to 1X not full speed
    case -1:
      x = 1;
      break;
	
    // can't go faster than full speed
    case 0:
      return;
	
    case 9:
      x = 0;
      break;
	
    default:
      x += 1;
      break;
  }
    
  setPlayback(x);
}

///////////////////////////////////////////
// Decrement the packet playback speed
void EQPacket::decPlayback(void)
{
  int x;

  if (m_vPacket)
  {
    x = m_vPacket->playbackSpeed();
  }
  else
  {
    x = m_packetCapture->getPlaybackSpeed();
  }

  switch(x)
  {
    // paused
    case -1:
      return;
	  break;

    // slower than 1 is paused
    case 1:
      x = -1;
      break;
	
    // if we were full speed goto 9
    case 0:
      x = 9;
      break;
	
    default:
      x -= 1;
      break;
  }
    
  setPlayback(x);
}

///////////////////////////////////////////
// Set the IP address of the client to monitor
void EQPacket::monitorIPClient(const QString& ip)
{
  m_ip = ip;

  validateIP();

  emit clientChanged(m_client_addr);

  resetEQPacket();

  seqInfo("Listening for IP client: %s", (m_ip == AUTOMATIC_CLIENT_IP) ? "auto-detect" : m_ip.toLatin1().data());
  if (m_playbackPackets == PLAYBACK_OFF)
  {
    m_packetCapture->setFilter(m_device.toLatin1().data(),
            m_ip.toLatin1().data(),
            m_realtime,
            IP_ADDRESS_TYPE, 0, 0);
    emit filterChanged();
  }
}

///////////////////////////////////////////
// Set the MAC address of the client to monitor
void EQPacket::monitorMACClient(const QString& mac)
{
  m_mac = mac;
  struct in_addr  ia;
  inet_aton (AUTOMATIC_CLIENT_IP, &ia);
  m_detectingClient = true;
  m_client_addr = ia.s_addr;
  emit clientChanged(m_client_addr);

  resetEQPacket();

  if (!m_mac.isEmpty() && m_mac.length() == 17)
  {
    seqInfo("Listening for MAC client: %s", m_mac.toLatin1().data());

    if (m_playbackPackets == PLAYBACK_OFF)
    {
        m_packetCapture->setFilter(m_device.toLatin1().data(),
                m_mac.toLatin1().data(),
                m_realtime,
                MAC_ADDRESS_TYPE, 0, 0);
        emit filterChanged();
    }
  }
  else
  {
      seqWarn("Invalid MAC specified.  Defaulting to auto-detect of next client.");
      monitorNextClient();
  }
}

///////////////////////////////////////////
// Monitor the next client seen
void EQPacket::monitorNextClient()
{
  m_detectingClient = true;
  m_ip = AUTOMATIC_CLIENT_IP;
  struct in_addr  ia;
  inet_aton (m_ip.toLatin1().data(), &ia);
  m_client_addr = ia.s_addr;
  emit clientChanged(m_client_addr);

  resetEQPacket();

  seqInfo("Listening for next client seen. (you must zone for this to work!)");

  if (m_playbackPackets == PLAYBACK_OFF)
  {
    m_packetCapture->setFilter(m_device.toLatin1().data(), NULL,
            m_realtime,
            DEFAULT_ADDRESS_TYPE, 0, 0);
    emit filterChanged();
  }
}

///////////////////////////////////////////
// Monitor for packets on the specified device
void EQPacket::monitorDevice(const QString& dev)
{
  // set the device to use
  m_device = dev;

  // make sure we aren't playing back packets
  if (m_playbackPackets != PLAYBACK_OFF)
    return;

  // stop the current packet capture
  m_packetCapture->stop();

  validateIP();

  resetEQPacket();

  // restart packet capture
  if (m_mac.length() == 17)
  {
    seqInfo("Listening for client MAC: %s", m_mac.toLatin1().data());

    m_packetCapture->start(m_device.toLatin1().data(),
            m_mac.toLatin1().data(),
            m_realtime, MAC_ADDRESS_TYPE );
  }
  else
  {
    if (m_detectingClient)
      seqInfo("Listening for next client seen. (you must zone for this to work!)");
    else
      seqInfo("Listening for client: %s", m_ip.toLatin1().data());

    m_packetCapture->start(m_device.toLatin1().data(),
            m_ip.toLatin1().data(),
            m_realtime, IP_ADDRESS_TYPE );
  }

  emit filterChanged();
}

///////////////////////////////////////////
// Set the session tracking state
void EQPacket::session_tracking(bool enable)
{
  m_session_tracking = enable;
  m_client2WorldStream->setSessionTracking(m_session_tracking);
  m_world2ClientStream->setSessionTracking(m_session_tracking);
  m_client2ZoneStream->setSessionTracking(m_session_tracking);
  m_zone2ClientStream->setSessionTracking(m_session_tracking);
  emit sessionTrackingChanged(m_session_tracking);

}

///////////////////////////////////////////
// Set the current ArqSeqGiveUp
void EQPacket::setArqSeqGiveUp(uint16_t giveUp)
{
  // a sanity check, if the user set it to below 32, they're prolly nuts
  if (giveUp >= 32)
    m_arqSeqGiveUp = giveUp;

  // a sanity check, if the user set it to below 32, they're prolly nuts
  if (m_arqSeqGiveUp >= 32)
    giveUp = m_arqSeqGiveUp;
  else
    giveUp = 32;

  m_client2WorldStream->setArqSeqGiveUp(giveUp);
  m_world2ClientStream->setArqSeqGiveUp(giveUp);
  m_client2ZoneStream->setArqSeqGiveUp(giveUp);
  m_zone2ClientStream->setArqSeqGiveUp(giveUp);
}

void EQPacket::setRealtime(bool val)
{
  m_realtime = val;
}

///////////////////////////////////////////
// Reset EQPacket's state
void EQPacket::resetEQPacket()
{
  m_client2WorldStream->reset();
  m_client2WorldStream->setSessionTracking(m_session_tracking);
  m_world2ClientStream->reset();
  m_world2ClientStream->setSessionTracking(m_session_tracking);
  m_client2ZoneStream->reset();
  m_client2ZoneStream->setSessionTracking(m_session_tracking);
  m_zone2ClientStream->reset();
  m_zone2ClientStream->setSessionTracking(m_session_tracking);

  unlatchClientPort();
}

///////////////////////////////////////////
// Return the current pcap filter
const QString EQPacket::pcapFilter()
{
  // make sure we aren't playing back packets
  if (m_playbackPackets != PLAYBACK_OFF)
    return QString("Playback");

  return m_packetCapture->getFilter();
}


int EQPacket::packetCount(int stream)
{
  return m_streams[stream]->packetCount();
}

uint8_t EQPacket::session_tracking_enabled(void)
{
  return m_zone2ClientStream->sessionTracking();
}

size_t EQPacket::currentCacheSize(int stream)
{
  return m_streams[stream]->currentCacheSize();
}

uint32_t EQPacket::currentMaxLength(int streamId)
{
    return m_streams[streamId]->getMaxLength();
}

uint16_t EQPacket::serverSeqExp(int stream)
{
  return m_streams[stream]->arqSeqExp();
}

// ---------------------------------------------------------------------------
// Session handoff: persist/restore the 4 stream states across a process
// restart triggered by SIGHUP. The file lives in configDir so both the
// outgoing and incoming daemon use the same --config-dir value.

namespace {
  static const uint32_t HANDOFF_MAGIC   = 0x45514853; // "SHEQ"
  static const uint32_t HANDOFF_VERSION = 1;
  struct HandoffFile {
    uint32_t magic;
    uint32_t version;
    EQPacketStream::StreamHandoff streams[MAXSTREAMS];
  };

  QString handoffPath(const QString& configDir)
  {
    if (!configDir.isEmpty())
      return configDir + "/.handoff";
    // Compiled namespace, not a literal — else this drifts off the real root.
    return QDir::homePath() + "/" SEQ_DATA_NAMESPACE "/daemon/.handoff";
  }
} // namespace

void EQPacket::exportHandoffState(const QString& configDir) const
{
  HandoffFile hf{};
  hf.magic   = HANDOFF_MAGIC;
  hf.version = HANDOFF_VERSION;
  for (int i = 0; i < MAXSTREAMS; ++i)
    hf.streams[i] = m_streams[i]->exportState();

  const QString path = handoffPath(configDir);
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning("handoff: failed to write %s: %s",
             qUtf8Printable(path), qUtf8Printable(f.errorString()));
    return;
  }
  f.write(reinterpret_cast<const char*>(&hf), sizeof(hf));
  qInfo("handoff: session state written to %s", qUtf8Printable(path));
}

bool EQPacket::importHandoffState(const QString& configDir)
{
  const QString path = handoffPath(configDir);
  QFile f(path);
  if (!f.exists())
    return false;
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning("handoff: failed to read %s: %s",
             qUtf8Printable(path), qUtf8Printable(f.errorString()));
    return false;
  }
  HandoffFile hf{};
  if (f.read(reinterpret_cast<char*>(&hf), sizeof(hf)) != sizeof(hf)) {
    qWarning("handoff: truncated handoff file %s — ignoring",
             qUtf8Printable(path));
    f.close();
    QFile::remove(path);
    return false;
  }
  f.close();
  QFile::remove(path);

  if (hf.magic != HANDOFF_MAGIC || hf.version != HANDOFF_VERSION) {
    qWarning("handoff: bad magic/version in %s — ignoring",
             qUtf8Printable(path));
    return false;
  }
  for (int i = 0; i < MAXSTREAMS; ++i)
    m_streams[i]->importState(hf.streams[i]);
  qInfo("handoff: session state restored from %s", qUtf8Printable(path));
  return true;
}
