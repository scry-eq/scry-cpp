/*
 *  packetcapture.cpp
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

/* Implementation of Packet class */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include "packetcapture.h"
#include "packetcommon.h"
#include "diagnosticmessages.h"

//#define PCAP_DEBUG 1

unsigned int PacketCaptureThread::last_ps_ifdrop = 0;
unsigned int PacketCaptureThread::last_ps_drop = 0;

//----------------------------------------------------------------------
// PacketCaptureThread
//  start and stop the thread
//  get packets to the processing engine(dispatchPacket)
PacketCaptureThread::PacketCaptureThread(int snaplen, int buffersize) : PacketCaptureProviderThread(),
    m_pcache_pcap(NULL),
    m_playbackSpeed(0),
    m_snaplen(snaplen),
    m_buffersize(buffersize)
{ }

PacketCaptureThread::~PacketCaptureThread()
{
    // Stop the capture thread and close the handle safely (see stop()).
    stop();
}

void PacketCaptureThread::setPlaybackSpeed(int playbackSpeed)
{
    if (playbackSpeed < -1)
    {
        m_playbackSpeed = -1;
    }
    else if (playbackSpeed > 9)
    {
        m_playbackSpeed = 9;
    }
    else if (playbackSpeed == 0)
    {
        // Fast as possible. But using 0 makes the UI unresponsive!
        m_playbackSpeed = 100;
    }
    else
    {
        m_playbackSpeed = playbackSpeed;
    }
}

void PacketCaptureThread::start(const char *device, const char *host, 
        bool realtime, uint8_t address_type)
{
    char ebuf[PCAP_ERRBUF_SIZE]; // pcap error buffer

    seqInfo("Initializing Packet Capture Thread: ");
    m_pcache_closed = false;

    /* We've replaced pcap_open_live() with pcap_create/pcap_activate.
     * This allows us to use immedate mode, rather than approximating it with
     * a 1ms timeout value.
     *
     * NOTE: a race condition exists between this thread and the main thread
     * - any artificial delay in getting packets can cause filtering problems
     *   and cause us to miss new stream when the player zones.
     */

    // initialize the pcap object 
    m_pcache_pcap = pcap_create((const char*)device, ebuf);

    if (!m_pcache_pcap)
    {
        seqFatal("pcap_error:pcap_create(%s): %s", device, ebuf);
        if ((getuid() != 0) && (geteuid() != 0))
        {
            seqWarn("Make sure you are running ShowEQ as root.");
        }
        exit(1);
    }

    pcap_set_promisc(m_pcache_pcap, 1);
    pcap_set_snaplen(m_pcache_pcap, m_snaplen*1024);
    pcap_set_buffer_size(m_pcache_pcap, m_buffersize*1024*1024);

#ifndef __FreeBSD__
    pcap_set_immediate_mode(m_pcache_pcap, 1);
#endif

    int act = pcap_activate(m_pcache_pcap);
    if (act > 0)
    {
        seqWarn("pcap_warning:pcap_activate:%s", pcap_statustostr(act));
    }
    else if (act < 0)
    {
        seqFatal("pcap_error:pcap_activate:%s", pcap_statustostr(act));
        pcap_close(m_pcache_pcap);
        exit(1);
    }

#ifdef __FreeBSD__
    // if we're on FreeBSD, we need to call ioctl on the file descriptor
    // with BIOCIMMEDIATE to get the kernel Berkeley Packet Filter device
    // to return packets to us immediately, rather than holding them in
    // it's internal buffer... if we don't do this, we end up getting 32K
    // worth of packets all at once, at long intervals -- if someone
    // knows a less hacky way of doing this, I'd love to hear about it.
    // the problem here is that libpcap doesn't expose an API to do this
    // in any way
    int fd = pcap_fileno(m_pcache_pcap);
    int temp = 1;
    if ( ioctl( fd, BIOCIMMEDIATE, &temp ) < 0 )
    {
        seqWarn("PCAP couldn't set immediate mode on BSD" );
    }
#endif

    m_pcache_first = m_pcache_last = NULL;

    // Install the capture filter BEFORE launching the read loop. pcap_loop() and
    // pcap_setfilter() both touch the non-thread-safe pcap_t handle; on a
    // freshly-activated handle, the loop thread's mmap ring-buffer setup races
    // with pcap_setfilter() and segfaults inside libpcap (null deref in the mmap
    // read path) when a device is selected. Compiling/installing the filter on
    // this thread first, then spawning the loop, serializes all handle setup.
    this->setFilter(device, host, realtime, address_type, 0, 0);

    pthread_create (&m_tid, NULL, loop, (void*)this);

    // Now that the capture thread exists, raise its priority if requested. This
    // previously lived in setFilter(), which forced setFilter() to run after
    // thread creation and is what created the race above.
    if (realtime)
    {
        struct sched_param sp;
        memset (&sp, 0, sizeof (sp));
        sp.sched_priority = 1;
        if (pthread_setschedparam (m_tid, SCHED_RR, &sp) != 0)
        {
            seqWarn("Failed to set capture thread realtime.");
        }
    }

}

