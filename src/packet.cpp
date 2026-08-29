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
#include <cstdlib>
#include <algorithm>
#include <unistd.h>
#include <netdb.h>

#ifdef __FreeBSD__
#include "packet.h"
#endif
#include <netinet/if_ether.h>

#include <QDir>
#include <QCoreApplication>
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

QString shadowBackendName()
{
#if defined(SEQ_TARGET_LIVE)
  return QStringLiteral("live");
#elif defined(SEQ_TARGET_TEST)
  return QStringLiteral("test");
#elif defined(SEQ_TARGET_EQL)
  return QStringLiteral("eql");
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
		   seq::shadow::LifecycleSelector lifecycleSelector,
		   seq::shadow::EntitySelector entitySelector,
		   seq::shadow::PlayerSelector playerSelector,
		   seq::shadow::ProgressionSelector progressionSelector,
		   seq::shadow::LootSelector lootSelector,
		   seq::shadow::CombatSelector combatSelector,
		   seq::shadow::CommunicationSelector communicationSelector,
		   QString applicationTraceDir,
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
    m_playbackSpeed(playbackSpeed),
    m_lifecycleSelector(lifecycleSelector),
    m_entitySelector(entitySelector),
    m_playerSelector(playerSelector),
    m_progressionSelector(progressionSelector),
    m_lootSelector(lootSelector),
    m_combatSelector(combatSelector),
    m_communicationSelector(communicationSelector)
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
    m_applicationTraceCatalogHash = QString::fromStdString(
        m_shadowRegistry->contentHash(shadowBackend()));
    seqInfo("Rust shadow protocol catalog: %s",
            qUtf8Printable(m_applicationTraceCatalogHash));
    if (!applicationTraceDir.isEmpty()) {
      QDir directory(applicationTraceDir);
      if (!directory.exists() && !QDir().mkpath(directory.absolutePath()))
        throw std::runtime_error(
            QStringLiteral("could not create application trace directory %1")
                .arg(directory.absolutePath()).toStdString());
      const QString run = QStringLiteral("scry-%1-%2-%3")
          .arg(shadowBackendName(),
               QDateTime::currentDateTimeUtc().toString(
                   QStringLiteral("yyyyMMdd-HHmmsszzz")))
          .arg(QCoreApplication::applicationPid());
      m_applicationTracePrefix = directory.filePath(run);
      seqInfo("Application packet traces: %s-session-NNNNNN-part-NNNN.trace.json",
              qUtf8Printable(m_applicationTracePrefix));
    }
  } catch (const std::exception& error) {
    if (m_lifecycleSelector == seq::shadow::LifecycleSelector::Rust ||
        m_entitySelector == seq::shadow::EntitySelector::Rust ||
        m_playerSelector == seq::shadow::PlayerSelector::Rust ||
        m_progressionSelector == seq::shadow::ProgressionSelector::Rust ||
        m_lootSelector == seq::shadow::LootSelector::Rust ||
        m_combatSelector == seq::shadow::CombatSelector::Rust ||
        m_communicationSelector ==
            seq::shadow::CommunicationSelector::Rust ||
        !applicationTraceDir.isEmpty())
      throw;
    // Shadow diagnostics must never take down the legacy mutation path.
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
    auto* zoneObserver = new ZoneServerObserver(
        &box, box.world_s2c, [this] { return nowMs(); }, observerParent);
    zoneObserver->setMutationGuard(
        [this] { return legacyLifecycleEnabledForCurrentPacket(); });
    zoneObserver->setObservedCallback(
        [this](const QString& host, uint16_t port) {
          observeLegacyLifecycle(seq::shadow::observeZoneServer(
              host.toStdString(), port));
        });
    auto* promoter = new NamePromoter(&box, &m_boxes, c2s, observerParent);
    promoter->setMutationGuard(
        [this] { return legacyLifecycleEnabledForCurrentPacket(); });
    promoter->setPromotedObserver([this](const QString& name) {
      observeLegacyLifecycle(seq::shadow::observeSessionReset(
          seq::rust::EventSessionResetReason::EnterWorld));
      observeLegacyLifecycle(
          seq::shadow::observeEnterWorld(name.toStdString()));
    });

    if (m_shadowRegistry) {
      try {
        auto session = std::make_unique<seq::shadow::Session>(
            *m_shadowRegistry, shadowBackend(), 256, 4 * 1024 * 1024,
            m_lifecycleSelector, m_entitySelector, m_playerSelector,
            m_progressionSelector, m_lootSelector, m_combatSelector,
            m_communicationSelector);
        session->setTraceWriter(makeApplicationTraceWriter());
        m_shadowSessions.emplace(&box, std::move(session));
        for (EQPacketStream* stream : {box.world_c2s, box.world_s2c,
                                       box.zone_c2s, box.zone_s2c})
          installShadowHook(stream);
      } catch (const std::exception& error) {
        if (m_lifecycleSelector == seq::shadow::LifecycleSelector::Rust ||
            m_entitySelector == seq::shadow::EntitySelector::Rust ||
            m_playerSelector == seq::shadow::PlayerSelector::Rust ||
            m_progressionSelector == seq::shadow::ProgressionSelector::Rust ||
            m_lootSelector == seq::shadow::LootSelector::Rust ||
            m_combatSelector == seq::shadow::CombatSelector::Rust ||
            m_communicationSelector ==
                seq::shadow::CommunicationSelector::Rust ||
            !m_applicationTracePrefix.isEmpty()) {
          m_lifecycleFatal = true;
          qCritical("Rust-owned lifecycle session creation failed for box %s: %s",
                    qUtf8Printable(box.box_id), error.what());
          QCoreApplication::exit(EXIT_FAILURE);
          return;
        }
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
             EQPacketFlowKey flowKey, bool sourceIsLow,
             uintptr_t attributionToken) {
        return decodeShadowApplication(streamId, direction, opcode, payload,
                                       payloadSize, timestamp, flowKey,
                                       sourceIsLow, attributionToken);
      },
      [this] { return int64_t(nowMs()); },
      [this](bool legacyDispatched) {
        completeShadowApplication(legacyDispatched);
      });
}

std::unique_ptr<seq::shadow::ApplicationTraceWriter>
EQPacket::makeApplicationTraceWriter()
{
  if (m_applicationTracePrefix.isEmpty()) return {};
  const QString prefix = QStringLiteral("%1-session-%2")
      .arg(m_applicationTracePrefix)
      .arg(++m_applicationTraceSession, 6, 10, QLatin1Char('0'));
  return std::make_unique<seq::shadow::ApplicationTraceWriter>(
      prefix, shadowBackendName(), m_applicationTraceCatalogHash,
      /*synthetic*/ false);
}

bool EQPacket::rustOwnsAnyFamily() const
{
  return m_lifecycleSelector == seq::shadow::LifecycleSelector::Rust ||
         m_entitySelector == seq::shadow::EntitySelector::Rust ||
         m_playerSelector == seq::shadow::PlayerSelector::Rust ||
         m_progressionSelector == seq::shadow::ProgressionSelector::Rust ||
         m_lootSelector == seq::shadow::LootSelector::Rust ||
         m_combatSelector == seq::shadow::CombatSelector::Rust ||
         m_communicationSelector ==
             seq::shadow::CommunicationSelector::Rust;
}

