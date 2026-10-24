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

// Hardware independent test for the decision to pair a vehicle from its
// ruby telemetry packets, when the radio interfaces of the controller can not
// send any RF packet back to the vehicle (they are receive-only).
//
// The test exists because of a real defect: on such a controller the vehicle
// never answers (it can not even receive) a Ruby pairing request, so it never
// sends a Ruby pairing confirmation packet, and the router kept the vehicle
// in the "pairing in progress" state forever. The video stream of the vehicle
// was received but dropped, because it only displays the video of a paired
// vehicle.
//
// The test uses the same code as the controller router
// ("r_station/pairing_telemetry.h"), it needs no radio and no vehicle.

#include "../base/base.h"
#include "../r_station/pairing_telemetry.h"
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
      printf("  PASS  %-56s got=%d\n", szName, iGot);
      return;
   }
   s_iFailures++;
   printf("  FAIL  %-56s got=%d expected=%d\n", szName, iGot, iExpected);
}

int main()
{
   printf("\n== Test: the pairing of a vehicle from its telemetry ==\n");

   // The real vehicle of the user: a fresh telemetry stream, on a
   // controller whose radio interfaces can not send packets.

   _check("a fresh ruby telemetry pairs the vehicle",
      pairing_telemetry_can_pair_vehicle(false, 2172108829, 2172108829, 0xB8, 1000, 1200), PAIRING_TELEMETRY_RESULT_PAIR_VEHICLE);

   _check("the telemetry that is exactly at the freshness limit pairs",
      pairing_telemetry_can_pair_vehicle(false, 99, 99, 0xB8, 1000, 1000 + PAIRING_TELEMETRY_MAX_FRESHNESS_MS), PAIRING_TELEMETRY_RESULT_PAIR_VEHICLE);

   _check("a telemetry that is too old does not pair",
      pairing_telemetry_can_pair_vehicle(false, 99, 99, 0xB8, 1000, 1000 + PAIRING_TELEMETRY_MAX_FRESHNESS_MS + 1), PAIRING_TELEMETRY_RESULT_EPAIRING_TELEMETRY_OLD);

   _check("a vehicle that is already paired does not pair again",
      pairing_telemetry_can_pair_vehicle(true, 99, 99, 0xB8, 1000, 1200), PAIRING_TELEMETRY_RESULT_EPAIRING_DONE);

   _check("a vehicle without telemetry does not pair",
      pairing_telemetry_can_pair_vehicle(false, 99, 99, 0xB8, 0, 1200), PAIRING_TELEMETRY_RESULT_EPAIRING_NO_TELEMETRY);

   // The version of the vehicle is zero only if its telemetry is not from a
   // Ruby-like software that we fully understand. Pairing on it would claim
   // a compatibility that we can not verify.
   _check("a telemetry without a ruby software version does not pair",
      pairing_telemetry_can_pair_vehicle(false, 99, 99, 0, 1000, 1200), PAIRING_TELEMETRY_RESULT_EPAIRING_UNKNOWN_SOFTWARE);

   // A telemetry packet from a different vehicle must never pair the model
   // on the current frequency of the controller.
   _check("the telemetry of another vehicle does not pair",
      pairing_telemetry_can_pair_vehicle(false, 2172108829, 111, 0xB8, 1000, 1200), PAIRING_TELEMETRY_RESULT_EPAIRING_VEHICLE_MISMATCH);
   _check("a telemetry without vehicle id does not pair",
      pairing_telemetry_can_pair_vehicle(false, 99, 0, 0xB8, 1000, 1200), PAIRING_TELEMETRY_RESULT_EPAIRING_VEHICLE_MISMATCH);

   // The time of the telemetry must never be interpreted as in the future
   // (a wrap-around of the clock is not a fresh telemetry).
   _check("a telemetry time in the future does not pair",
      pairing_telemetry_can_pair_vehicle(false, 99, 99, 0xB8, 4000000001, 1000), PAIRING_TELEMETRY_RESULT_EPAIRING_TELEMETRY_OLD);

   printf("\nResult: %d checks, %d failures\n", s_iTests, s_iFailures);
   if ( 0 == s_iFailures )
      printf("PASSED\n");
   else
   {
      printf("FAILED\n");
      return 1;
   }
   return 0;
}