//------------------------------------------------------------------------
// Capture thread for offline packet capture. Input filename should be a
// tcpdump file. playbackSpeed is how fast to playback. 1 = realtime,
// 2 = 2x speed, 3 = 3x speed, etc. 0 = no throttle. -1 = paused.
//
void PacketCaptureThread::startOffline(const char* filename, int playbackSpeed)
{
    char ebuf[256]; // pcap error buffer

    seqInfo("Initializing Offline Packet Capture Thread: ");
    m_pcache_closed = false;

    // initialize the pcap object 
    m_pcache_pcap = pcap_open_offline(filename, ebuf);

    if (!m_pcache_pcap)
    {
        seqWarn("pcap_error:pcap_open_offline(%s): %s", filename, ebuf);
        exit(0);
    }

    // Set the speed
    setPlaybackSpeed(playbackSpeed);

    m_tvLastProcessedActual.tv_sec = 0;
    m_tvLastProcessedOriginal.tv_sec = 0;

    m_pcache_first = m_pcache_last = NULL;

    // The decode pipeline (EQPacket::processPackets) assumes libpcap has already
    // narrowed the stream to UDP — the live path guarantees this in setFilter().
    // A raw offline capture (e.g. from dumpcap) carries the client's entire
    // conversation (TCP/HTTPS/Steam/etc.), so without the same filter those
    // frames get misparsed as EQ packets. Install the identical open-UDP filter
    // used for live auto-detect, BEFORE the read loop starts (same handle-race
    // reasoning as start()/setFilter()).
    struct bpf_program bpp;
    // Exclude mDNS (port 5353) + multicast: EQ is always unicast client<->server,
    // but LAN service-discovery (Bonjour/mDNS "_dosvc._tcp.local" etc.) is high-port
    // UDP that otherwise passes and gets misparsed as EQ (net-opcode 0000 spam).
    static const char udpFilter[] =
        "udp[0:2] > 1024 and udp[2:2] > 1024 and not port 5353 and not net 224.0.0.0/4";
    if (pcap_compile(m_pcache_pcap, &bpp, udpFilter, 1, 0) == 0)
    {
        if (pcap_setfilter(m_pcache_pcap, &bpp) == -1)
            seqWarn("startOffline: pcap_setfilter failed: %s",
                    pcap_geterr(m_pcache_pcap));
        pcap_freecode(&bpp);
    }
    else
    {
        seqWarn("startOffline: pcap_compile(\"%s\") failed: %s — replaying "
                "unfiltered; non-UDP frames may be misdecoded",
                udpFilter, pcap_geterr(m_pcache_pcap));
    }

    // The pipeline strips a fixed 14-byte Ethernet header per frame. A capture
    // taken with a different link-layer (Linux cooked SLL = 16B, raw IP = 0B)
    // would be silently misaligned — every opcode would decode to garbage.
    // Warn loudly rather than pretend to decode it.
    const int dlt = pcap_datalink(m_pcache_pcap);
    if (dlt != DLT_EN10MB)
    {
        const char* dltName = pcap_datalink_val_to_name(dlt);
        seqWarn("startOffline: capture link-layer is %s (not Ethernet); decode "
                "offsets assume a 14-byte Ethernet header and will be wrong",
                dltName ? dltName : "unknown");
    }

    pthread_create(&m_tid, NULL, loop, (void*)this);
}