seq::shadow::Session* EQPacket::provisionalShadowSession(
    EQPacketFlowKey flowKey)
{
  if (!m_shadowRegistry || !flowKey.isValid()) return nullptr;
  auto found = m_provisionalShadowSessions.find(flowKey);
  if (found != m_provisionalShadowSessions.end())
    return found->second.disabled ? nullptr : found->second.session.get();

  try {
    ProvisionalShadowSession provisional;
    provisional.session = std::make_unique<seq::shadow::Session>(
        *m_shadowRegistry, shadowBackend(), 256, 4 * 1024 * 1024,
        m_lifecycleSelector, m_entitySelector, m_playerSelector,
        m_progressionSelector, m_lootSelector, m_combatSelector,
        m_communicationSelector);
    auto [it, inserted] =
        m_provisionalShadowSessions.emplace(flowKey, std::move(provisional));
    return inserted ? it->second.session.get() : nullptr;
  } catch (const std::exception& error) {
    if (rustOwnsAnyFamily() ||
        !m_applicationTracePrefix.isEmpty()) {
      m_lifecycleFatal = true;
      qCritical("Provisional Rust session creation failed: %s",
                error.what());
      QCoreApplication::exit(EXIT_FAILURE);
      return nullptr;
    }
    seqWarn("Provisional Rust shadow session creation failed: %s", error.what());
    return nullptr;
  }
}

void EQPacket::writeProvisionalTrace(
    const seq::shadow::ProvisionalPacketFlow& flow)
{
  if (m_applicationTracePrefix.isEmpty() || flow.packets.empty()) return;
  if (!flow.complete)
    throw std::runtime_error("incomplete provisional packet trace");
  auto writer = makeApplicationTraceWriter();
  std::optional<int64_t> traceTimestamp;
  for (const auto& packet : flow.packets) {
    if (traceTimestamp && packet.timestamp < *traceTimestamp)
      writer->finalize();
    traceTimestamp = packet.timestamp;
    const bool world = packet.stream == client2world ||
                       packet.stream == world2client;
    writer->push(
        world ? seq::shadow::Stream::World : seq::shadow::Stream::Zone,
        packet.opcode,
        packet.direction == DIR_Server
            ? seq::shadow::Direction::ServerToClient
            : seq::shadow::Direction::ClientToServer,
        reinterpret_cast<const uint8_t*>(packet.payload.constData()),
        size_t(packet.payload.size()), packet.timestamp);
  }
  writer->finalize();
}

void EQPacket::finalizeProvisionalFlow(
    EQPacketFlowKey flowKey, seq::shadow::ProvisionalPacketFlow flow,
    seq::shadow::FlushReason reason)
{
  auto preview = m_provisionalShadowSessions.find(flowKey);
  try {
    if (!flow.complete &&
        (rustOwnsAnyFamily() || !m_applicationTracePrefix.isEmpty()))
      throw std::runtime_error(
          "bounded provisional packet history is incomplete");

    if (preview != m_provisionalShadowSessions.end() &&
        !preview->second.disabled && preview->second.session) {
      const auto& flushed = preview->second.session->flush(reason);
      const bool ownedOutput =
          (preview->second.session->appliesRustLifecycle() &&
           !seq::shadow::lifecycleObservations(flushed.batch).empty()) ||
          (preview->second.session->appliesRustEntities() &&
           !seq::shadow::entityObservations(flushed.batch).empty()) ||
          (preview->second.session->appliesRustPlayers() &&
           !seq::shadow::playerObservations(flushed.batch).empty()) ||
          (preview->second.session->appliesRustProgression() &&
           !seq::shadow::progressionObservations(flushed.batch).empty()) ||
          (preview->second.session->appliesRustLoot() &&
           !seq::shadow::lootObservations(flushed.batch).empty()) ||
          (preview->second.session->appliesRustCombat() &&
           !seq::shadow::combatObservations(flushed.batch).empty()) ||
          (preview->second.session->appliesRustCommunication() &&
           !seq::shadow::communicationObservations(flushed.batch).empty());
      if (ownedOutput)
        throw std::runtime_error(
            "unattributed Rust-owned output cannot mutate host state");
    }
    writeProvisionalTrace(flow);
  } catch (const std::exception& error) {
    if (rustOwnsAnyFamily() || !m_applicationTracePrefix.isEmpty()) {
      m_lifecycleFatal = true;
      qCritical("Provisional Rust flow finalization failed: %s", error.what());
      QCoreApplication::exit(EXIT_FAILURE);
    } else {
      seqWarn("Provisional Rust shadow finalization failed: %s", error.what());
    }
  }
  if (preview != m_provisionalShadowSessions.end())
    m_provisionalShadowSessions.erase(preview);
}

void EQPacket::finalizeAllProvisionalFlows(
    seq::shadow::FlushReason reason)
{
  for (auto& evicted : m_provisionalPackets.takeAll())
    finalizeProvisionalFlow(evicted.key, std::move(evicted.flow), reason);
  m_provisionalShadowSessions.clear();
}

bool EQPacket::replayProvisionalFlow(
    EQPacketFlowKey flowKey, Box* box,
    seq::shadow::ProvisionalPacketFlow flow)
{
  if (!flow.complete) {
    if (rustOwnsAnyFamily() || !m_applicationTracePrefix.isEmpty()) {
      m_lifecycleFatal = true;
      qCritical("Cannot adopt incomplete provisional Rust flow");
      QCoreApplication::exit(EXIT_FAILURE);
      return false;
    }
    seqWarn("Discarding incomplete provisional Rust shadow flow");
    return true;
  }

  auto attributed = m_shadowSessions.find(box);
  if (attributed == m_shadowSessions.end() ||
      m_shadowDisabled.count(box) != 0)
    return !m_lifecycleFatal;

  if (!flow.packets.empty()) attributed->second->finalizeTrace();
  std::optional<int64_t> replayTimestamp;
  for (const auto& packet : flow.packets) {
    if (replayTimestamp && packet.timestamp < *replayTimestamp)
      attributed->second->finalizeTrace();
    replayTimestamp = packet.timestamp;
    uint8_t direction = packet.direction;
    const bool world = packet.stream == client2world ||
                       packet.stream == world2client;
    if (!world) {
      const uint64_t source = packet.sourceIsLow
          ? flowKey.endpointLow : flowKey.endpointHigh;
      direction = in_addr_t(source >> 16) == box->client_ip
          ? DIR_Client : DIR_Server;
    }
    if (!decodeShadowApplication(
            EQStreamID(packet.stream), direction, packet.opcode,
            reinterpret_cast<const uint8_t*>(packet.payload.constData()),
            size_t(packet.payload.size()), packet.timestamp, flowKey,
            packet.sourceIsLow, reinterpret_cast<uintptr_t>(box)))
      return false;
    completeShadowApplication(false);
  }
  seqInfo("Rust session replayed %zu pre-attribution packets for box %s",
          flow.packets.size(), qUtf8Printable(box->box_id));
  return !m_lifecycleFatal;
}

