/*
 *  remotecapture.h
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

#ifndef _REMOTECAPTURE_H_
#define _REMOTECAPTURE_H_

#include <cstdint>
#include <atomic>
#include <vector>

#include <QString>

#include "packetcaptureprovider.h"

//----------------------------------------------------------------------
// RemoteCaptureThread
//
// A capture source that sources frames from a remote `seq-agent` over TCP
// instead of a local libpcap device — the C++ analogue of scry's RemoteCapture.
// The agent listens; the daemon dials in, sends a SEQA ClientHello naming the
// BPF filter it wants captured, reads the agent's Hello (link type / snaplen /
// applied filter), then reads timestamped raw Ethernet frames and pushes them
// into the same packetCache the decode loop drains — so downstream processing is
// identical to local pcap capture. A dedicated reader thread auto-reconnects
// with backoff, so the daemon survives an agent restart.
//
// This is a live-only source: no offline playback / speed control (the agent, or
// a `.pcap`/`.vpk` file, is the replay frontend — see PacketCaptureThread).
class RemoteCaptureThread : public PacketCaptureProviderThread
{
    public:
        // `agentTarget` is "host:port" (defaults to port 9099 if none given).
        explicit RemoteCaptureThread(const QString& agentTarget);
        ~RemoteCaptureThread();

        bool offlinePlaybackSupported() { return false; }
        void startOffline(const char* filename, int playbackSpeed);
        void setPlaybackSpeed(int) {}
        int getPlaybackSpeed() { return 0; }

        // `device` is ignored (frames come from the agent). `hostname`/
        // `address_type` scope the BPF filter requested from the agent, exactly
        // as for local capture.
        void start(const char* device, const char* host, bool realtime, uint8_t address_type);
        void stop();

        void setFilter(const char* device, const char* hostname, bool realtime,
                uint8_t address_type, uint16_t zone_server_port, uint16_t client_port);
        const QString getFilter();

    private:
        static void* loop(void* param);
        void run();                              // connect → handshake → pump, with backoff
        int  connectToAgent();                   // returns socket fd, or -1
        bool sendClientHello(int fd);            // ClientHello filter request
        bool readMsg(int fd, uint8_t& type, std::vector<uint8_t>& payload);   // one envelope
        bool readHello(int fd);                  // SEQA link_type / snaplen / filter
        uint64_t pumpFrames(int fd);             // read frames → packetCache until EOF/error
        bool readFull(int fd, void* buf, size_t n);
        void backoffSleep(int ms);               // interruptible by m_stop

        QString m_host;
        uint16_t m_port;

        QString m_filter;                        // BPF string requested from the agent
        int m_linkType;                          // DLT reported by the agent's Hello
        std::atomic<int> m_sock;                 // connected socket fd, or -1
        std::atomic<bool> m_stop;                // set by stop() to break the loop
        bool m_started;                          // true once the reader thread exists
};

#endif // _REMOTECAPTURE_H_