void PacketCaptureThread::stop()
{
    // close the pcap session
    if (m_pcache_pcap)
    {
        // Stop the capture thread BEFORE closing the handle. The thread is
        // blocked in pcap_loop() on m_pcache_pcap; pcap_close()ing it out from
        // under the running loop is a use-after-free inside libpcap. Mark the
        // cache closed, break the loop (libpcap >= 1.10 wakes a blocked read),
        // join the thread, then close the handle.
        pthread_mutex_lock(&m_pcache_mutex);
        m_pcache_closed = true;
        pthread_mutex_unlock(&m_pcache_mutex);

        pcap_breakloop(m_pcache_pcap);
        pthread_join(m_tid, NULL);

        pcap_close(m_pcache_pcap);
        m_pcache_pcap = NULL;
    }
}

void* PacketCaptureThread::loop (void *param)
{
    PacketCaptureThread* myThis = (PacketCaptureThread*)param;
    const int rc = pcap_loop (myThis->m_pcache_pcap, -1, packetCallBack, (u_char*)param);
    // rc == 0: pcap ran out of packets. For an offline capture that's EOF; a
    // live capture only returns via pcap_breakloop() (PCAP_ERROR_BREAK == -2)
    // or an error (-1). Flag EOF so the consumer emits playbackFinished once
    // the queue drains.
    if (rc == 0)
        myThis->signalOfflineEof();
    return NULL;
}

