/*
 *  vpacket.h
 *  Copyright 2000 Maerlyn (MaerlynTheWiz@yahoo.com)
 *  Copyright 2001, 2019 by the respective ShowEQ Developers
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

/*
 * Orig Author - Maerlyn (MaerlynTheWiz@yahoo.com)
 * Date   - 3/11/00
 */

/*
 * VPacket
 *
 * VPacket is a generic class that allows recording and playback of
 * generic data with timestamps.  The data is stored in binary format
 * with headers around it so that it is retrieved in the same manner 
 * (exact same 'chunks') that it was sent.  This makes a good class for
 * a packet capture
 *
 * The following public member functions are available:
 *
 * Plaback(...)                Fetch data from the buffer
 * Record(...)                 Record data to the buffer
 * Flush()                     Force a flush of data to the file
 * setPlaybackSpeed()          Set a playback rate (0=not timed, 1=1X, etc)
 * EndOfData()                 Check for out of data
 *
 *
 * The intention of this class was to capture network packets to play back
 * at a later time.  Using without a file is untested, as well as using to
 * Fetch and Record simultaneously.  These features may not be implemented
 */
 
#ifndef VPACKET_H
#define VPACKET_H

#include <ctime>

#define DEFBUFSIZE 8192
#define USEVERSION

struct packet_struct
{
   int     size;
   time_t  time;
#ifdef USEVERSION
   long    version;
#endif
   long    ms;
   long    sequence;
   char    buffer[0];
};

class VPacket
{
 public:
   VPacket(const char *name = 0, int timed = 3, 
	   bool bRecord = false, int bufsize = DEFBUFSIZE);
   ~VPacket();

   int Playback(char *buff, int bufsize, time_t* time, long *ver = NULL);
   int Record(const char *buff, int bufsize, time_t time, long ver = 0);
   void Flush(void)                     { if (m_bRecord) writeBuffer(); }
   void setPlaybackSpeed(int speed);
   void setFlushPacket(bool inset)       { m_bFlushPacket = inset; }
   void setCompressTime(int ms)         { m_nCompressTime = ms; }
   int endOfData(void)                  { return m_bEndofFile; }
   long FilePos(void)                   { return m_lBytesIO;   }
   bool isRecording(void)               { return m_bRecord; }
   const char* getFileName()            { return m_sFile; }

 private:
   int   fillBuffer(void);
   int   writeBuffer(void);
   int   flush(void);

   char* m_sFile;
   int   m_fd;
   char* m_cBuffer;
   int   m_nBufIndex;           // Current Index into buffer
   int   m_nBufBytes;           // Bytes avail in buffer
   int   m_nBufSize;            // Max Size of buffer
   int   m_nPlaybackSpeed;      // Playback speed (0=ASAP, 1=1x, 2=2x, etc)
   long  m_lStartTime;
   long  m_nSequence;
   bool   m_bEndofFile;
   long  m_lBytesIO;            // number of bytes transferred since creation
   int   mTime();
   int   m_bFlushPacket;
   int   m_nLastPacketTime;
   int   m_nFirstPacketTime;
   int   m_nLastTime;
   int   m_nCompressTime;
   bool m_bRecord;
};

#endif				// VPACKET_H
