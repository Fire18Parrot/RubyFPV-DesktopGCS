/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions and/or use of the source code (partially or complete) must retain
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form (partially or complete) must reproduce
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
        * Neither the name of the organization nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
        * Military use is not permitted.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE AUTHOR (PETRU SOROAGA) BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "base.h"
#include "sharedmem_videoframes.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

int s_iSharedMemVideoWriteFd = 0;
type_shared_mem_video_header* s_pSharedMemVideoWrite = NULL;
long s_lSharedMemVideoWriteSize = 0;
int s_iSharedMemVideoReadFd = 0;
type_shared_mem_video_header* s_pSharedMemVideoRead = NULL;
long s_lSharedMemVideoReadSize = 0;
u32 s_uSharedMemVideoLastReadFrameSeq = 0;
static ino_t s_uSharedMemVideoReadInode = 0;

// The first versions were capped at 1920x1080: any video resolution above
// 1080p was silently rejected in sharedmem_video_frames_write_frame() and the
// video was frozen on the last HD frame. 2160p is supported now.
#define MAX_VIDEO_WIDTH_SHM  3840
#define MAX_VIDEO_HEIGHT_SHM 2160

#define SIZE_HEADER shm_video_frames_compute_header_size()
#define SIZE_SLOT   shm_video_frames_compute_slot_size()

#define OFFSET_META(i)   (SIZE_HEADER + (long)(i)*sizeof(type_shared_mem_video_slot_meta))
#define OFFSET_PLANE_Y(i) (SIZE_HEADER + SHM_VIDEO_FRAMES_SLOTS_COUNT*sizeof(type_shared_mem_video_slot_meta) + (long)(i)*((long)MAX_VIDEO_WIDTH_SHM*MAX_VIDEO_HEIGHT_SHM))
#define OFFSET_PLANE_U(i) (OFFSET_PLANE_Y(i) + (long)MAX_VIDEO_WIDTH_SHM*MAX_VIDEO_HEIGHT_SHM)
#define OFFSET_PLANE_V(i) (OFFSET_PLANE_U(i) + (long)MAX_VIDEO_WIDTH_SHM*MAX_VIDEO_HEIGHT_SHM/4)

static long shm_video_frames_compute_header_size()
{
   long lSize = sizeof(type_shared_mem_video_header);
   lSize += (SHM_VIDEO_FRAMES_SLOTS_COUNT+1) * sizeof(u32);
   long lAlign = lSize % sizeof(unsigned long long);
   if ( 0 != lAlign )
      lSize += sizeof(unsigned long long) - lAlign;
   return lSize;
}

static long shm_video_frames_compute_slot_size()
{
   long lSize = (long)MAX_VIDEO_WIDTH_SHM * (long)MAX_VIDEO_HEIGHT_SHM * 3 / 2;
   lSize += (long)MAX_VIDEO_WIDTH_SHM * (long)MAX_VIDEO_HEIGHT_SHM / 4;
   return lSize;
}

static void shm_video_frames_init_header()
{
   s_pSharedMemVideoWrite->uMagic = SHM_VIDEO_FRAMES_MAGIC;
   s_pSharedMemVideoWrite->uVersion = SHM_VIDEO_FRAMES_VERSION;
   s_pSharedMemVideoWrite->uSlotsCount = SHM_VIDEO_FRAMES_SLOTS_COUNT;
   s_pSharedMemVideoWrite->uPublishedFrameIndex = 0;
   s_pSharedMemVideoWrite->uPublishedFrameSeq = 0;
   s_pSharedMemVideoWrite->uWriteCounter = 0;
   s_pSharedMemVideoWrite->uStreamsCount = 0;
   s_pSharedMemVideoWrite->uReserve = 0;
}