bool EQPacket::bindShadowFlow(EQPacketFlowKey flowKey, Box* box)
{
  if (!m_shadowRegistry) return true;
  if (!flowKey.isValid() || !box) return !m_lifecycleFatal;
  m_flowOwners[flowKey] = box;
  auto flow = m_provisionalPackets.take(flowKey);
  if (!flow) return !m_lifecycleFatal;
  m_provisionalShadowSessions.erase(flowKey);
  return replayProvisionalFlow(flowKey, box, std::move(*flow));
}

bool EQPacket::decodeShadowApplication(
    EQStreamID stream, uint8_t direction, uint16_t opcode,
    const uint8_t* payload, size_t payloadSize, int64_t timestamp,
    EQPacketFlowKey flowKey, bool sourceIsLow, uintptr_t attributionToken)
{
  const bool world = stream == client2world || stream == world2client;
  Box* box = reinterpret_cast<Box*>(attributionToken);
  seq::shadow::Session* session = nullptr;

  auto owner = m_flowOwners.find(flowKey);
  if (owner != m_flowOwners.end()) {
    box = owner->second;
  } else if (box && !bindShadowFlow(flowKey, box)) {
    return false;
  }

  if (box) {
    auto attributed = m_shadowSessions.find(box);
    if (attributed == m_shadowSessions.end() ||
        m_shadowDisabled.count(box) != 0)
      return !m_lifecycleFatal;

    session = attributed->second.get();
  } else {
    seq::shadow::BufferedApplicationPacket packet;
    packet.order = ++m_applicationDispatchOrder;
    packet.stream = uint8_t(stream);
    packet.direction = direction;
    packet.sourceIsLow = sourceIsLow;
    packet.opcode = opcode;
    packet.payload = QByteArray(reinterpret_cast<const char*>(payload),
                                qsizetype(payloadSize));
    packet.timestamp = timestamp;
    auto appended = m_provisionalPackets.append(flowKey, std::move(packet));
    for (auto& evicted : appended.evicted) {
      finalizeProvisionalFlow(evicted.key, std::move(evicted.flow),
                              seq::shadow::FlushReason::Shutdown);
      if (m_lifecycleFatal) return false;
    }
    if (appended.flowInvalidated) {
      m_provisionalShadowSessions.erase(flowKey);
      if (rustOwnsAnyFamily() || !m_applicationTracePrefix.isEmpty()) {
        m_lifecycleFatal = true;
        qCritical("Provisional Rust flow exceeded bounded replay history");
        QCoreApplication::exit(EXIT_FAILURE);
        return false;
      }
      seqWarn("Provisional Rust shadow disabled after replay history overflow");
      return true;
    }
    if (!appended.stored) return !m_lifecycleFatal;
    session = provisionalShadowSession(flowKey);
  }

  if (!session) return !m_lifecycleFatal;
  m_currentLifecycleSession = session;
  m_currentRustLifecycleKinds.clear();
  m_currentRustEntityKinds.clear();
  m_currentRustPlayerKinds.clear();
  m_currentRustProgressionKinds.clear();
  m_currentRustLootKinds.clear();
  m_currentRustCombatKinds.clear();
  m_currentRustCommunicationKinds.clear();
  m_pendingRustLoot.reset();
  m_currentRustPacketDecoded = false;
  try {
    const seq::shadow::Record& record = session->decode(
        world ? seq::shadow::Stream::World : seq::shadow::Stream::Zone,
        opcode,
        direction == DIR_Server
            ? seq::shadow::Direction::ServerToClient
            : seq::shadow::Direction::ClientToServer,
        payload, payloadSize, timestamp);
    m_currentRustPacketDecoded =
        record.batch.disposition == seq::shadow::Disposition::Decoded;

    auto rustEvents = seq::shadow::lifecycleObservations(record.batch);
    const bool ownsGlobalLifecycle =
        !box || !m_lifecycleGlobalOwnershipPredicate ||
        m_lifecycleGlobalOwnershipPredicate(box);
    if (!ownsGlobalLifecycle) {
      // Time is daemon-global and the legacy handler is wired only for the
      // active box. ZoneServerInfo still owns per-box routing, but its public
      // ZoneServer envelope is likewise active-box-only.
      rustEvents.erase(
          std::remove_if(rustEvents.begin(), rustEvents.end(), [](const auto& e) {
            return e.kind == seq::shadow::LifecycleKind::TimeOfDay;
          }),
          rustEvents.end());
    }
#if defined(SEQ_TARGET_EQL)
    // EQL's public host reducer has no standalone environment action: NewZone
    // resolves names and publishes one ZoneChanged envelope. Keep applying the
    // decoded environment state in Rust mode, but compare the ordered actions
    // that the production EQL reducer actually exposes.
    rustEvents.erase(
        std::remove_if(rustEvents.begin(), rustEvents.end(), [](const auto& e) {
          return e.kind ==
                 seq::shadow::LifecycleKind::ZoneEnvironmentChanged;
        }),
        rustEvents.end());
#endif
    for (const auto& observation : rustEvents)
      m_currentRustLifecycleKinds.push_back(observation.kind);
    auto rustEntityEvents = seq::shadow::entityObservations(record.batch);
    for (const auto& observation : rustEntityEvents)
      m_currentRustEntityKinds.push_back(observation.kind);
    // Runtime shadow comparison gets field equality from the exact seq.v1
    // envelopes below. Keep this vector as the independently observed legacy
    // action order, because legacy manager signals cannot recover every
    // optional input field after mutation.
    for (auto& observation : rustEntityEvents)
      observation.payload.clear();
    auto rustPlayerEvents = seq::shadow::playerObservations(record.batch);
    for (const auto& observation : rustPlayerEvents)
      m_currentRustPlayerKinds.push_back(observation.kind);
    for (auto& observation : rustPlayerEvents)
      observation.payload.clear();
    auto rustProgressionEvents =
        seq::shadow::progressionObservations(record.batch);
    for (const auto& observation : rustProgressionEvents)
      m_currentRustProgressionKinds.push_back(observation.kind);
    for (auto& observation : rustProgressionEvents)
      observation.payload.clear();
    auto rustLootEvents = seq::shadow::lootObservations(record.batch);
    for (const auto& observation : rustLootEvents)
      m_currentRustLootKinds.push_back(observation.kind);
    for (auto& observation : rustLootEvents)
      observation.payload.clear();
    auto rustCombatEvents = seq::shadow::combatObservations(record.batch);
    for (const auto& observation : rustCombatEvents)
      m_currentRustCombatKinds.push_back(observation.kind);
    for (auto& observation : rustCombatEvents)
      observation.payload.clear();
    auto rustCommunicationEvents =
        seq::shadow::communicationObservations(record.batch);
    for (const auto& observation : rustCommunicationEvents)
      m_currentRustCommunicationKinds.push_back(observation.kind);
    for (auto& observation : rustCommunicationEvents)
      observation.payload.clear();
    const bool orderedLifecycleCommunication =
        session->appliesRustLifecycle() &&
        session->appliesRustCommunication();
    if (orderedLifecycleCommunication &&
        (!rustEvents.empty() || !rustCommunicationEvents.empty())) {
      if (box) {
        if (!m_lifecycleEventHandler || !m_communicationEventHandler)
          throw std::runtime_error(
              "Rust lifecycle/communication event has no host applier");
        // Reset batches deliberately put communication clears before the
        // lifecycle transition. Dispatch the two owned families in their shared
        // event order so group/guild projections cannot trail ZoneChanged.
        for (const seq::shadow::Event& event : record.batch.events) {
          if (seq::shadow::isCommunicationEvent(event))
            m_communicationEventHandler(box, event);
          else if (seq::shadow::isLifecycleEvent(event))
            m_lifecycleEventHandler(box, event);
        }
      }
    }
    if (session->comparesLifecycle()) {
        PendingLifecycleComparison pending;
        pending.session = session;
        pending.box = box;
        pending.rustEvents = rustEvents;
        pending.rustProjections = seq::shadow::projectLifecycle(record.batch);
        if (!ownsGlobalLifecycle) {
          pending.rustProjections.erase(
              std::remove_if(
                  pending.rustProjections.begin(),
                  pending.rustProjections.end(), [](const auto& envelope) {
                    return envelope.has_eq_time_sync() ||
                           envelope.has_zone_server();
                  }),
              pending.rustProjections.end());
        }
        for (const seq::shadow::Event& event : record.batch.events) {
#if !defined(SEQ_TARGET_EQL)
          if (std::holds_alternative<seq::shadow::PlayerProfile>(event))
            pending.expectsHostZoneProjection = true;
#endif
          if (const auto* transition =
                  std::get_if<seq::shadow::ZoneTransition>(&event)) {
            if (transition->payload.confirmed &&
                transition->payload.has_zone_id)
              pending.expectsHostZoneProjection = true;
          }
        }
        m_pendingLifecycle = std::move(pending);
    } else if (!rustEvents.empty() && session->appliesRustLifecycle() &&
               !orderedLifecycleCommunication) {
      if (box) {
        if (!m_lifecycleEventHandler) {
          throw std::runtime_error(
              "Rust lifecycle event has no host applier");
        }
        for (const seq::shadow::Event& event : record.batch.events) {
          if (seq::shadow::isLifecycleEvent(event))
            m_lifecycleEventHandler(box, event);
        }
      }
    }
    if (session->comparesEntities()) {
      PendingEntityComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(rustEntityEvents);
      pending.rustProjections = seq::shadow::projectEntities(record.batch);
      m_pendingEntity = std::move(pending);
    } else if (!rustEntityEvents.empty() && session->appliesRustEntities()) {
      if (box) {
        if (!m_entityEventHandler)
          throw std::runtime_error("Rust entity event has no host applier");
        for (const seq::shadow::Event& event : record.batch.events) {
          if (seq::shadow::isEntityEvent(event))
            m_entityEventHandler(box, event);
        }
      }
    }
    if (session->comparesPlayers()) {
      PendingPlayerComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(rustPlayerEvents);
      pending.rustProjections = seq::shadow::projectPlayers(record.batch);
      m_pendingPlayer = std::move(pending);
    } else if (!rustPlayerEvents.empty() && session->appliesRustPlayers()) {
      if (box) {
        if (!m_playerEventHandler)
          throw std::runtime_error("Rust player event has no host applier");
        for (const seq::shadow::Event& event : record.batch.events) {
          if (seq::shadow::isPlayerEvent(event))
            m_playerEventHandler(box, event);
        }
      }
    }
    if (session->comparesProgression()) {
      PendingProgressionComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(rustProgressionEvents);
      pending.rustProjections = seq::shadow::projectProgression(record.batch);
      m_pendingProgression = std::move(pending);
    } else if (!rustProgressionEvents.empty() &&
               session->appliesRustProgression()) {
      if (box) {
        if (!m_progressionBatchHandler)
          throw std::runtime_error(
              "Rust progression event has no host applier");
        m_progressionBatchHandler(box, record.batch);
      }
    }
    if (session->comparesLoot()) {
      PendingLootComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(rustLootEvents);
      pending.rustProjections = seq::shadow::projectLoot(record.batch);
      m_pendingLoot = std::move(pending);
    } else if (!rustLootEvents.empty() && session->appliesRustLoot()) {
      if (box) {
        if (!m_lootBatchHandler)
          throw std::runtime_error("Rust loot event has no host applier");
        // Legacy loot confirmations adjust the Player money total even when
        // Rust owns correlation. Publish the semantic loot event after that
        // handler runs so seq.v1 ordering stays PlayerStats then transaction.
        m_pendingRustLoot = PendingRustLootApplication{box, &record.batch};
      }
    }
    if (session->comparesCombat()) {
      PendingCombatComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(rustCombatEvents);
      pending.rustProjections = seq::shadow::projectCombat(record.batch);
      m_pendingCombat = std::move(pending);
    } else if (!rustCombatEvents.empty() && session->appliesRustCombat()) {
      if (box) {
        if (!m_combatBatchHandler)
          throw std::runtime_error("Rust combat event has no host applier");
        m_combatBatchHandler(box, record.batch);
      }
    }
    if (session->comparesCommunication()) {
      PendingCommunicationComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(rustCommunicationEvents);
      if (m_communicationProjectionProvider)
        pending.rustProjections =
            m_communicationProjectionProvider(box, record.batch);
      else
        pending.rustProjections =
            seq::shadow::projectCommunication(record.batch);
      m_pendingCommunication = std::move(pending);
    } else if (!rustCommunicationEvents.empty() &&
               session->appliesRustCommunication() &&
               !orderedLifecycleCommunication) {
      if (box) {
        if (!m_communicationEventHandler)
          throw std::runtime_error(
              "Rust communication event has no host applier");
        for (const seq::shadow::Event& event : record.batch.events)
          if (seq::shadow::isCommunicationEvent(event))
            m_communicationEventHandler(box, event);
      }
    }
  } catch (const std::exception& error) {
    m_pendingLifecycle.reset();
    m_pendingEntity.reset();
    m_pendingPlayer.reset();
    m_pendingProgression.reset();
    m_pendingLoot.reset();
    m_pendingCombat.reset();
    m_pendingCommunication.reset();
    m_pendingRustLoot.reset();
    if (rustOwnsAnyFamily() || session->traceEnabled() ||
        !m_applicationTracePrefix.isEmpty()) {
      m_lifecycleFatal = true;
      qCritical("Rust lifecycle decode/apply failed for box %s: %s; "
                "the immutable Rust-owned session cannot fall back in place",
                box ? qUtf8Printable(box->box_id) : "<unattributed>",
                error.what());
      QCoreApplication::exit(EXIT_FAILURE);
      m_currentLifecycleSession = nullptr;
      m_currentRustEntityKinds.clear();
      m_currentRustPlayerKinds.clear();
      m_currentRustProgressionKinds.clear();
      m_currentRustLootKinds.clear();
      m_currentRustCombatKinds.clear();
      m_currentRustCommunicationKinds.clear();
      m_currentRustPacketDecoded = false;
      return false;
    }
    if (box) {
      m_shadowDisabled.insert(box);
      seqWarn("Rust shadow disabled for box %s after decode error: %s",
              qUtf8Printable(box->box_id), error.what());
    } else {
      auto provisional = m_provisionalShadowSessions.find(flowKey);
      if (provisional != m_provisionalShadowSessions.end())
        provisional->second.disabled = true;
      seqWarn("Provisional Rust shadow disabled after decode error: %s",
              error.what());
    }
  }
  if (!box) {
    // The raw history can safely rebuild correlation after attribution, but
    // applying an already-emitted owned event later is not generally exact.
    // Some legacy compatibility handlers mutate host state before their Rust
    // ownership guard (loot money is one example), so suppressing dispatch or
    // replaying it later can respectively lose or duplicate that mutation.
    const bool delayedOwnedOutput =
        (session->appliesRustLifecycle() &&
         !m_currentRustLifecycleKinds.empty()) ||
        (session->appliesRustEntities() &&
         !m_currentRustEntityKinds.empty()) ||
        (session->appliesRustPlayers() &&
         !m_currentRustPlayerKinds.empty()) ||
        (session->appliesRustProgression() &&
         !m_currentRustProgressionKinds.empty()) ||
        (session->appliesRustLoot() && !m_currentRustLootKinds.empty()) ||
        (session->appliesRustCombat() && !m_currentRustCombatKinds.empty()) ||
        (session->appliesRustCommunication() &&
         !m_currentRustCommunicationKinds.empty());
    if (delayedOwnedOutput) {
      m_lifecycleFatal = true;
      qCritical("Rust-owned event requires flow attribution before host apply");
      QCoreApplication::exit(EXIT_FAILURE);
      return false;
    }
  }
  return true;
}

