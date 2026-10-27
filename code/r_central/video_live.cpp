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

#include "../base/base.h"
#include "../base/config.h"
#include "../base/sharedmem_videoframes.h"
#include "timers.h"
#include "video_live.h"

#define VIDEO_LIVE_MAX_WIDTH  1920
#define VIDEO_LIVE_MAX_HEIGHT 1080

// Scratch buffers: I420 planes and the converted ARGB frame (BGRA bytes in
// memory, cairo ARGB32 compatible).

static u8 s_pVideoLiveY[VIDEO_LIVE_MAX_WIDTH * VIDEO_LIVE_MAX_HEIGHT];
static u8 s_pVideoLiveU[VIDEO_LIVE_MAX_WIDTH * VIDEO_LIVE_MAX_HEIGHT / 4];
static u8 s_pVideoLiveV[VIDEO_LIVE_MAX_WIDTH * VIDEO_LIVE_MAX_HEIGHT / 4];
// Double-buffered ARGB output: conversion writes into the back buffer and
// only after a full frame conversion the front buffer index is flipped, so
// the render thread almost never sees a half-converted frame.
static u32 s_pVideoLiveARGB[2][VIDEO_LIVE_MAX_WIDTH * VIDEO_LIVE_MAX_HEIGHT];
static int s_iVideoLiveFrontIndex = 0;
static int s_iVideoLiveWidth = 0;
static int s_iVideoLiveHeight = 0;
static bool s_bVideoLiveHasFrame = false;
static bool s_bVideoLiveEnabled = false;
static u32 s_uTimeLastOpenAttempt = 0;

static bool video_live_try_open_read()
{
   if ( 0 != sharedmem_video_frames_open_for_read() )
      return false;
   return true;
}

void video_live_init()
{
   s_bVideoLiveEnabled = true;
   video_live_try_open_read();
   s_uTimeLastOpenAttempt = get_current_timestamp_ms();
   log_line("[VideoLive] Merged video module initialized.");
}

void video_live_shutdown()
{
   sharedmem_video_frames_close_read();
}

void video_live_set_enabled(bool bEnabled)
{
   s_bVideoLiveEnabled = bEnabled;
}

bool video_live_is_enabled()
{
   return s_bVideoLiveEnabled;
}

bool video_live_has_data()
{
   return (s_bVideoLiveHasFrame && s_iVideoLiveWidth > 0 && s_iVideoLiveHeight > 0);
}

// Pulls the latest published frame and converts it to ARGB. Cheap when no new
// frame was published (a single u32 sequence check).

void video_live_update()
{
   if ( ! s_bVideoLiveEnabled )
      return;

   if ( 0 == s_iSharedMemVideoReadFd )
   {
      // The player creates the shared memory on start; retry opening it
      // once per second, without spamming logs.
      u32 uNow = get_current_timestamp_ms();
      if ( s_uTimeLastOpenAttempt != 0 && (uNow - s_uTimeLastOpenAttempt) < 1000 )
         return;
      s_uTimeLastOpenAttempt = uNow;
      if ( ! video_live_try_open_read() )
         return;
   }

   int iWidth = 0;
   int iHeight = 0;
   u32 uTimestamp = 0;
   int iRes = sharedmem_video_frames_read_latest_frame(s_pVideoLiveY, s_pVideoLiveU, s_pVideoLiveV, VIDEO_LIVE_MAX_WIDTH, VIDEO_LIVE_MAX_HEIGHT, &iWidth, &iHeight, &uTimestamp);
   if ( 0 == iRes )
      return;

   if ( (iWidth != s_iVideoLiveWidth) || (iHeight != s_iVideoLiveHeight) )
   {
      log_line("[VideoLive] New video frame size: %dx%d", iWidth, iHeight);
      s_iVideoLiveWidth = iWidth;
      s_iVideoLiveHeight = iHeight;
   }

   // Convert I420 (full range ITU-R BT.601) to BGRA (cairo ARGB32 in memory
   // order, alpha = 255, not premultiplied since the frame is opaque).

   const u8* pY = s_pVideoLiveY;
   const u8* pU = s_pVideoLiveU;
   const u8* pV = s_pVideoLiveV;
   int iHalfWidth = iWidth / 2;

   for ( int y = 0; y < iHeight; y++ )
   {
      const u8* pRowY = pY + (long)y * iWidth;
      const u8* pRowU = pU + (long)(y / 2) * iHalfWidth;
      const u8* pRowV = pV + (long)(y / 2) * iHalfWidth;
      u32* pRowDst = s_pVideoLiveARGB[1 - s_iVideoLiveFrontIndex] + (long)y * iWidth;

      for ( int x = 0; x < iWidth; x++ )
      {
         int iY = pRowY[x];
         int iU = pRowU[x / 2] - 128;
         int iV = pRowV[x / 2] - 128;
         int iYr = 298 * (iY - 16);
         int iR = (iYr + 409 * iV + 128) >> 8;
         int iG = (iYr - 100 * iU - 208 * iV + 128) >> 8;
         int iB = (iYr + 516 * iU + 128) >> 8;
         if ( iR < 0 ) iR = 0;
         if ( iR > 255 ) iR = 255;
         if ( iG < 0 ) iG = 0;
         if ( iG > 255 ) iG = 255;
         if ( iB < 0 ) iB = 0;
         if ( iB > 255 ) iB = 255;
         pRowDst[x] = (u32)(0xFF000000u | ((u32)iR << 16) | ((u32)iG << 8) | (u32)iB);
      }
   }

   __sync_synchronize();
   s_iVideoLiveFrontIndex = 1 - s_iVideoLiveFrontIndex;
   s_bVideoLiveHasFrame = true;
}

u8* video_live_get_argb(int* piWidth, int* piHeight)
{
   if ( ! s_bVideoLiveHasFrame )
      return NULL;
   if ( NULL != piWidth )
      *piWidth = s_iVideoLiveWidth;
   if ( NULL != piHeight )
      *piHeight = s_iVideoLiveHeight;
   return (u8*)s_pVideoLiveARGB[s_iVideoLiveFrontIndex];
}