// The video streamer (the player) is recreated whenever the video resolution
// or codec changes, and each instance opens the shared memory for writing.
// If it unconditionally destroyed and recreated the shared memory object,
// readers (the central controller, which attaches to it once) would keep
// their mapping of the now removed object that is never updated anymore:
// the video would freeze on the last frame rendered until the whole
// application is restarted.
// For this reason, if an existing object with the same layout (magic,
// version, slots count, size) is present, it is reused and the writer
// continues the frame publish sequence from where the previous streamer
// instance left it. The ring slots content is ignored (it can hold frames
// from the previous instance), and the frames written by the new instance
// seamlessly take over.
static bool shm_video_frames_try_reuse_existing_writer()
{
   int fd = shm_open(SYSTEM_SHARED_MEM_VIDEO_FRAMES, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
   if ( fd < 0 )
      return false;

   long lSize = SIZE_HEADER + SHM_VIDEO_FRAMES_SLOTS_COUNT*sizeof(type_shared_mem_video_slot_meta) + SHM_VIDEO_FRAMES_SLOTS_COUNT*SIZE_SLOT;

   struct stat st;
   if ( 0 != fstat(fd, &st) )
   {
      close(fd);
      return false;
   }
   if ( (long)st.st_size != lSize )
   {
      close(fd);
      return false;
   }

   s_pSharedMemVideoWrite = (type_shared_mem_video_header*)mmap(NULL, lSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if ( s_pSharedMemVideoWrite == (void*)-1 || NULL == s_pSharedMemVideoWrite )
   {
      s_pSharedMemVideoWrite = NULL;
      close(fd);
      return false;
   }

   if ( s_pSharedMemVideoWrite->uMagic != SHM_VIDEO_FRAMES_MAGIC ||
        s_pSharedMemVideoWrite->uVersion != SHM_VIDEO_FRAMES_VERSION ||
        s_pSharedMemVideoWrite->uSlotsCount != SHM_VIDEO_FRAMES_SLOTS_COUNT )
   {
      munmap(s_pSharedMemVideoWrite, lSize);
      s_pSharedMemVideoWrite = NULL;
      close(fd);
      return false;
   }

   s_iSharedMemVideoWriteFd = fd;
   s_lSharedMemVideoWriteSize = lSize;
   log_line("[SharedMemVideo] Reusing the existing shared memory object for video frames writing (%ld bytes). No frame is published yet.", lSize);
   s_pSharedMemVideoWrite->uStreamsCount = 0;
   // Reset the publishing state as well: readers detect a sequence going
   // backwards (or a sequence different than the one they last saw) and
   // immediately present the new writer's frames. It also prevents readers
   // that attach while no new frame was published yet from presenting a
   // stale frame from a previous streaming session.
   s_pSharedMemVideoWrite->uWriteCounter = 0;
   s_pSharedMemVideoWrite->uPublishedFrameIndex = 0;
   s_pSharedMemVideoWrite->uPublishedFrameSeq = 0;
   __sync_synchronize();
   return true;
}

int sharedmem_video_frames_open_for_write()
{
   if ( 0 != s_iSharedMemVideoWriteFd )
      return 0;

   if ( shm_video_frames_try_reuse_existing_writer() )
      return 0;

   long lSize = SIZE_HEADER + SHM_VIDEO_FRAMES_SLOTS_COUNT*sizeof(type_shared_mem_video_slot_meta) + SHM_VIDEO_FRAMES_SLOTS_COUNT*SIZE_SLOT;

   shm_unlink(SYSTEM_SHARED_MEM_VIDEO_FRAMES);

   int res = shm_open(SYSTEM_SHARED_MEM_VIDEO_FRAMES, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
   if ( res < 0 )
   {
      log_softerror_and_alarm("[SharedMemVideo] Failed to open shared memory file for write: %s, error: %d, %s", SYSTEM_SHARED_MEM_VIDEO_FRAMES, errno, strerror(errno));
      return -1;
   }
   s_iSharedMemVideoWriteFd = res;
   if ( 0 != ftruncate(s_iSharedMemVideoWriteFd, lSize) )
   {
      log_softerror_and_alarm("[SharedMemVideo] Failed to truncate shared memory file: %s, size: %ld, error: %d, %s", SYSTEM_SHARED_MEM_VIDEO_FRAMES, lSize, errno, strerror(errno));
      close(s_iSharedMemVideoWriteFd);
      s_iSharedMemVideoWriteFd = 0;
      return -1;
   }

   s_pSharedMemVideoWrite = (type_shared_mem_video_header*)mmap(NULL, lSize, PROT_READ | PROT_WRITE, MAP_SHARED, s_iSharedMemVideoWriteFd, 0);
   if ( s_pSharedMemVideoWrite == (void*)-1 || NULL == s_pSharedMemVideoWrite )
   {
      log_softerror_and_alarm("[SharedMemVideo] Failed to mmap shared memory file for write: %s, error: %d, %s", SYSTEM_SHARED_MEM_VIDEO_FRAMES, errno, strerror(errno));
      close(s_iSharedMemVideoWriteFd);
      s_iSharedMemVideoWriteFd = 0;
      s_pSharedMemVideoWrite = NULL;
      return -1;
   }

   s_lSharedMemVideoWriteSize = lSize;

   memset(s_pSharedMemVideoWrite, 0, lSize);
   shm_video_frames_init_header();
   return 0;
}

void sharedmem_video_frames_close_write()
{
   if ( 0 == s_iSharedMemVideoWriteFd )
      return;
   if ( NULL != s_pSharedMemVideoWrite )
      munmap((void*)s_pSharedMemVideoWrite, s_lSharedMemVideoWriteSize);
   close(s_iSharedMemVideoWriteFd);
   s_iSharedMemVideoWriteFd = 0;
   s_pSharedMemVideoWrite = NULL;
   // Do not unlink the shared memory object here. On a clean restart of the
   // player (for example a codec or resolution change) the next player
   // instance reuses the existing object (same layout, same inode) and
   // continues publishing frames into it, so the readers keep their mapping
   // and the new frames reach them immediately. Unlinking here would leave
   // the readers with a mapping of a removed object that is never updated
   // anymore: the video would freeze on the last frame until the whole
   // application is restarted. The object is only replaced (unlinked and
   // recreated) by sharedmem_video_frames_open_for_write() when the layout
   // must change, and it is safely reused after an application restart.
}

int sharedmem_video_frames_open_for_read()
{
   if ( 0 != s_iSharedMemVideoReadFd )
      return 0;

   long lSize = SIZE_HEADER + SHM_VIDEO_FRAMES_SLOTS_COUNT*sizeof(type_shared_mem_video_slot_meta) + SHM_VIDEO_FRAMES_SLOTS_COUNT*SIZE_SLOT;

   int res = shm_open(SYSTEM_SHARED_MEM_VIDEO_FRAMES, O_RDONLY, S_IRUSR | S_IRGRP | S_IROTH);
   if ( res < 0 )
      return -1;
   s_iSharedMemVideoReadFd = res;

   struct stat st;
   if ( 0 == fstat(s_iSharedMemVideoReadFd, &st) )
      s_uSharedMemVideoReadInode = st.st_ino;
   else
      s_uSharedMemVideoReadInode = 0;

   // Validate that the object matches this build's layout, else reading the
   // planes would fault (mapping of a smaller object).
   if ( (long)st.st_size != lSize )
   {
      log_softerror_and_alarm("[SharedMemVideo] The existing shared memory object for video frames has an unexpected size: %ld (expected %ld). Ignoring it.", (long)st.st_size, lSize);
      close(s_iSharedMemVideoReadFd);
      s_iSharedMemVideoReadFd = 0;
      s_pSharedMemVideoRead = NULL;
      s_uSharedMemVideoReadInode = 0;
      return -1;
   }

   s_pSharedMemVideoRead = (type_shared_mem_video_header*)mmap(NULL, lSize, PROT_READ, MAP_SHARED, s_iSharedMemVideoReadFd, 0);
   if ( s_pSharedMemVideoRead == (void*)-1 || NULL == s_pSharedMemVideoRead )
   {
      log_softerror_and_alarm("[SharedMemVideo] Failed to mmap shared memory file for read: %s, error: %d, %s", SYSTEM_SHARED_MEM_VIDEO_FRAMES, errno, strerror(errno));
      close(s_iSharedMemVideoReadFd);
      s_iSharedMemVideoReadFd = 0;
      s_pSharedMemVideoRead = NULL;
      return -1;
   }
   s_lSharedMemVideoReadSize = lSize;
   // Start from the sequence currently published in the object: frames from
   // that sequence were published before this reader attached (for example
   // stale frames from a previous application run) and must not be
   // presented as current video.
   s_uSharedMemVideoLastReadFrameSeq = s_pSharedMemVideoRead->uPublishedFrameSeq;
   return 0;
}

void sharedmem_video_frames_close_read()
{
   if ( 0 == s_iSharedMemVideoReadFd )
      return;
   if ( NULL != s_pSharedMemVideoRead )
      munmap((void*)s_pSharedMemVideoRead, s_lSharedMemVideoReadSize);
   close(s_iSharedMemVideoReadFd);
   s_iSharedMemVideoReadFd = 0;
   s_pSharedMemVideoRead = NULL;
   s_uSharedMemVideoReadInode = 0;
   s_uSharedMemVideoLastReadFrameSeq = 0;
}

void sharedmem_video_frames_write_frame(int iFrameIndex, int iWidth, int iHeight, u8* pY, u8* pU, u8* pV, int iStrideY, int iStrideU, int iStrideV, u32 uTimestamp)
{
   if ( NULL == s_pSharedMemVideoWrite || iFrameIndex < 0 || iFrameIndex >= SHM_VIDEO_FRAMES_SLOTS_COUNT )
      return;
   if ( NULL == pY || NULL == pU || NULL == pV )
      return;
   if ( iWidth > MAX_VIDEO_WIDTH_SHM || iHeight > MAX_VIDEO_HEIGHT_SHM || iWidth < 1 || iHeight < 1 )
      return;

   // Write planes compacted (stride in shared memory equals width, rows are contiguous)
   u8* pDstY = (u8*)s_pSharedMemVideoWrite + OFFSET_PLANE_Y(iFrameIndex);
   u8* pDstU = (u8*)s_pSharedMemVideoWrite + OFFSET_PLANE_U(iFrameIndex);
   u8* pDstV = (u8*)s_pSharedMemVideoWrite + OFFSET_PLANE_V(iFrameIndex);

   for ( int y = 0; y < iHeight; y++ )
      memcpy(pDstY + (long)y*iWidth, pY + (long)y*iStrideY, iWidth);
   for ( int y = 0; y < iHeight/2; y++ )
      memcpy(pDstU + (long)y*(iWidth/2), pU + (long)y*iStrideU, iWidth/2);
   for ( int y = 0; y < iHeight/2; y++ )
      memcpy(pDstV + (long)y*(iWidth/2), pV + (long)y*iStrideV, iWidth/2);

   type_shared_mem_video_slot_meta* pMeta = (type_shared_mem_video_slot_meta*)((u8*)s_pSharedMemVideoWrite + OFFSET_META(iFrameIndex));
   pMeta->uWidth = iWidth;
   pMeta->uHeight = iHeight;
   pMeta->uStrideY = iWidth;
   pMeta->uStrideUV = iWidth/2;
   pMeta->uFormat = 0; // I420
   pMeta->uTimestamp = uTimestamp;

   // Memory barrier before publishing the frame
   __sync_synchronize();
   u32 seq = s_pSharedMemVideoWrite->uWriteCounter + 1;
   s_pSharedMemVideoWrite->uWriteCounter = seq;
   s_pSharedMemVideoWrite->uPublishedFrameIndex = iFrameIndex;
   s_pSharedMemVideoWrite->uPublishedFrameSeq = seq;
}

int sharedmem_video_frames_read_latest_frame(u8* pOutY, u8* pOutU, u8* pOutV, int iMaxWidth, int iMaxHeight, int* piWidth, int* piHeight, u32* puTimestamp)
{
   if ( NULL == s_pSharedMemVideoRead )
      return 0;

   // Detect if the shared memory object was replaced (for example destroyed
   // and recreated by an older version of the writer, or by a binary update
   // with a changed layout): a mapping of the old (unlinked) object keeps
   // working but is never updated by the new writer, so the video would
   // appear frozen without this check.
   // Note: fstat() on our own fd would always return the inode of the old
   // object (the mapping stays valid even after the object was unlinked), so
   // the object must be opened and stat'ed by name to detect the replacement.
   if ( 0 != s_uSharedMemVideoReadInode && NULL != s_pSharedMemVideoRead )
   {
      struct stat st;
      int fdCheck = shm_open(SYSTEM_SHARED_MEM_VIDEO_FRAMES, O_RDONLY, 0);
      if ( fdCheck >= 0 )
      {
         bool bReopen = false;
         if ( 0 == fstat(fdCheck, &st) && st.st_ino != s_uSharedMemVideoReadInode )
            bReopen = true;
         close(fdCheck);
         if ( bReopen )
         {
            log_line("[SharedMemVideo] The shared memory object for video frames was replaced by the writer. Re-opening it.");
            sharedmem_video_frames_close_read();
            if ( 0 != sharedmem_video_frames_open_for_read() )
            {
               // The object might have been removed by the writer between
               // close and open. Try again on the next read call.
               return 0;
            }
         }
      }
   }

   if ( s_pSharedMemVideoRead->uMagic != SHM_VIDEO_FRAMES_MAGIC )
      return 0;
   if ( s_pSharedMemVideoRead->uVersion != SHM_VIDEO_FRAMES_VERSION )
      return 0;
   u32 seq = s_pSharedMemVideoRead->uPublishedFrameSeq;
   if ( 0 == seq || seq == s_uSharedMemVideoLastReadFrameSeq )
      return 0;

   int iSlot = s_pSharedMemVideoRead->uPublishedFrameIndex;
   if ( iSlot < 0 || iSlot >= SHM_VIDEO_FRAMES_SLOTS_COUNT )
   {
      s_uSharedMemVideoLastReadFrameSeq = seq;
      return 0;
   }

   type_shared_mem_video_slot_meta* pMeta = (type_shared_mem_video_slot_meta*)((u8*)s_pSharedMemVideoRead + OFFSET_META(iSlot));
   int iWidth = pMeta->uWidth;
   int iHeight = pMeta->uHeight;
   if ( iWidth < 1 || iHeight < 1 || iWidth > iMaxWidth || iHeight > iMaxHeight )
   {
      s_uSharedMemVideoLastReadFrameSeq = seq;
      return 0;
   }

   const u8* pSrcY = (const u8*)s_pSharedMemVideoRead + OFFSET_PLANE_Y(iSlot);
   const u8* pSrcU = (const u8*)s_pSharedMemVideoRead + OFFSET_PLANE_U(iSlot);
   const u8* pSrcV = (const u8*)s_pSharedMemVideoRead + OFFSET_PLANE_V(iSlot);

   for ( int y = 0; y < iHeight; y++ )
      memcpy(pOutY + (long)y*iWidth, pSrcY + (long)y*iWidth, iWidth);
   for ( int y = 0; y < iHeight/2; y++ )
      memcpy(pOutU + (long)y*(iWidth/2), pSrcU + (long)y*(iWidth/2), iWidth/2);
   for ( int y = 0; y < iHeight/2; y++ )
      memcpy(pOutV + (long)y*(iWidth/2), pSrcV + (long)y*(iWidth/2), iWidth/2);

   __sync_synchronize();
   // Detect a torn frame: the writer wrapped around the whole ring and
   // started overwriting the slot we are copying from while the copy was in
   // progress. Presenting such a frame would show a flash of two half
   // updated frames. Discard it; a newer published frame is presented on
   // the next call.
   u32 seqAfter = s_pSharedMemVideoRead->uPublishedFrameSeq;
   u32 idxAfter = s_pSharedMemVideoRead->uPublishedFrameIndex;
   if ( idxAfter == (u32)iSlot && seqAfter != seq )
   {
      s_uSharedMemVideoLastReadFrameSeq = seqAfter;
      return 0;
   }
   s_uSharedMemVideoLastReadFrameSeq = seq;

   if ( NULL != piWidth )
      *piWidth = iWidth;
   if ( NULL != piHeight )
      *piHeight = iHeight;
   if ( NULL != puTimestamp )
      *puTimestamp = pMeta->uTimestamp;
   return 1;
}
