#pragma once

#include "base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHM_VIDEO_FRAMES_MAGIC 0x52564652
#define SHM_VIDEO_FRAMES_VERSION 0x01
#define SHM_VIDEO_FRAMES_SLOTS_COUNT 4
#define SHM_VIDEO_FRAMES_FORMAT_I420 0

#define SYSTEM_SHARED_MEM_VIDEO_FRAMES "/ruby_shared_mem_video_frames"

typedef struct
{
   u32 uMagic;
   u32 uVersion;
   u32 uSlotsCount;
   u32 uPublishedFrameIndex;
   u32 uPublishedFrameSeq;
   u32 uWriteCounter;
   u32 uStreamsCount;
   u32 uReserve;
} type_shared_mem_video_header;

typedef struct
{
   u32 uWidth;
   u32 uHeight;
   u32 uStrideY;
   u32 uStrideUV;
   u32 uFormat;
   u32 uTimestamp;
} type_shared_mem_video_slot_meta;
extern int s_iSharedMemVideoWriteFd;
extern type_shared_mem_video_header* s_pSharedMemVideoWrite;
extern long s_lSharedMemVideoWriteSize;
extern int s_iSharedMemVideoReadFd;
extern type_shared_mem_video_header* s_pSharedMemVideoRead;
extern long s_lSharedMemVideoReadSize;
extern u32 s_uSharedMemVideoLastReadFrameSeq;

int sharedmem_video_frames_open_for_write();
void sharedmem_video_frames_close_write();

int sharedmem_video_frames_open_for_read();
void sharedmem_video_frames_close_read();

void sharedmem_video_frames_write_frame(int iFrameIndex, int iWidth, int iHeight, u8* pY, u8* pU, u8* pV, int iStrideY, int iStrideU, int iStrideV, u32 uTimestamp);
int sharedmem_video_frames_read_latest_frame(u8* pOutY, u8* pOutU, u8* pOutV, int iMaxWidth, int iMaxHeight, int* piWidth, int* piHeight, u32* puTimestamp);

#ifdef __cplusplus
}
#endif