bool EQPacket::legacyLifecycleEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyLifecycle());
}

bool EQPacket::rustLifecycleAcceptedForCurrentPacket(
    seq::shadow::LifecycleKind kind) const
{
  return std::find(m_currentRustLifecycleKinds.begin(),
                   m_currentRustLifecycleKinds.end(), kind) !=
         m_currentRustLifecycleKinds.end();
}

bool EQPacket::legacyEntitiesEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyEntities());
}

bool EQPacket::rustEntityAcceptedForCurrentPacket(
    seq::shadow::EntityKind kind) const
{
  return std::find(m_currentRustEntityKinds.begin(),
                   m_currentRustEntityKinds.end(), kind) !=
         m_currentRustEntityKinds.end();
}

bool EQPacket::legacyPlayersEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyPlayers());
}

bool EQPacket::legacyPlayerAppearanceEnabledForCurrentPacket() const
{
  if (legacyPlayersEnabledForCurrentPacket()) return true;
  // These opcodes also target non-player spawns. A decoded non-player event
  // stays outside player decoding and keeps its legacy owner; malformed or unhandled
  // Rust-owned packets remain fail-closed.
  return m_currentRustPacketDecoded &&
         !rustPlayerAcceptedForCurrentPacket(
             seq::shadow::PlayerKind::PlayerAppearanceUpdated);
}

