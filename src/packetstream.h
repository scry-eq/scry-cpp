/*
 *  packetstream.h
 *  Copyright 2000-2005, 2019 by the respective ShowEQ Developers
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

#ifndef _PACKETSTREAM_H_
#define _PACKETSTREAM_H_

#include <QObject>
#include <QHash>
#include <QSet>
#include <functional>
#include <map>
#include <memory>
#include <utility>

#include "packetcommon.h"
#include "packetfragment.h"
#include "packetformat.h"
#include "packetinfo.h"

#if (defined(__FreeBSD__) || defined(__linux__)) && defined(__GLIBC__) && (__GLIBC__ == 2) && (__GLIBC_MINOR__ < 2)
typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;
#endif

#include <netinet/in.h>

class EQUDPIPPacketFormat;
class EQProtocolPacket;
class EQPacketOPCodeDB;
class EQPacketOPCode;

//----------------------------------------------------------------------
// map type used for caching packets.
// The reason an STL Map was chosen is because of it's consitent 
// O(log N) behavior, and the key based ordering it enforces
// is convenient for processing.  The Qt hash based collections really
// arent' appropriate for the packet cache for multiple reasons:
// 1) They're iterators are based on key hash order and not key value
// ordering, making certain algorithms more difficult and potentially 
// time consuming. 2) insertions into an STL map can be optimized 
// with the packet data's behavior using an iterator as a hint for 
// insertion location to typically yield amortized constant time behavior.
// 3) Another optimization possible with this data set using a map 
// is that after a matching arq is found in the map,  finding/checking 
// for the next expected arq in the map only requires moving the iterator 
// forward (using operator++()) once and checking if the next key in the list
// is the expected arq.  This results in the check for followers to only 
// taking amortized constant time (as opposed to the O(log N) of map::find()
// or constant average time of the hash find methods.
typedef std::map<uint16_t, EQProtocolPacket* > EQPacketMap;

//----------------------------------------------------------------------
// EQPacketStream
class EQPacketStream : public QObject
{
  Q_OBJECT

 public:
  EQPacketStream(EQStreamID streamid, uint8_t dir, uint16_t m_arqSeqGiveUp,
		 EQPacketOPCodeDB& opcodeDB, 
		 QObject* parent = 0, const char* name = 0);
  ~EQPacketStream();
  void reset();
  uint8_t sessionTracking();
  void setSessionTracking(uint8_t);
  uint16_t arqSeqGiveUp();
  void setArqSeqGiveUp(uint16_t);
  int packetCount(void);
  uint8_t dir();
  EQStreamID streamID();
  size_t currentCacheSize();
  uint16_t arqSeqExp();
  // Register a PacketHandler for the opcode+payload+szt (typed dispatch, no Qt
  // SLOT). The handler is appended to the payload's dispatcher and fired in
  // registration order (goldens depend on it).
  bool on(const QString& opcodeName,
	  const char* payload, EQSizeCheckType szt,
	  PacketHandler handler);
  void receiveSessionKey(uint32_t sessionId, EQStreamID streamid, 
    uint32_t sessionKey);
  void close(uint32_t sessionId, EQStreamID streamid, uint8_t sessionTracking);
  uint16_t calculateCRC(EQProtocolPacket& packet);
  uint32_t getSessionKey() const { return m_sessionKey; }
  uint32_t getMaxLength() const { return m_maxLength; }

  // Multibox active-box gate (Stage 3b of MULTIBOX_PLAN.md). When
  // muted, dispatchPacket() still emits the 5-arg decodedPacket
  // signal (so per-box observers like NamePromoter / ZoneServerObserver
  // keep firing) but skips the on()-wired EQPacketDispatch
  // activations and the 6-arg "with unknown flag" signal — so the
  // singleton state managers don't get duplicate hits from non-active
  // boxes. The session-key handshake and ARQ/fragment reassembly run
  // unmuted so flipping the gate later doesn't require a re-handshake.
  void setMuted(bool m) { m_muted = m; }
  bool isMuted() const  { return m_muted; }

  // Runs once for each reassembled application packet, before decodedPacket
  // observers and legacy handlers. Shadow decoding uses this path so muted
  // boxes receive the same ordered packet stream as active boxes.
  using ApplicationPacketHook =
      std::function<bool(EQStreamID, uint8_t, uint16_t,
                         const uint8_t*, size_t, int64_t,
                         EQPacketFlowKey, uintptr_t)>;
  using ApplicationPacketCompleteHook = std::function<void(bool)>;
  using TimestampProvider = std::function<int64_t()>;
  void setApplicationPacketHook(ApplicationPacketHook hook,
                                TimestampProvider timestampProvider,
                                ApplicationPacketCompleteHook completeHook = {})
  {
    m_applicationPacketHook = std::move(hook);
    m_timestampProvider = std::move(timestampProvider);
    m_applicationPacketCompleteHook = std::move(completeHook);
  }

  struct StreamHandoff {
    uint32_t sessionId;
    uint32_t sessionKey;
    uint32_t clientIP;
    uint16_t clientPort;
    uint16_t _pad;
    uint32_t maxLength;
    uint16_t arqSeqExp;
    uint16_t _pad2;
    int64_t  decodeKey;
    uint8_t  validKey;
    uint8_t  _pad3[7];
  };
  StreamHandoff exportState() const;
  void importState(const StreamHandoff& s);

 public slots:
  void handlePacket(EQUDPIPPacketFormat& pf);
  
 signals:
  void rawPacket(const uint8_t* data, size_t len, uint8_t dir, 
		 uint16_t opcode);
  void decodedPacket(const uint8_t* data, size_t len, uint8_t dir,
		     uint16_t opcode, const EQPacketOPCode* opcodeEntry);
  void decodedPacket(const uint8_t* data, size_t len, uint8_t dir,
		     uint16_t opcode, const EQPacketOPCode* opcodeEntry,
		     bool unknown);

  // this signals stream closure
  void closing(uint32_t sessionId, EQStreamID streamId);

  // this signals a change in the session tracking state
  void sessionTrackingChanged(uint8_t);
  void lockOnClient(in_port_t serverPort, in_port_t clientPort, in_addr_t clientAddr);

  // Signal a new session key being received
  void sessionKey(uint32_t sessionId, EQStreamID streadid, uint32_t sessionKey);
		    
  // used for net_stats display
  void cacheSize(int, int);
  void seqReceive(int, int);
  void seqExpect(int, int);
  void numPacket(int, int);
  void resetPacket(int, int);
  void maxLength(int, int);

 protected:
  void resetCache();
  void setCache(uint16_t serverArqSeq, EQProtocolPacket& packet);
  void processCache();
  void processPacket(EQProtocolPacket& packet, bool subpacket);
  void dispatchPacket(const uint8_t* data, size_t len,
			      uint16_t opCode, const EQPacketOPCode* opcodeEntry);
  void dispatchPacketAt(const uint8_t* data, size_t len,
			        uint16_t opCode, const EQPacketOPCode* opcodeEntry,
                        int64_t captureTimeMs, EQPacketFlowKey flowKey,
                        uintptr_t attributionToken);
  // Lookup for on(): resolve opcode+payload+szt to its
  // (lazily created) dispatcher, or nullptr with a diagnostic on miss.
  EQPacketDispatch* dispatchFor(const QString& opcodeName,
				const char* payloadType, EQSizeCheckType szt);


  EQPacketOPCodeDB& m_opcodeDB;
  QHash<void*, EQPacketDispatch*> m_dispatchers;
  EQStreamID m_streamid;
  uint8_t m_dir;
  int m_packetCount;
  uint8_t m_session_tracking_enabled;
  bool m_muted = false;
  ApplicationPacketHook m_applicationPacketHook;
  ApplicationPacketCompleteHook m_applicationPacketCompleteHook;
  TimestampProvider m_timestampProvider;

  // Payload-size-mismatch warnings, deduped on (opcode, length). A gated
  // opcode mismatches on EVERY packet, so warning per-packet buries the log
  // (thousands of lines for one position opcode). The fact is what matters,
  // not the count — report each distinct pair once per stream.
  QSet<quint64> m_sizeMismatchWarned;

  // ARQ cache handling
  EQPacketMap m_cache;
  size_t m_maxCacheCount;
  uint16_t m_arqSeqExp;
  uint16_t m_arqSeqGiveUp;
  bool m_arqSeqFound;
  
  // Fragment handling
  EQPacketFragmentSequence m_fragment;

  // Session info
  uint32_t m_sessionId;
  uint32_t m_sessionKey;
  in_port_t m_sessionClientPort;
  in_addr_t m_sessionClientIP;
  uint32_t m_maxLength;

  // encryption
  int64_t m_decodeKey;
  bool m_validKey;
};

inline uint8_t EQPacketStream::sessionTracking()
{
  return m_session_tracking_enabled;
}

inline void EQPacketStream::setSessionTracking(uint8_t val)
{
  m_session_tracking_enabled = val;
}

inline uint16_t EQPacketStream::arqSeqGiveUp()
{
  return m_arqSeqGiveUp;
}

inline void EQPacketStream::setArqSeqGiveUp(uint16_t val)
{
  m_arqSeqGiveUp = val;
}

inline int EQPacketStream::packetCount(void)
{
  return m_packetCount;
}

inline uint8_t EQPacketStream::dir()
{
  return m_dir;
}

inline EQStreamID EQPacketStream::streamID()
{
  return m_streamid;
}

inline size_t EQPacketStream::currentCacheSize()
{
  return m_cache.size();
}

inline uint16_t EQPacketStream::arqSeqExp()
{
  return m_arqSeqExp;
}

//----------------------------------------------------------------------
// seqBind — adapt a manager member function into a uniform 3-arg
// PacketHandler. Handlers historically took 1, 2, or 3 args (Qt dropped the
// extra signal args when connecting a shorter slot); these overloads
// reproduce that by ignoring the unused trailing args. Replaces the old
// SLOT("method(...)") string in the wiring tables.
template <class T>
inline PacketHandler seqBind(T* obj, void (T::*m)(const uint8_t*))
{
  return [obj, m](const uint8_t* d, size_t, uint8_t) { (obj->*m)(d); };
}
template <class T>
inline PacketHandler seqBind(T* obj, void (T::*m)(const uint8_t*, size_t))
{
  return [obj, m](const uint8_t* d, size_t n, uint8_t) { (obj->*m)(d, n); };
}
template <class T>
inline PacketHandler seqBind(T* obj,
			     void (T::*m)(const uint8_t*, size_t, uint8_t))
{
  return [obj, m](const uint8_t* d, size_t n, uint8_t dir) { (obj->*m)(d, n, dir); };
}

// shared_ptr overloads — for a backend adapter (e.g. EqlDispatch) with no
// QObject parent to own it. The captured shared_ptr keeps the adapter alive as
// long as any wired dispatcher references the handler, so its lifetime tracks
// the box's stream dispatchers rather than a QObject parent.
template <class T>
inline PacketHandler seqBind(std::shared_ptr<T> obj, void (T::*m)(const uint8_t*))
{
  return [obj = std::move(obj), m](const uint8_t* d, size_t, uint8_t) { ((obj.get())->*m)(d); };
}
template <class T>
inline PacketHandler seqBind(std::shared_ptr<T> obj, void (T::*m)(const uint8_t*, size_t))
{
  return [obj = std::move(obj), m](const uint8_t* d, size_t n, uint8_t) { ((obj.get())->*m)(d, n); };
}
template <class T>
inline PacketHandler seqBind(std::shared_ptr<T> obj,
			     void (T::*m)(const uint8_t*, size_t, uint8_t))
{
  return [obj = std::move(obj), m](const uint8_t* d, size_t n, uint8_t dir) { ((obj.get())->*m)(d, n, dir); };
}

#endif //  _PACKETSTREAM_H_