void PacketCaptureThread::packetCallBack(u_char * param, 
            const struct pcap_pkthdr *ph,
            const u_char *data)
{
    struct packetCache *pc;
    PacketCaptureThread* myThis = (PacketCaptureThread*)param;
    pc = (struct packetCache *) malloc (sizeof (struct packetCache) + ph->len);
    pc->len = ph->len;
    pc->ts_ms = (int64_t)ph->ts.tv_sec * 1000 + ph->ts.tv_usec / 1000;
    memcpy (pc->data, data, ph->len);
    pc->next = NULL;

#ifdef PCAP_DEBUG
    struct ether_header* ethHeader = (struct ether_header*) data;

    if (ntohs(ethHeader->ether_type) == ETHERTYPE_IP)
    {
        struct ip* ipHeader = 
            (struct ip*) (data + sizeof(struct ether_header));

        char src[128];
        strcpy(src, inet_ntoa(ipHeader->ip_src));
        char dst[128];
        strcpy(dst, inet_ntoa(ipHeader->ip_dst));

        if (ipHeader->ip_p == IPPROTO_UDP)
        {
            struct udphdr* udpHeader = 
                (struct udphdr*) (data + sizeof(struct ip) + sizeof(struct ether_header));

            printf("recv(%d): %s:%d -> %s:%d (size: %d)\n",
                    ipHeader->ip_p,
                    src,
                    ntohs(udpHeader->source),
                    dst,
                    ntohs(udpHeader->dest),
                    ph->len);
        }
        else
        {
            printf("Non-UDP traffic %s -> %s\n",
                    inet_ntoa(ipHeader->ip_src),
                    inet_ntoa(ipHeader->ip_dst));
        }
    }
    else
    {
        printf("Non-IP packet...\n");
    }
#endif

    // Throttle offline playback properly if applicable.
    int speed = myThis->m_playbackSpeed;

    if (speed != 0)
    {
        if (speed == -1)
        {
            // We are paused. Need to wait for it to unpause.
            while ((speed = myThis->m_playbackSpeed) == -1)
            {
                sleep(1);
            }
        }

        // Playing back from a file. Need to honor playback speed and packet
        // timestamps properly.
        timeval now;

        if (gettimeofday(&now, NULL) == 0)
        {
            // Anchor the first run through.
            if (myThis->m_tvLastProcessedActual.tv_sec == 0)
            {
                myThis->m_tvLastProcessedActual = now;
            }
            if (myThis->m_tvLastProcessedOriginal.tv_sec == 0)
            {
                myThis->m_tvLastProcessedOriginal = ph->ts;
            }

            // The goal here is to make sure that time elapsed since last
            // packet / playbackSpeed > time elapsed between original
            // previous packet and this packet. If it is not, we need to sleep
            // for the difference.
            long usecDiffActual = 
                ((now.tv_sec - myThis->m_tvLastProcessedActual.tv_sec)*1000000 +
                (now.tv_usec - myThis->m_tvLastProcessedActual.tv_usec));
            long usecDiffOriginal =
                ((ph->ts.tv_sec - myThis->m_tvLastProcessedOriginal.tv_sec)*1000000 +
                (ph->ts.tv_usec - myThis->m_tvLastProcessedOriginal.tv_usec)) /
                    ((long) speed);

            if (usecDiffActual < usecDiffOriginal)
            {
                // Need to wait out the difference.
                timeval tvWait;
                
                tvWait.tv_usec = (usecDiffOriginal - usecDiffActual) % 1000000;
                tvWait.tv_sec = (usecDiffOriginal - usecDiffActual) / 1000000;

                select(1, NULL, NULL, NULL, &tvWait);
            }

            // And get ready for next one
            myThis->m_tvLastProcessedActual = now;
        }
    }

    myThis->m_tvLastProcessedOriginal = ph->ts;

    pthread_mutex_lock (&myThis->m_pcache_mutex);
   
    if (! myThis->m_pcache_closed)
    {
        if (myThis->m_pcache_last)
        {
            myThis->m_pcache_last->next = pc;
        }

        myThis->m_pcache_last = pc;

        if (!myThis->m_pcache_first)
        {
            myThis->m_pcache_first = pc;
        }
    }
    else
    {
      free(pc);
    }

    pthread_mutex_unlock (&myThis->m_pcache_mutex);

    struct pcap_stat ps = {0};

    //NOTE: using fprintf here because seqWarn sends a MessageEntry, which
    //can't cross thread boundaries using signals/slots without doing work
    //to change MessageEntry to a proper Qt MetaType FIXME
    pcap_stats(myThis->m_pcache_pcap, &ps);
    if (ps.ps_ifdrop > 0 && ps.ps_ifdrop != last_ps_ifdrop)
    {
        fprintf(stderr, "PCAP detected %d packets dropped at the network interface! "
                "This could cause ShowEQ to malfunction.  Read FAQ #5 in the "
                "FAQ located in the ShowEQ source directory for information "
                "about tuning your kernel networking parameters.\n", ps.ps_ifdrop);
        fprintf(stderr, "Packet loss due to dropping at the interface: %02f%%\n", (ps.ps_ifdrop * 1.0) / (ps.ps_recv * 1.0) * 100.0);
        last_ps_ifdrop = ps.ps_ifdrop;
    }

    if (ps.ps_drop > 0 && ps.ps_drop != last_ps_drop)
    {
        fprintf(stderr, "PCAP detected %d packets dropped due to insufficent PCAP buffer size! "
                "This could cause ShowEQ to malfunction.  Increase the PCAP buffer "
                "size and/or decrease the PCAP snapshot length.\n", ps.ps_drop);
        fprintf(stderr, "Packet loss due to dropping at the PCAP buffer: %02f%%\n", (ps.ps_drop * 1.0) / (ps.ps_recv * 1.0) * 100.0);
        last_ps_drop = ps.ps_drop;
    }
}