bool EQPacket::rustPlayerAcceptedForCurrentPacket(
    seq::shadow::PlayerKind kind) const
{
  return std::find(m_currentRustPlayerKinds.begin(),
                   m_currentRustPlayerKinds.end(), kind) !=
         m_currentRustPlayerKinds.end();
}

bool EQPacket::legacyProgressionEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyProgression());
}

bool EQPacket::rustProgressionAcceptedForCurrentPacket(
    seq::shadow::ProgressionKind kind) const
{
  return std::find(m_currentRustProgressionKinds.begin(),
                   m_currentRustProgressionKinds.end(), kind) !=
         m_currentRustProgressionKinds.end();
}

bool EQPacket::legacyLootEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyLoot());
}

bool EQPacket::rustLootAcceptedForCurrentPacket(
    seq::shadow::LootKind kind) const
{
  return std::find(m_currentRustLootKinds.begin(),
                   m_currentRustLootKinds.end(), kind) !=
         m_currentRustLootKinds.end();
}

bool EQPacket::legacyCombatEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyCombat());
}

bool EQPacket::rustCombatAcceptedForCurrentPacket(
    seq::shadow::CombatKind kind) const
{
  return std::find(m_currentRustCombatKinds.begin(),
                   m_currentRustCombatKinds.end(), kind) !=
         m_currentRustCombatKinds.end();
}

bool EQPacket::legacyCommunicationEnabledForCurrentPacket() const
{
  return !m_lifecycleFatal &&
         (!m_currentLifecycleSession ||
          m_currentLifecycleSession->runsLegacyCommunication());
}

bool EQPacket::rustCommunicationAcceptedForCurrentPacket(
    seq::shadow::CommunicationKind kind) const
{
  return std::find(m_currentRustCommunicationKinds.begin(),
                   m_currentRustCommunicationKinds.end(), kind) !=
         m_currentRustCommunicationKinds.end();
}

