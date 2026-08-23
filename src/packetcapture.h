/*
 *  packetcapture.h
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

#ifndef _PACKETCAPTURE_H_
#define _PACKETCAPTURE_H_

#include <pthread.h>

#ifdef __FreeBSD__
#include <sys/ioccom.h>
#endif

extern "C" { // fix for bpf not being c++ happy
#include <pcap.h>
}

#include "packetcaptureprovider.h"
#include "packetcommon.h"


//----------------------------------------------------------------------
// constants
// Used in the packet capture filter setup.  If address_type is
//   MAC_ADDRESS_TYPE, then we use the hostname string as a MAC address
// for the filter. cpphack
const uint8_t DEFAULT_ADDRESS_TYPE = 10;   /* These were chosen arbitrarily */
const uint8_t IP_ADDRESS_TYPE = 11;
const uint8_t MAC_ADDRESS_TYPE =  12;

//----------------------------------------------------------------------
// PacketCaptureThread
class PacketCaptureThread : public PacketCaptureProviderThread
{
    public:
        PacketCaptureThread(int snaplen, int buffersize);
        ~PacketCaptureThread();

        // Set the playback speed for offline packet capture. Valid values
        // are -1-9, 1 is 1x, 2 is 2x, etc. -1 is paused. 0 is as fast as
        // possible (no throttle)
        void setPlaybackSpeed(int playbackSpeed);
        int getPlaybackSpeed() { return (m_playbackSpeed == 100 ? 0 : m_playbackSpeed); }

        void start (const char *device, const char *host, bool realtime, uint8_t address_type);
        void startOffline(const char* filename, int playbackSpeed);
        void stop ();

        //moved to base class
        //uint16_t getPacket (unsigned char *buff);

        void setFilter (const char *device, const char *hostname, bool realtime,
                uint8_t address_type, uint16_t zone_server_port, uint16_t client_port);

    private:
        static void* loop(void *param);
        static void packetCallBack(u_char * param, const struct pcap_pkthdr *ph, const u_char *data);
        static unsigned int last_ps_ifdrop;
        static unsigned int last_ps_drop;

        pcap_t *m_pcache_pcap;

        // Playback controls for offline file processing
        int m_playbackSpeed; // -1=paused, 0=max, 1=1x speed, 2=2x speed, up to 9
        timeval m_tvLastProcessedActual;
        timeval m_tvLastProcessedOriginal;

        int m_snaplen;
        int m_buffersize;
};

#endif // _PACKETCAPTURE_H_
