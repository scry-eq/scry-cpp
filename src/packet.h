/*
 *  packet.h
 *  Copyright 2000-2024 by the respective ShowEQ Developers
 *  Portions Copyright 2001-2003 Zaphod (dohpaz@users.sourceforge.net).
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

#ifndef _PACKET_H_
#define _PACKET_H_

#include <QHash>
#include <QObject>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <vector>
#include "boxregistry.h"
#include "packetcommon.h"
#include "packetinfo.h"

#if defined (__GLIBC__) && (__GLIBC__ < 2)
#error "Need glibc 2.1.3 or better"
#endif

#if (defined(__FreeBSD__) || defined(__linux__)) && defined(__GLIBC__) && (__GLIBC__ == 2) && (__GLIBC_MINOR__ < 2)
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;
#endif

#include <netinet/in.h>

//----------------------------------------------------------------------
// enumerated types
enum EQStreamPairs
{
  SP_World = 0x01,
  SP_Zone = 0x02
};

//----------------------------------------------------------------------
// forward declarations
class VPacket;
class PacketCaptureProviderThread;
class EQPacketStream;
class EQUDPIPPacketFormat;
class EQPacketTypeDB;
class EQPacketOPCodeDB;
class EQPacketOPCode;

//----------------------------------------------------------------------
// EQPacket
class EQPacket : public QObject
{
   Q_OBJECT 
 public:
   
   // One opcodes.toml serves both DBs — its [[world]] and [[zone]] arrays are
   // separate id namespaces read by separate EQPacketOPCodeDBs.
   EQPacket(const QString& opcodesToml,
	    uint16_t m_arqSeqGiveUp,
	    QString m_device,
	    QString m_agent_target,
	    QString m_ip,
	    QString m_mac_address,
	    bool m_realtime,
        int snaplen,
        int buffersize,
	    bool m_session_tracking,
	    bool m_recordPackets,
	    int m_playbackPackets,
	    int8_t m_playbackSpeed, 
	    QObject *parent,
            const char *name);
   ~EQPacket();           
   void start(int delay = 0);
   void stop(void);

   const QString pcapFilter();
   int packetCount(int);
   const QString& ip();
   const QString& mac();
   const QString& device();
   in_addr_t clientAddr(void);
   in_port_t clientPort(void);
   in_port_t serverPort(void);
   uint8_t session_tracking_enabled(void);
   int playbackPackets(void);
   int playbackSpeed(void);
   size_t currentCacheSize(int);
   uint32_t currentMaxLength(int);
   uint16_t serverSeqExp(int);
   uint16_t arqSeqGiveUp(void);
   bool session_tracking(void);
   bool realtime(void);
   int snaplen(void) { return m_snaplen; }
   int buffersize(void) { return m_buffersize; }
   void setSnapLen(int len) { m_snaplen = len; }
   void setBufferSize(int size) { m_buffersize = size; }

   // Epoch-ms timestamp of the packet currently being dispatched. During
   // --replay this is the *recorded* time (epoch seconds from the .vpk,
   // ×1000) so regenerated timelines match the original capture; 0 in live
   // capture, where consumers fall back to wall-clock.
   qint64 currentPacketTimeMs(void) const { return m_currentPacketTimeMs; }

   // Deterministic "now" for identity/routing state (BoxRegistry timestamps):
   // the packet's recorded time during --replay (so box creation/binding order
   // is reproducible for goldens), wall-clock during live capture. Prefer this
   // over QDateTime::currentMSecsSinceEpoch() anywhere a timestamp influences
   // decode output, not just diagnostics.
   qint64 nowMs(void) const;

   // Total opcodes flagged by the backend gate-size audit at opcode-DB load
   // (world + zone). Non-zero means a mapped SZC_Match opcode still gates on
   // an inherited Live sizeof; --strict-gate-sizes turns that into a fatal.
   int undeclaredGateSizeCount(void) const { return m_undeclaredGateSizes; }

   void exportHandoffState(const QString& configDir) const;
   bool importHandoffState(const QString& configDir);

 public slots:
   void processPackets(void);
   void processPlaybackPackets(void);
   void incPlayback(void);
   void decPlayback(void);
   void setPlayback(int);
   void monitorIPClient(const QString& address);   
   void monitorMACClient(const QString& address);   
   void monitorNextClient();   
   void monitorDevice(const QString& dev);   
   void session_tracking(bool enable);
   void setArqSeqGiveUp(uint16_t giveUp);
   void setRealtime(bool val);
   void dispatchSessionKey(uint32_t sessionId, EQStreamID streamid,
      uint32_t sessionKey);

 protected slots:
   void closeStream(uint32_t sessionId, EQStreamID streamId);
   void unlatchClientPort();
   void lockOnClient(in_port_t serverPort, in_port_t clientPort, in_addr_t clientAddr);
   // BoxRegistry::boxAboutToBeRemoved handler. Tears down the per-box
   // streams + observers this EQPacket owns for the evicted box (the
   // reverse of the BoxCreatedHook allocation). No-op for the primary box,
   // whose streams alias the globals and which is never evicted.
   void onBoxAboutToBeRemoved(Box* box);

 signals:
   // Emitted exactly once when a --replay session reaches end-of-file.
   // Wired by DaemonApp to QCoreApplication::quit() in record-golden
   // mode so `--replay X.vpk --record-golden Y.pbstream` exits cleanly
   // instead of hanging in the event loop after EOF.
   void playbackFinished();

   // used for net_stats display
   void cacheSize(int, int);
   void seqReceive(int, int);
   void seqExpect(int, int);
   void numPacket(int, int);
   void maxLength(int, int);
   void resetPacket(int, int);
   void playbackSpeedChanged(int);
   void clientChanged(in_addr_t);
   void clientPortLatched(in_port_t);
   void serverPortLatched(in_port_t);
   void sessionTrackingChanged(uint8_t);
   void toggle_session_tracking(bool);
   void filterChanged(void);
   void stsMessage(const QString &, int = 0);

   // new logging
   void newPacket(const EQUDPIPPacketFormat& packet);
   void rawWorldPacket(const uint8_t* data, size_t len, uint8_t dir, 
		       uint16_t opcode);
   void decodedWorldPacket(const uint8_t* data, size_t len, uint8_t dir,
			   uint16_t opcode, const EQPacketOPCode* opcodeEntry);
   void decodedWorldPacket(const uint8_t* data, size_t len, uint8_t dir,
			   uint16_t opcode, const EQPacketOPCode* opcodeEntry,
               bool unknown);
   // EQ Legends UCS (cross-zone chat): one raw server->client port-9877 UDP
   // payload + the owning client's addr (for the per-client channel-mask
   // cache), forwarded to MessageShell::ucsChatMessage for Rust decode.
   void ucsChatData(const uint8_t* data, size_t len, uint8_t dir,
                    in_addr_t clientAddr);

   void rawZonePacket(const uint8_t* data, size_t len, uint8_t dir,
		      uint16_t opcode);
   void decodedZonePacket(const uint8_t* data, size_t len, uint8_t dir,
			  uint16_t opcode, const EQPacketOPCode* opcodeEntry);
   void decodedZonePacket(const uint8_t* data, size_t len, uint8_t dir,
			  uint16_t opcode, const EQPacketOPCode* opcodeEntry,
			  bool unknown);

 private:
   void validateIP();

   PacketCaptureProviderThread* m_packetCapture;
   VPacket* m_vPacket;
   QTimer* m_timer;

   in_port_t m_serverPort;
   in_port_t m_clientPort;
   bool m_busy_decoding;
   bool m_detectingClient;
   in_addr_t m_client_addr;
   qint64 m_currentPacketTimeMs = 0;
   int m_undeclaredGateSizes = 0;

   // Zone 5-tuples already warned about as unbound (see dispatchPacket). One
   // warning per tuple, capped — an unbindable session emits thousands of
   // packets and the point is a pointer, not a flood.
   QSet<quint64> m_unboundZoneWarned;
   static constexpr int kMaxUnboundZoneWarnings = 4;

   // How recently a box must have been world-active for the no-announced-port
   // zone binding fallback to claim its SessionRequest. Generous: a client
   // opens the zone socket within ~300ms of the world handshake, and the only
   // competing candidates are same-host boxes.
   static constexpr qint64 kZoneBindWindowMs = 15000;

   uint16_t m_arqSeqGiveUp;
   QString m_device;
   QString m_agent;      // scry-agent "host:port" (remote capture source), or empty
   QString m_ip;
   QString m_mac;
   bool m_realtime;
   int m_snaplen;
   int m_buffersize;
   bool m_session_tracking;
   bool m_recordPackets;
   int m_playbackPackets;
   int8_t m_playbackSpeed; // Should be signed since -1 is pause

   EQPacketStream* m_client2WorldStream;
   EQPacketStream* m_world2ClientStream;
   EQPacketStream* m_client2ZoneStream;
   EQPacketStream* m_zone2ClientStream;
   EQPacketStream* m_streams[MAXSTREAMS];

   EQPacketTypeDB* m_packetTypeDB;
   EQPacketOPCodeDB* m_worldOPCodeDB;
   EQPacketOPCodeDB* m_zoneOPCodeDB;

   // Stage 1 of multibox-sessions: observe every world-port-talking
   // client_ip on the wire. Read-only sibling of the legacy
   // m_detectingClient single-shot auto-detect. See
   // docs/MULTIBOX_PLAN.md.
   BoxRegistry m_boxes;

   // Per-box parent QObject for every non-primary Box's owned objects (its
   // four EQPacketStreams + ZoneServerObserver + NamePromoter). Deleting
   // the root cascade-deletes the whole subtree and unwinds its signal
   // connections in one shot, so eviction teardown is order-safe. Keyed by
   // the stable Box* (box_id mutates on promotion). The primary box owns no
   // root — its streams are the globals.
   QHash<const Box*, QObject*> m_boxRoots;

 public:
   const BoxRegistry& boxRegistry() const { return m_boxes; }
   BoxRegistry&       boxRegistry()       { return m_boxes; }

   // The four global decode streams. The primary box aliases these (see
   // BoxRegistry's BoxCreatedHook), so DaemonApp wires the active
   // ManagerSet onto them at startup via wireBoxPipeline(). Non-primary
   // boxes own their own streams (Box::{world,zone}_{c2s,s2c}).
   EQPacketStream* worldClientStream() const { return m_client2WorldStream; }
   EQPacketStream* worldServerStream() const { return m_world2ClientStream; }
   EQPacketStream* zoneClientStream()  const { return m_client2ZoneStream; }
   EQPacketStream* zoneServerStream()  const { return m_zone2ClientStream; }

   void connectStream(EQPacketStream* stream);
   // --only-session: detach the four global (primary-box) streams from the
   // recon broadcast signals (decodedZonePacket / decodedWorldPacket) so the
   // recon taps (--dump-payload / --opcode-stats / --list-events) stop seeing
   // the primary box by default; DaemonApp re-relays the selected session
   // instead. The typed manager dispatch is per-stream and unaffected.
   void disconnectReconTaps();
   void dispatchPacket   (int size, unsigned char *buffer);
   void dispatchPacket(EQUDPIPPacketFormat& packet);
   // EQ Legends UCS: forward a raw port-9877 chat payload to MessageShell.
   void decodeUCSPacket(EQUDPIPPacketFormat& packet);
 protected slots:
   void resetEQPacket();
   void dispatchWorldChatData (size_t len, uint8_t* data, uint8_t direction = 0);
};

inline in_addr_t EQPacket::clientAddr(void)
{
   return m_client_addr;
}

inline in_port_t EQPacket::clientPort(void)
{
  return m_clientPort;
}

inline in_port_t EQPacket::serverPort(void)
{
  return m_serverPort;
}

inline uint16_t EQPacket::arqSeqGiveUp(void)
{
  return m_arqSeqGiveUp;
}

inline bool EQPacket::session_tracking(void)
{
  return m_session_tracking;
}

inline int EQPacket::playbackPackets(void)
{
  return m_playbackPackets;
}

inline bool EQPacket::realtime(void)
{
  return m_realtime;
}

inline const QString& EQPacket::ip()
{
  return m_ip;
}

inline const QString& EQPacket::mac()
{
  return m_mac;
}

inline const QString& EQPacket::device()
{
  return m_device;
}

#endif // _PACKET_H_