void EQPacket::observeLegacyLifecycle(
    seq::shadow::LifecycleObservation observation)
{
  if (m_pendingLifecycle)
    m_pendingLifecycle->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyLifecycleProjection(seq::v1::Envelope envelope)
{
  if (m_pendingLifecycle)
    m_pendingLifecycle->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::observeLegacyEntity(
    seq::shadow::EntityObservation observation)
{
  if (m_pendingEntity)
    m_pendingEntity->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyEntityProjection(seq::v1::Envelope envelope)
{
  if (m_pendingEntity)
    m_pendingEntity->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::observeLegacyPlayer(
    seq::shadow::PlayerObservation observation)
{
  if (m_pendingPlayer)
    m_pendingPlayer->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyPlayerProjection(seq::v1::Envelope envelope)
{
  if (m_pendingPlayer)
    m_pendingPlayer->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::observeLegacyProgression(
    seq::shadow::ProgressionObservation observation)
{
  if (m_pendingProgression)
    m_pendingProgression->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyProgressionProjection(seq::v1::Envelope envelope)
{
  if (m_pendingProgression)
    m_pendingProgression->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::observeLegacyLoot(seq::shadow::LootObservation observation)
{
  if (m_pendingLoot)
    m_pendingLoot->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyLootProjection(seq::v1::Envelope envelope)
{
  if (m_pendingLoot)
    m_pendingLoot->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::observeLegacyCombat(
    seq::shadow::CombatObservation observation)
{
  if (m_pendingCombat)
    m_pendingCombat->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyCombatProjection(seq::v1::Envelope envelope)
{
  if (!m_pendingCombat) return;
  const size_t index = m_pendingCombat->legacyProjections.size();
  if (index < m_pendingCombat->rustProjections.size()) {
    auto& rust = m_pendingCombat->rustProjections[index];
    if (rust.has_combat() && envelope.has_combat()) {
      rust.mutable_combat()->set_source_name(envelope.combat().source_name());
      rust.mutable_combat()->set_target_name(envelope.combat().target_name());
      rust.mutable_combat()->set_spell_name(envelope.combat().spell_name());
    } else if (rust.has_spawn_cast() && envelope.has_spawn_cast()) {
      rust.mutable_spawn_cast()->set_caster_name(
          envelope.spawn_cast().caster_name());
      rust.mutable_spawn_cast()->set_spell_name(
          envelope.spawn_cast().spell_name());
    }
  }
  m_pendingCombat->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::observeLegacyCommunication(
    seq::shadow::CommunicationObservation observation)
{
  if (m_pendingCommunication)
    m_pendingCommunication->legacyEvents.push_back(std::move(observation));
}

void EQPacket::observeLegacyCommunicationProjection(seq::v1::Envelope envelope)
{
  if (m_pendingCommunication)
    m_pendingCommunication->legacyProjections.push_back(std::move(envelope));
}

void EQPacket::applyValidatedZoneServerInfo(Box* box, uint16_t port)
{
  if (!box) return;
  box->expected_zone_server_port = htons(port);
  box->zone_client_port = 0;
  box->zone_server_port_bound = 0;
  box->zone_await_ms = nowMs();
}

void EQPacket::completeShadowApplication(bool legacyDispatched)
{
  const auto rustLoot = m_pendingRustLoot;
  m_pendingRustLoot.reset();
  m_currentLifecycleSession = nullptr;
  m_currentRustLifecycleKinds.clear();
  m_currentRustEntityKinds.clear();
  m_currentRustPlayerKinds.clear();
  m_currentRustProgressionKinds.clear();
  m_currentRustLootKinds.clear();
  m_currentRustCombatKinds.clear();
  m_currentRustCommunicationKinds.clear();
  m_currentRustPacketDecoded = false;
  if (m_pendingLifecycle) {
    PendingLifecycleComparison pending = std::move(*m_pendingLifecycle);
    m_pendingLifecycle.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() || !pending.legacyProjections.empty())) {
      if (m_lifecycleProjectionEnricher)
        m_lifecycleProjectionEnricher(
            pending.box, pending.expectsHostZoneProjection,
            pending.rustEvents, pending.rustProjections);
      const seq::shadow::LifecycleComparison comparison =
          seq::shadow::compareLifecycle(
              pending.rustEvents, pending.rustProjections,
              pending.legacyEvents, pending.legacyProjections);
      if (pending.session)
        pending.session->recordLifecycleComparison(comparison);
      if (!comparison.orderedEventsEqual || !comparison.projectionsEqual) {
        seqWarn("Rust lifecycle shadow mismatch: events rust=%zu legacy=%zu, "
                "seq.v1 rust=%zu legacy=%zu",
                comparison.rustEventCount, comparison.legacyEventCount,
                comparison.rustProjectionCount,
                comparison.legacyProjectionCount);
      }
    }
  }
  if (m_pendingEntity) {
    PendingEntityComparison pending = std::move(*m_pendingEntity);
    m_pendingEntity.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() || !pending.legacyProjections.empty())) {
      seq::shadow::EntityComparison comparison;
      comparison.rustEventCount = pending.rustEvents.size();
      comparison.legacyEventCount = pending.legacyEvents.size();
      comparison.rustProjectionCount = pending.rustProjections.size();
      comparison.legacyProjectionCount = pending.legacyProjections.size();
      comparison.orderedEventsEqual =
          pending.rustEvents == pending.legacyEvents;
      comparison.projectionsEqual =
          pending.rustProjections.size() == pending.legacyProjections.size();
      if (comparison.projectionsEqual) {
        for (size_t i = 0; i < pending.rustProjections.size(); ++i) {
          if (pending.rustProjections[i].SerializeAsString() !=
              pending.legacyProjections[i].SerializeAsString()) {
            comparison.projectionsEqual = false;
            break;
          }
        }
      }
      if (pending.session)
        pending.session->recordEntityComparison(comparison);
      if (!comparison.orderedEventsEqual || !comparison.projectionsEqual) {
        seqWarn("Rust entity shadow mismatch: events rust=%zu legacy=%zu, "
                "seq.v1 rust=%zu legacy=%zu",
                comparison.rustEventCount, comparison.legacyEventCount,
                comparison.rustProjectionCount,
                comparison.legacyProjectionCount);
      }
    }
  }
  if (m_pendingPlayer) {
    PendingPlayerComparison pending = std::move(*m_pendingPlayer);
    m_pendingPlayer.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() || !pending.legacyProjections.empty())) {
      seq::shadow::PlayerComparison comparison;
      comparison.rustEventCount = pending.rustEvents.size();
      comparison.legacyEventCount = pending.legacyEvents.size();
      comparison.rustProjectionCount = pending.rustProjections.size();
      comparison.legacyProjectionCount = pending.legacyProjections.size();
      comparison.orderedEventsEqual =
          pending.rustEvents == pending.legacyEvents;
      comparison.projectionsEqual =
          pending.rustProjections.size() == pending.legacyProjections.size();
      if (comparison.projectionsEqual) {
        for (size_t i = 0; i < pending.rustProjections.size(); ++i) {
          if (pending.rustProjections[i].SerializeAsString() !=
              pending.legacyProjections[i].SerializeAsString()) {
            comparison.projectionsEqual = false;
            break;
          }
        }
      }
      if (pending.session)
        pending.session->recordPlayerComparison(comparison);
      if (!comparison.orderedEventsEqual || !comparison.projectionsEqual) {
        seqWarn("Rust player shadow mismatch: events rust=%zu legacy=%zu, "
                "seq.v1 rust=%zu legacy=%zu",
                comparison.rustEventCount, comparison.legacyEventCount,
                comparison.rustProjectionCount,
                comparison.legacyProjectionCount);
      }
    }
  }
  if (m_pendingProgression) {
    PendingProgressionComparison pending =
        std::move(*m_pendingProgression);
    m_pendingProgression.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() ||
         !pending.legacyProjections.empty())) {
      seq::shadow::ProgressionComparison comparison;
      comparison.rustEventCount = pending.rustEvents.size();
      comparison.legacyEventCount = pending.legacyEvents.size();
      comparison.rustProjectionCount = pending.rustProjections.size();
      comparison.legacyProjectionCount = pending.legacyProjections.size();
      comparison.orderedEventsEqual =
          pending.rustEvents == pending.legacyEvents;
      comparison.projectionsEqual =
          pending.rustProjections.size() ==
          pending.legacyProjections.size();
      if (comparison.projectionsEqual) {
        for (size_t i = 0; i < pending.rustProjections.size(); ++i) {
          if (pending.rustProjections[i].SerializeAsString() !=
              pending.legacyProjections[i].SerializeAsString()) {
            comparison.projectionsEqual = false;
            break;
          }
        }
      }
      if (pending.session)
        pending.session->recordProgressionComparison(comparison);
      if (!comparison.orderedEventsEqual || !comparison.projectionsEqual) {
        seqWarn("Rust progression shadow mismatch: events rust=%zu legacy=%zu, "
                "seq.v1 rust=%zu legacy=%zu",
                comparison.rustEventCount, comparison.legacyEventCount,
                comparison.rustProjectionCount,
                comparison.legacyProjectionCount);
      }
    }
  }
  if (m_pendingLoot) {
    PendingLootComparison pending = std::move(*m_pendingLoot);
    m_pendingLoot.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() ||
         !pending.legacyProjections.empty())) {
      seq::shadow::LootComparison comparison;
      comparison.rustEventCount = pending.rustEvents.size();
      comparison.legacyEventCount = pending.legacyEvents.size();
      comparison.rustProjectionCount = pending.rustProjections.size();
      comparison.legacyProjectionCount = pending.legacyProjections.size();
      comparison.orderedEventsEqual =
          pending.rustEvents == pending.legacyEvents;
      comparison.projectionsEqual =
          pending.rustProjections.size() ==
          pending.legacyProjections.size();
      if (comparison.projectionsEqual) {
        for (size_t i = 0; i < pending.rustProjections.size(); ++i) {
          if (pending.rustProjections[i].SerializeAsString() !=
              pending.legacyProjections[i].SerializeAsString()) {
            comparison.projectionsEqual = false;
            break;
          }
        }
      }
      if (pending.session)
        pending.session->recordLootComparison(comparison);
      if (!comparison.orderedEventsEqual || !comparison.projectionsEqual) {
        seqWarn("Rust loot shadow mismatch: events rust=%zu legacy=%zu, "
                "seq.v1 rust=%zu legacy=%zu",
                comparison.rustEventCount, comparison.legacyEventCount,
                comparison.rustProjectionCount,
                comparison.legacyProjectionCount);
      }
    }
  }
  if (m_pendingCombat) {
    PendingCombatComparison pending = std::move(*m_pendingCombat);
    m_pendingCombat.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() ||
         !pending.legacyProjections.empty())) {
      seq::shadow::CombatComparison actual;
      actual.rustEventCount = pending.rustEvents.size();
      actual.legacyEventCount = pending.legacyEvents.size();
      actual.rustProjectionCount = pending.rustProjections.size();
      actual.legacyProjectionCount = pending.legacyProjections.size();
      actual.orderedEventsEqual =
          pending.rustEvents == pending.legacyEvents;
      actual.projectionsEqual =
          pending.rustProjections.size() == pending.legacyProjections.size();
      if (actual.projectionsEqual) {
        for (size_t i = 0; i < pending.rustProjections.size(); ++i) {
          if (pending.rustProjections[i].SerializeAsString() !=
              pending.legacyProjections[i].SerializeAsString()) {
            actual.projectionsEqual = false;
            break;
          }
        }
      }
      if (pending.session)
        pending.session->recordCombatComparison(actual);
      if (!actual.orderedEventsEqual || !actual.projectionsEqual) {
        seqWarn("Rust combat shadow mismatch: events rust=%zu legacy=%zu, "
                "seq.v1 rust=%zu legacy=%zu",
                actual.rustEventCount, actual.legacyEventCount,
                actual.rustProjectionCount, actual.legacyProjectionCount);
      }
    }
  }
  if (m_pendingCommunication) {
    PendingCommunicationComparison pending =
        std::move(*m_pendingCommunication);
    m_pendingCommunication.reset();
    if (legacyDispatched &&
        (!pending.rustEvents.empty() || !pending.rustProjections.empty() ||
         !pending.legacyEvents.empty() ||
         !pending.legacyProjections.empty())) {
      seq::shadow::CommunicationComparison actual;
      actual.rustEventCount = pending.rustEvents.size();
      actual.legacyEventCount = pending.legacyEvents.size();
      actual.rustProjectionCount = pending.rustProjections.size();
      actual.legacyProjectionCount = pending.legacyProjections.size();
      actual.orderedEventsEqual = pending.rustEvents == pending.legacyEvents;
      actual.projectionsEqual =
          pending.rustProjections.size() ==
          pending.legacyProjections.size();
      if (actual.projectionsEqual) {
        for (size_t i = 0; i < pending.rustProjections.size(); ++i) {
          if (pending.rustProjections[i].SerializeAsString() !=
              pending.legacyProjections[i].SerializeAsString()) {
            actual.projectionsEqual = false;
            break;
          }
        }
      }
      if (pending.session)
        pending.session->recordCommunicationComparison(actual);
      if (!actual.orderedEventsEqual || !actual.projectionsEqual) {
        seqWarn("Rust communication shadow mismatch: events rust=%zu "
                "legacy=%zu, seq.v1 rust=%zu legacy=%zu",
                actual.rustEventCount, actual.legacyEventCount,
                actual.rustProjectionCount, actual.legacyProjectionCount);
      }
    }
  }
  if (rustLoot && rustLoot->box && rustLoot->batch && m_lootBatchHandler) {
    try {
      m_lootBatchHandler(rustLoot->box, *rustLoot->batch);
    } catch (const std::exception& error) {
      m_lifecycleFatal = true;
      qCritical("Rust loot apply failed for box %s: %s; the immutable "
                "Rust-owned session cannot fall back in place",
                qUtf8Printable(rustLoot->box->box_id), error.what());
      QCoreApplication::exit(EXIT_FAILURE);
    }
  }
}

