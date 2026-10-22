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

// Hardware independent test for the comparison between the software version of
// a vehicle and the software version of the controller.
//
// The test exists because of a real defect: the version of the vehicle is
// known only as major and minor from the telemetry stream (the build number
// is 0 until the full model settings are received), and the ground station
// used to compare the whole version word, so it declared a vehicle that was
// 11.8 as incompatible and showed the "update the vehicle" screen instead of
// the video.
//
// The test uses the same code as the ground station
// ("base/sw_version_compat.h"), it needs no radio and no vehicle.

#include "../base/base.h"
#include "../base/sw_version_compat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int s_iTests = 0;
static int s_iFailures = 0;

static void _check(const char* szName, int iGot, int iExpected)
{
   s_iTests++;
   if ( iGot == iExpected )
   {
      printf("  PASS  %-60s got=%d\n", szName, iGot);
      return;
   }
   s_iFailures++;
   printf("  FAIL  %-60s got=%d expected=%d\n", szName, iGot, iExpected);
}

// Builds the version word of a vehicle: major << 8 | minor | build << 16
static u32 _version(int iMajor, int iMinor, int iBuild)
{
   return (((u32)iBuild) << 16) | (((u32)iMajor) << 8) | ((u32)iMinor);
}

int main()
{
   printf("\n== Test: the compatibility of the software version of a vehicle ==\n");

   // The version of the controller of this build
   _check("the version of the controller", (int)CONTROLLER_FULL_SW_VERSION, (int)_version(SYSTEM_SW_VERSION_MAJOR, SYSTEM_SW_VERSION_MINOR, SYSTEM_SW_BUILD_NUMBER));

   // The real vehicle of the user: 11.8, build 11801 (11.8.01)
   _check("the vehicle 11.8 build 11801", classify_vehicle_software_version(_version(11, 8, 11801)), VEHICLE_SW_VERSION_COMPATIBLE);

   // The same vehicle, seen from the telemetry stream: the build number is
   // not known (0). This is the case that produced the false "firmware
   // mismatch" complaint.
   _check("the vehicle 11.8 with an unknown build", classify_vehicle_software_version(_version(11, 8, 0)), VEHICLE_SW_VERSION_COMPATIBLE);

   // A vehicle of the same major and minor, with a build number that is
   // lower than the one of the controller, but that speaks the current video
   // protocol
   _check("the vehicle 11.8 build 11503", classify_vehicle_software_version(_version(11, 8, 11503)), VEHICLE_SW_VERSION_COMPATIBLE);
   _check("the vehicle 11.8 build 11600", classify_vehicle_software_version(_version(11, 8, 11600)), VEHICLE_SW_VERSION_COMPATIBLE);

   // A vehicle that is really too old: it must be told to update
   _check("the vehicle 11.4 build 11401", classify_vehicle_software_version(_version(11, 4, 11401)), VEHICLE_SW_VERSION_OLDER);
   _check("the vehicle 11.5 build 11502", classify_vehicle_software_version(_version(11, 5, 11502)), VEHICLE_SW_VERSION_OLDER);
   _check("the vehicle 10.9 with an unknown build", classify_vehicle_software_version(_version(10, 9, 0)), VEHICLE_SW_VERSION_OLDER);
   _check("the vehicle 11.4 with an unknown build", classify_vehicle_software_version(_version(11, 4, 0)), VEHICLE_SW_VERSION_OLDER);

   // A vehicle that is newer than the controller: the controller must be
   // updated
   _check("the vehicle 11.9 build 11901", classify_vehicle_software_version(_version(11, 9, 11901)), VEHICLE_SW_VERSION_NEWER);
   _check("the vehicle 12.0 build 12001", classify_vehicle_software_version(_version(12, 0, 12001)), VEHICLE_SW_VERSION_NEWER);
   _check("the vehicle 11.9 with an unknown build", classify_vehicle_software_version(_version(11, 9, 0)), VEHICLE_SW_VERSION_NEWER);
   _check("the vehicle 12.0 with an unknown build", classify_vehicle_software_version(_version(12, 0, 0)), VEHICLE_SW_VERSION_NEWER);

   // The edge of the unknown build number: the minor version is the limit
   _check("the vehicle 11.5 with an unknown build", classify_vehicle_software_version(_version(11, 5, 0)), VEHICLE_SW_VERSION_COMPATIBLE);
   _check("the vehicle 11.4 with an unknown build (edge)", classify_vehicle_software_version(_version(11, 4, 0)), VEHICLE_SW_VERSION_OLDER);
   _check("the vehicle 11.8 with an unknown build (edge)", classify_vehicle_software_version(_version(11, 8, 0)), VEHICLE_SW_VERSION_COMPATIBLE);
   _check("the vehicle 11.9 with an unknown build (edge)", classify_vehicle_software_version(_version(11, 9, 0)), VEHICLE_SW_VERSION_NEWER);

   // The first build that speaks the current video protocol
   _check("the minimum build for the current protocol", (int)MIN_BUILD_FOR_CURRENT_VIDEO_PROTOCOL, 11503);

   printf("\n=========================================================\n");
   printf("Tests: %d, failures: %d\n", s_iTests, s_iFailures);
   if ( 0 == s_iFailures )
      printf("ALL TESTS PASSED\n");
   else
      printf("TEST FAILURES\n");
   printf("=========================================================\n");
   return ( 0 == s_iFailures ) ? 0 : 1;
}
