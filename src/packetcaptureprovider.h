/*
 *  packetcaptureprovider.h
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

#ifndef _PACKETCAPTUREPROVIDERTHREAD_H_
#define _PACKETCAPTUREPROVIDERTHREAD_H_

#include <cstdint>

#include <pthread.h>

class PacketCaptureProviderThread
{
    public:
        PacketCaptureProviderThread();
        virtual ~PacketCaptureProviderThread();

        virtual void startOffline(const char* filename, int playbackSpeed) = 0;
        virtual void setPlaybackSpeed(int playbackSpeed) = 0;

        virtual void start (const char *device, const char *host, bool realtime, uint8_t address_type) = 0;
        virtual void stop () = 0;

        virtual uint16_t getPacket (unsigned char *buf);

        virtual void setFilter (const char *device, const char *hostname, bool realtime,
                uint8_t address_type, uint16_t zone_server_port, uint16_t client_port) = 0;

        // Offline (pcap/tcpdump) playback only: true once the reader thread has
        // hit end-of-file AND every queued packet has been consumed. Lets the
        // decode loop emit playbackFinished at EOF, mirroring the .vpk path.
        bool offlinePlaybackComplete();

        // Capture time (unix ms) of the packet most recently returned by
        // getPacket(). For offline (pcap) replay the caller stamps decode with
        // this so --list-events reflects the ORIGINAL capture time, not replay
        // wall-clock — matching the .vpk path's recorded-time behaviour.
        int64_t lastCaptureMs() const { return m_lastCaptureMs; }


    protected:

        // Called by the derived reader thread when pcap_loop() reports the
        // offline file is exhausted (rc == 0). No-op semantics for live capture.
        void signalOfflineEof();

        // Append a captured frame to the queue getPacket() drains: allocates a
        // packetCache node, copies `data`, and links it under m_pcache_mutex
        // (dropped if the cache is closed). The libpcap reader open-codes this in
        // its callback for its own reasons; other providers should call this.
        void enqueue(const unsigned char* data, uint32_t len, int64_t ts_ms);

        struct packetCache
        {
            struct packetCache *next;
            ssize_t len;
            int64_t ts_ms;          // capture time (unix ms) from pcap_pkthdr
            unsigned char data[0];
        };
        struct packetCache *m_pcache_first;
        struct packetCache *m_pcache_last;
        bool m_pcache_closed;
        bool m_offline_eof;
        int64_t m_lastCaptureMs;    // ts_ms of the last packet getPacket() popped

        pthread_t m_tid;
        pthread_mutex_t m_pcache_mutex;

};

#endif