void PacketCaptureThread::setFilter (const char *device,
                                     const char *hostname,
                                     bool realtime,
                                     uint8_t address_type,
                                     uint16_t zone_port,
                                     uint16_t client_port)
{
    char filter_buf[256]; // pcap filter buffer
    char* pfb = filter_buf;
    char ebuf[PCAP_ERRBUF_SIZE];
    struct bpf_program bpp;
    bpf_u_int32 mask = 0; // sniff device netmask
    bpf_u_int32 net = 0 ; // sniff device ip

    // Fetch the netmask for the device to use later with the filter
    if (pcap_lookupnet(device, &net, &mask, ebuf) == -1)
    {
        // Couldn't find net mask. Just leave it open.
        seqWarn("Couldn't determine netmask of device %s. Using 0.0.0.0. Error was %s",
                device, ebuf);
    }

    if (!client_port && !zone_port)
    {
        //no client/zone port detected, so leave it open (but drop mDNS/multicast
        // LAN noise — EQ is unicast; see the offline filter above for why).
        pfb += sprintf(pfb, "udp[0:2] > 1024 and udp[2:2] > 1024 and not port 5353 and not net 224.0.0.0/4");
    }
    else
    {
        // restrict to client/zone port and world server ports
        pfb += sprintf(pfb, "udp");
        pfb += sprintf(pfb, " and (portrange %d-%d or port %d or port %d)",
                WorldServerGeneralMinPort, WorldServerGeneralMaxPort,
                WorldServerChatPort, (client_port) ? client_port : zone_port);
    }

    if (address_type == IP_ADDRESS_TYPE)
    {
        if (hostname && strlen(hostname) && strcmp(hostname, AUTOMATIC_CLIENT_IP) != 0)
            // host/net was specified/detected. A CIDR (contains '/') scopes to a
            // netblock — e.g. the Daybreak server block 69.174.0.0/16 to keep
            // only EQ traffic (login/world/zone/UCS/voice) and drop ambient LAN
            // UDP (IPsec NAT-T, cloud services) a mirror port also sees.
            pfb += sprintf(pfb, " and %s %s",
                           strchr(hostname, '/') ? "net" : "host", hostname);
    }
    else if (address_type == MAC_ADDRESS_TYPE)
    {
        if (hostname && strlen(hostname) && strcmp(hostname, AUTOMATIC_CLIENT_IP) != 0)
            // mac was specified
            pfb += sprintf(pfb, " and ether host %s", hostname);
    }
    else if (address_type == DEFAULT_ADDRESS_TYPE)
    {
        //don't specify IP or MAC address in filter string
    }
    else
    {
        seqFatal("pcap_error:filter_string: unknown address_type (%d)", address_type);
        exit(0);
    }

    //restrict to ipv4, and ignore broad/multi-cast packets
    pfb += sprintf(pfb, " and ether proto 0x800 and not broadcast and not multicast");

    seqInfo("Filtering packets on device %s", device);

    if (pcap_compile (m_pcache_pcap, &bpp, filter_buf, 1, net) == -1)
    {
        seqWarn("%s",filter_buf);
        pcap_perror(m_pcache_pcap, (char*)"pcap_error:pcap_compile_error");
        exit(0);
    }

    if (pcap_setfilter (m_pcache_pcap, &bpp) == -1)
    {
        pcap_perror(m_pcache_pcap, (char*)"pcap_error:pcap_setfilter_error");
        exit (0);
    }

    pcap_freecode(&bpp);

    seqDebug("PCAP Filter Set: %s", filter_buf);

    // realtime priority is applied once at thread creation in start(); see note
    // there. Re-applying it on every filter change is unnecessary (the capture
    // thread keeps its SCHED_RR priority for its lifetime).
    (void)realtime;
}