void EQPacket::flushShadowSession(const Box* box,
                                  seq::shadow::FlushReason reason)
{
  if (!box || m_shadowDisabled.count(box) != 0) return;
  const auto it = m_shadowSessions.find(box);
  if (it == m_shadowSessions.end()) return;
  try {
    const auto& flushed = it->second->flush(reason);
    if (it->second->appliesRustLoot() &&
        !seq::shadow::lootObservations(flushed.batch).empty()) {
      if (!m_lootBatchHandler)
        throw std::runtime_error(
            "Rust loot flush has no attributed host applier");
      m_lootBatchHandler(box, flushed.batch);
    }
    if (it->second->appliesRustCombat() &&
        !seq::shadow::combatObservations(flushed.batch).empty()) {
      if (!m_combatBatchHandler)
        throw std::runtime_error(
            "Rust combat flush has no attributed host applier");
      m_combatBatchHandler(box, flushed.batch);
    }
    if (it->second->appliesRustCommunication() &&
        !seq::shadow::communicationObservations(flushed.batch).empty()) {
      if (!m_communicationEventHandler)
        throw std::runtime_error(
            "Rust communication flush has no attributed host applier");
      for (const auto& event : flushed.batch.events)
        if (seq::shadow::isCommunicationEvent(event))
          m_communicationEventHandler(box, event);
    }
  } catch (const std::exception& error) {
    if (it->second->appliesRustLifecycle() ||
        it->second->appliesRustEntities() ||
        it->second->appliesRustPlayers() ||
        it->second->appliesRustProgression() ||
        it->second->appliesRustLoot() ||
        it->second->appliesRustCombat() ||
        it->second->appliesRustCommunication() ||
        it->second->traceEnabled()) {
      m_lifecycleFatal = true;
      qCritical("Rust lifecycle flush failed for box %s: %s; the immutable "
                "Rust-owned session cannot fall back in place",
                qUtf8Printable(box->box_id), error.what());
      QCoreApplication::exit(EXIT_FAILURE);
      return;
    }
    m_shadowDisabled.insert(box);
    seqWarn("Rust shadow disabled for box %s after flush error: %s",
            qUtf8Printable(box->box_id), error.what());
  }
}

