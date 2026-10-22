#pragma once

/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright notice,
          this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright notice,
          this list of conditions and the following disclaimer in the documentation
          and/or other materials provided with the distribution.
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

// The compatibility of the software version of a vehicle with the software
// version of the controller.
//
// The version of the vehicle is not always fully known: the telemetry stream
// carries only the major and the minor version of the vehicle (the version
// byte has 4 bits for the major and 4 bits for the minor), so the build
// number of the vehicle is known only after the full model settings have been
// received from it. A vehicle whose build number is unknown must not be
// declared incompatible: the ground station would then show the "update the
// vehicle" screen instead of the video.

#include "base.h"

// The build number of the first version of the vehicle software that speaks
// the current video protocol.
#define MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL 11503

// The version of the controller, in the same format as the version of the
// vehicle: major << 8 | minor | build << 16
#define CONTROLLER_FULL_SW_VERSION ( (((u32)SYSTEM_SW_BUILD_NUMBER) << 16) | (((u32)SYSTEM_SW_VERSION_MAJOR) << 8) | ((u32)SYSTEM_SW_VERSION_MINOR) )

// The result of the comparison between the software version of the vehicle
// and the software version of the controller.
enum
{
   VEHICLE_SW_VERSION_COMPATIBLE = 0,   // the vehicle speaks the current video protocol
   VEHICLE_SW_VERSION_OLDER = 1,        // the vehicle is too old, it must be updated
   VEHICLE_SW_VERSION_NEWER = 2         // the vehicle is newer than the controller, the controller must be updated
};

// Compares the software version of a vehicle with the software version of the
// controller.
//
// The build number of the vehicle is used when it is known. When it is not
// known (the vehicle sent only its major and minor version in the telemetry
// stream, the full model settings were not received yet), the major and the
// minor version are compared instead: the vehicle is compatible if it is not
// older than the first version that speaks the current video protocol.
static inline int classify_vehicle_software_version(u32 uVehicleSwVersion)
{
   u32 uVehicleBuild = uVehicleSwVersion >> 16;
   u32 uControllerBuild = CONTROLLER_FULL_SW_VERSION >> 16;

   if ( (uVehicleBuild > 0) && (uControllerBuild > 0) )
   {
      if ( uVehicleBuild > uControllerBuild )
         return VEHICLE_SW_VERSION_NEWER;
      if ( uVehicleBuild < MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL )
         return VEHICLE_SW_VERSION_OLDER;
      return VEHICLE_SW_VERSION_COMPATIBLE;
   }

   u32 uVehicleMajor = (uVehicleSwVersion >> 8) & 0xFF;
   u32 uVehicleMinor = uVehicleSwVersion & 0xFF;
   u32 uControllerMajor = (CONTROLLER_FULL_SW_VERSION >> 8) & 0xFF;
   u32 uControllerMinor = CONTROLLER_FULL_SW_VERSION & 0xFF;
   u32 uMinMajor = MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL / 1000;
   u32 uMinMinor = (MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL / 100) % 10;

   if ( (uVehicleMajor > uControllerMajor) || ((uVehicleMajor == uControllerMajor) && (uVehicleMinor > uControllerMinor)) )
      return VEHICLE_SW_VERSION_NEWER;
   if ( (uVehicleMajor < uMinMajor) || ((uVehicleMajor == uMinMajor) && (uVehicleMinor < uMinMinor)) )
      return VEHICLE_SW_VERSION_OLDER;
   return VEHICLE_SW_VERSION_COMPATIBLE;
}
