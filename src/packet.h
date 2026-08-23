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
#include <QTimer>
#include <memory>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "boxregistry.h"
#include "packetcommon.h"
#include "packetformat.h"
#include "packetinfo.h"
#include "provisionalflow.h"
#include "rustsession.h"

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
	    seq::shadow::LifecycleSelector lifecycleSelector,
	    seq::shadow::EntitySelector entitySelector,
	    seq::shadow::PlayerSelector playerSelector,
	    seq::shadow::ProgressionSelector progressionSelector,
	    seq::shadow::LootSelector lootSelector,
	    seq::shadow::CombatSelector combatSelector,
	    seq::shadow::CommunicationSelector communicationSelector,
	    QString applicationTraceDir,
	    QObject *parent,
            const char *name);
   ~EQPacket();           
   void start(int delay = 0);
   void stop(void);

   int packetCount(int);
   int playbackSpeed(void);

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

   // Stateful Rust sessions consume every application packet. Their immutable
   // lifecycle selector decides whether legacy handlers mutate, shadow output
   // is compared, or typed Rust lifecycle events mutate host state.
   void flushShadowSession(const Box* box, seq::shadow::FlushReason reason);
   void flushAllShadowSessions(seq::shadow::FlushReason reason);
   void finalizeApplicationTraces();
   using LifecycleEventHandler =
       std::function<void(const Box*, const seq::shadow::Event&)>;
   using EntityEventHandler = LifecycleEventHandler;
   using PlayerEventHandler = LifecycleEventHandler;
   using ProgressionBatchHandler =
       std::function<void(const Box*, const seq::shadow::Batch&)>;
   using LootBatchHandler =
       std::function<void(const Box*, const seq::shadow::Batch&)>;
   using CombatBatchHandler = LootBatchHandler;
   using CommunicationEventHandler =
       std::function<void(const Box*, const seq::shadow::Event&)>;
   using CommunicationProjectionProvider =
       std::function<std::vector<seq::v1::Envelope>(
           const Box*, const seq::shadow::Batch&)>;
   using LifecycleProjectionEnricher =
       std::function<void(const Box*, bool,
                          std::vector<seq::shadow::LifecycleObservation>&,
                          std::vector<seq::v1::Envelope>&)>;
   using LifecycleGlobalOwnershipPredicate =
       std::function<bool(const Box*)>;
   void setLifecycleEventHandler(LifecycleEventHandler handler)
   { m_lifecycleEventHandler = std::move(handler); }
   void setEntityEventHandler(EntityEventHandler handler)
   { m_entityEventHandler = std::move(handler); }
   void setPlayerEventHandler(PlayerEventHandler handler)
   { m_playerEventHandler = std::move(handler); }
   void setProgressionBatchHandler(ProgressionBatchHandler handler)
   { m_progressionBatchHandler = std::move(handler); }
   void setLootBatchHandler(LootBatchHandler handler)
   { m_lootBatchHandler = std::move(handler); }
   void setCombatBatchHandler(CombatBatchHandler handler)
   { m_combatBatchHandler = std::move(handler); }
   void setCommunicationEventHandler(CommunicationEventHandler handler)
   { m_communicationEventHandler = std::move(handler); }
   void setCommunicationProjectionProvider(
       CommunicationProjectionProvider provider)
   { m_communicationProjectionProvider = std::move(provider); }
   void setLifecycleProjectionEnricher(LifecycleProjectionEnricher enricher)
   { m_lifecycleProjectionEnricher = std::move(enricher); }
   void setLifecycleGlobalOwnershipPredicate(
       LifecycleGlobalOwnershipPredicate predicate)
   { m_lifecycleGlobalOwnershipPredicate = std::move(predicate); }
   bool legacyLifecycleEnabledForCurrentPacket() const;
   bool rustLifecycleAcceptedForCurrentPacket(
       seq::shadow::LifecycleKind kind) const;
   bool legacyEntitiesEnabledForCurrentPacket() const;
   bool rustEntityAcceptedForCurrentPacket(seq::shadow::EntityKind kind) const;
   bool legacyPlayersEnabledForCurrentPacket() const;
   bool legacyPlayerAppearanceEnabledForCurrentPacket() const;
   bool rustPlayerAcceptedForCurrentPacket(seq::shadow::PlayerKind kind) const;
   bool legacyProgressionEnabledForCurrentPacket() const;
   bool rustProgressionAcceptedForCurrentPacket(
       seq::shadow::ProgressionKind kind) const;
   bool legacyLootEnabledForCurrentPacket() const;
   bool rustLootAcceptedForCurrentPacket(seq::shadow::LootKind kind) const;
   bool legacyCombatEnabledForCurrentPacket() const;
   bool rustCombatAcceptedForCurrentPacket(seq::shadow::CombatKind kind) const;
   bool legacyCommunicationEnabledForCurrentPacket() const;
   bool rustCommunicationAcceptedForCurrentPacket(
       seq::shadow::CommunicationKind kind) const;
   void observeLegacyLoot(seq::shadow::LootObservation observation);
   void observeLegacyLootProjection(seq::v1::Envelope envelope);
   void observeLegacyCombat(seq::shadow::CombatObservation observation);
   void observeLegacyCombatProjection(seq::v1::Envelope envelope);
   void observeLegacyCommunication(
       seq::shadow::CommunicationObservation observation);
   void observeLegacyCommunicationProjection(seq::v1::Envelope envelope);
   void observeLegacyLifecycle(seq::shadow::LifecycleObservation observation);
   void observeLegacyLifecycleProjection(seq::v1::Envelope envelope);
   void observeLegacyEntity(seq::shadow::EntityObservation observation);
   void observeLegacyEntityProjection(seq::v1::Envelope envelope);
   void observeLegacyPlayer(seq::shadow::PlayerObservation observation);
   void observeLegacyPlayerProjection(seq::v1::Envelope envelope);
   void observeLegacyProgression(
       seq::shadow::ProgressionObservation observation);
   void observeLegacyProgressionProjection(seq::v1::Envelope envelope);
   void applyValidatedZoneServerInfo(Box* box, uint16_t port);

   void exportHandoffState(const QString& configDir) const;
   bool importHandoffState(const QString& configDir);

 public slots:
   void processPackets(void);
   void processPlaybackPackets(void);
   void incPlayback(void);
   void decPlayback(void);
   void setPlayback(int);
   void monitorIPClient(const QString& address);   
   void monitorNextClient();   
   void monitorDevice(const QString& dev);   
   void session_tracking(bool enable);
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
   const seq::shadow::LifecycleSelector m_lifecycleSelector;
   const seq::shadow::EntitySelector m_entitySelector;
   const seq::shadow::PlayerSelector m_playerSelector;
   const seq::shadow::ProgressionSelector m_progressionSelector;
   const seq::shadow::LootSelector m_lootSelector;
   const seq::shadow::CombatSelector m_combatSelector;
   const seq::shadow::CommunicationSelector m_communicationSelector;
   QString m_applicationTracePrefix;
   QString m_applicationTraceCatalogHash;
   uint64_t m_applicationTraceSession = 0;

   EQPacketStream* m_client2WorldStream;
   EQPacketStream* m_world2ClientStream;
   EQPacketStream* m_client2ZoneStream;
   EQPacketStream* m_zone2ClientStream;
   EQPacketStream* m_streams[MAXSTREAMS];

   EQPacketTypeDB* m_packetTypeDB;
   EQPacketOPCodeDB* m_worldOPCodeDB;
   EQPacketOPCodeDB* m_zoneOPCodeDB;

   // One immutable protocol registry for the process, then one stateful Rust
   // session per Box. Each Session owns its lifecycle selector for its entire
   // lifetime. Changing ownership requires a new session.
   std::unique_ptr<seq::shadow::ProtocolRegistry> m_shadowRegistry;
   std::unordered_map<const Box*, std::unique_ptr<seq::shadow::Session>>
       m_shadowSessions;
   std::unordered_set<const Box*> m_shadowDisabled;
   struct ProvisionalShadowSession {
       std::unique_ptr<seq::shadow::Session> session;
       bool disabled = false;
   };
   std::map<EQPacketFlowKey, ProvisionalShadowSession> m_provisionalShadowSessions;
   seq::shadow::ProvisionalPacketStore m_provisionalPackets;
   std::map<EQPacketFlowKey, Box*> m_flowOwners;
   uint64_t m_applicationDispatchOrder = 0;
   struct PendingLifecycleComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::LifecycleObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::LifecycleObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
       bool expectsHostZoneProjection = false;
   };
   std::optional<PendingLifecycleComparison> m_pendingLifecycle;
   struct PendingEntityComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::EntityObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::EntityObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
   };
   std::optional<PendingEntityComparison> m_pendingEntity;
   struct PendingPlayerComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::PlayerObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::PlayerObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
   };
   std::optional<PendingPlayerComparison> m_pendingPlayer;
   struct PendingProgressionComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::ProgressionObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::ProgressionObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
   };
   std::optional<PendingProgressionComparison> m_pendingProgression;
   struct PendingLootComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::LootObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::LootObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
   };
   std::optional<PendingLootComparison> m_pendingLoot;
   struct PendingCombatComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::CombatObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::CombatObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
   };
   std::optional<PendingCombatComparison> m_pendingCombat;
   struct PendingCommunicationComparison {
       seq::shadow::Session* session = nullptr;
       const Box* box = nullptr;
       std::vector<seq::shadow::CommunicationObservation> rustEvents;
       std::vector<seq::v1::Envelope> rustProjections;
       std::vector<seq::shadow::CommunicationObservation> legacyEvents;
       std::vector<seq::v1::Envelope> legacyProjections;
   };
   std::optional<PendingCommunicationComparison> m_pendingCommunication;
   struct PendingRustLootApplication {
       const Box* box = nullptr;
       const seq::shadow::Batch* batch = nullptr;
   };
   std::optional<PendingRustLootApplication> m_pendingRustLoot;
   seq::shadow::Session* m_currentLifecycleSession = nullptr;
   std::vector<seq::shadow::LifecycleKind> m_currentRustLifecycleKinds;
   std::vector<seq::shadow::EntityKind> m_currentRustEntityKinds;
   std::vector<seq::shadow::PlayerKind> m_currentRustPlayerKinds;
   std::vector<seq::shadow::ProgressionKind> m_currentRustProgressionKinds;
   std::vector<seq::shadow::LootKind> m_currentRustLootKinds;
   std::vector<seq::shadow::CombatKind> m_currentRustCombatKinds;
   std::vector<seq::shadow::CommunicationKind>
       m_currentRustCommunicationKinds;
   bool m_currentRustPacketDecoded = false;
   LifecycleEventHandler m_lifecycleEventHandler;
   EntityEventHandler m_entityEventHandler;
   PlayerEventHandler m_playerEventHandler;
   ProgressionBatchHandler m_progressionBatchHandler;
   LootBatchHandler m_lootBatchHandler;
   CombatBatchHandler m_combatBatchHandler;
   CommunicationEventHandler m_communicationEventHandler;
   CommunicationProjectionProvider m_communicationProjectionProvider;
   LifecycleProjectionEnricher m_lifecycleProjectionEnricher;
   LifecycleGlobalOwnershipPredicate m_lifecycleGlobalOwnershipPredicate;
   bool m_lifecycleFatal = false;

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
   void decodeUcsShadow(Box* box, const uint8_t* payload,
                        size_t payloadSize, uint8_t direction);
   void installShadowHook(EQPacketStream* stream);
   bool decodeShadowApplication(EQStreamID stream, uint8_t direction,
                                uint16_t opcode, const uint8_t* payload,
                                size_t payloadSize, int64_t timestamp,
                                EQPacketFlowKey flowKey, bool sourceIsLow,
                                uintptr_t attributionToken);
   void completeShadowApplication(bool legacyDispatched);
   seq::shadow::Session* provisionalShadowSession(EQPacketFlowKey flowKey);
   bool bindShadowFlow(EQPacketFlowKey flowKey, Box* box);
   bool replayProvisionalFlow(EQPacketFlowKey flowKey, Box* box,
                              seq::shadow::ProvisionalPacketFlow flow);
   void finalizeProvisionalFlow(EQPacketFlowKey flowKey,
                                seq::shadow::ProvisionalPacketFlow flow,
                                seq::shadow::FlushReason reason);
   void finalizeAllProvisionalFlows(seq::shadow::FlushReason reason);
   void writeProvisionalTrace(
       const seq::shadow::ProvisionalPacketFlow& flow);
   bool rustOwnsAnyFamily() const;
   std::unique_ptr<seq::shadow::ApplicationTraceWriter>
       makeApplicationTraceWriter();
 protected slots:
   void resetEQPacket();
   void dispatchWorldChatData (size_t len, uint8_t* data, uint8_t direction = 0);
};

#endif // _PACKET_H_