void EQPacket::flushAllShadowSessions(seq::shadow::FlushReason reason)
{
  finalizeAllProvisionalFlows(reason);
  for (const auto& entry : m_shadowSessions)
    flushShadowSession(entry.first, reason);
}

void EQPacket::finalizeApplicationTraces()
{
  finalizeAllProvisionalFlows(seq::shadow::FlushReason::Shutdown);
  for (auto& entry : m_shadowSessions)
    entry.second->finalizeTrace();
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

  // Session handling
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
  const EQPacketFlowKey flowKey = packetFlowKey(packet);
  const uint64_t sourceEndpoint =
      (uint64_t(packet.getIPv4SourceN()) << 16) |
      uint64_t(packet.getSourcePort());
  packet.setFlowKey(flowKey);
  packet.setSourceIsLow(sourceEndpoint == flowKey.endpointLow);
  packet.setAttributionToken(0);

  if (packet.getNetOpCode() == OP_SessionRequest) {
    if (auto prior = m_provisionalPackets.take(flowKey))
      finalizeProvisionalFlow(flowKey, std::move(*prior),
                              seq::shadow::FlushReason::Reset);
    if (m_lifecycleFatal) return;
    m_flowOwners.erase(flowKey);
  }

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
    seqInfo("Client Detected: %s", m_ip.toLatin1().data());
  }
  else if (m_detectingClient && dstIsWorld)
  {
    m_ip = packet.getIPv4SourceA();
    m_client_addr = packet.getIPv4SourceN();
    m_detectingClient = false;
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
      if (!bindShadowFlow(flowKey, box)) return;
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
      if (!bindShadowFlow(flowKey, box)) return;
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

  Box* box = m_boxes.currentBoxFor(m_boxes.activeCharacterId());
  if (!box || box->client_ip != clientAddr) {
    box = nullptr;
    for (const auto& candidate : m_boxes.boxes()) {
      if (candidate->client_ip != clientAddr) continue;
      if (!box || candidate->last_seen_ms > box->last_seen_ms)
        box = candidate.get();
    }
  }
  if (!box) {
    emit ucsChatData(raw, size_t(rawLen), dir, clientAddr);
    return;
  }
  decodeUcsShadow(box, raw, size_t(rawLen), dir);
}

void EQPacket::decodeUcsShadow(Box* box, const uint8_t* payload,
                               size_t payloadSize, uint8_t direction)
{
  const auto found = box ? m_shadowSessions.find(box)
                         : m_shadowSessions.end();
  if (!box || found == m_shadowSessions.end() ||
      m_shadowDisabled.count(box) != 0 || !found->second) {
    emit ucsChatData(payload, payloadSize, direction,
                     box ? box->client_ip : 0);
    return;
  }

  seq::shadow::Session* session = found->second.get();
  m_currentLifecycleSession = session;
  m_currentRustCommunicationKinds.clear();
  try {
    const auto& record = session->decodeUcs(
        direction == DIR_Server
            ? seq::shadow::Direction::ServerToClient
            : seq::shadow::Direction::ClientToServer,
        payload, payloadSize);
    auto observations =
        seq::shadow::communicationObservations(record.batch);
    for (const auto& observation : observations)
      m_currentRustCommunicationKinds.push_back(observation.kind);
    for (auto& observation : observations) observation.payload.clear();

    if (session->comparesCommunication()) {
      PendingCommunicationComparison pending;
      pending.session = session;
      pending.box = box;
      pending.rustEvents = std::move(observations);
      pending.rustProjections = m_communicationProjectionProvider
          ? m_communicationProjectionProvider(box, record.batch)
          : seq::shadow::projectCommunication(record.batch);
      m_pendingCommunication = std::move(pending);
    } else if (!observations.empty() &&
               session->appliesRustCommunication()) {
      if (!m_communicationEventHandler)
        throw std::runtime_error(
            "Rust UCS event has no attributed host applier");
      for (const auto& event : record.batch.events)
        if (seq::shadow::isCommunicationEvent(event))
          m_communicationEventHandler(box, event);
    }

    // Shadow and legacy modes run the old parser for comparison. Rust mode
    // reaches it too, but MessageShell's immutable guard rejects mutation.
    emit ucsChatData(payload, payloadSize, direction, box->client_ip);
    completeShadowApplication(true);
  } catch (const std::exception& error) {
    m_pendingCommunication.reset();
    m_currentLifecycleSession = nullptr;
    m_currentRustCommunicationKinds.clear();
    if (session->appliesRustCommunication()) {
      m_lifecycleFatal = true;
      qCritical("Rust UCS decode/apply failed for box %s: %s",
                qUtf8Printable(box->box_id), error.what());
      QCoreApplication::exit(EXIT_FAILURE);
      return;
    }
    m_shadowDisabled.insert(box);
    seqWarn("Rust UCS shadow disabled for box %s after decode error: %s",
            qUtf8Printable(box->box_id), error.what());
    emit ucsChatData(payload, payloadSize, direction, box->client_ip);
  }
}

////////////////////////////////////////////////////
// Reclaim a non-primary box's owned objects when the registry evicts it.
void EQPacket::onBoxAboutToBeRemoved(Box* box)
{
  if (!box || box->is_primary) return;   // primary aliases the globals

  for (auto owner = m_flowOwners.begin(); owner != m_flowOwners.end();) {
    if (owner->second == box)
      owner = m_flowOwners.erase(owner);
    else
      ++owner;
  }

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
    }
    else
    {
      m_packetCapture->setFilter(m_device.toLatin1().data(),
              m_ip.toLatin1().data(),
              m_realtime,
              IP_ADDRESS_TYPE, 0,
              m_clientPort);
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
// Set the IP address of the client to monitor
void EQPacket::monitorIPClient(const QString& ip)
{
  m_ip = ip;

  validateIP();


  resetEQPacket();

  seqInfo("Listening for IP client: %s", (m_ip == AUTOMATIC_CLIENT_IP) ? "auto-detect" : m_ip.toLatin1().data());
  if (m_playbackPackets == PLAYBACK_OFF)
  {
    m_packetCapture->setFilter(m_device.toLatin1().data(),
            m_ip.toLatin1().data(),
            m_realtime,
            IP_ADDRESS_TYPE, 0, 0);
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

  resetEQPacket();

  seqInfo("Listening for next client seen. (you must zone for this to work!)");

  if (m_playbackPackets == PLAYBACK_OFF)
  {
    m_packetCapture->setFilter(m_device.toLatin1().data(), NULL,
            m_realtime,
            DEFAULT_ADDRESS_TYPE, 0, 0);
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

}

///////////////////////////////////////////
// Reset EQPacket's state
void EQPacket::resetEQPacket()
{
  finalizeAllProvisionalFlows(seq::shadow::FlushReason::Reset);
  m_flowOwners.clear();
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
