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

#include "pairing_telemetry.h"

int pairing_telemetry_can_pair_vehicle(bool bIsPairingDone, u32 uVehicleIdExpected, u32 uTelemetryVehicleId, u8 uRubyVersionByte, u32 uTimeMsLastTelemetry, u32 uTimeNowMs)
{
   if ( bIsPairingDone )
      return PAIRING_TELEMETRY_RESULT_EPAIRING_DONE;
   if ( 0 == uTimeMsLastTelemetry )
      return PAIRING_TELEMETRY_RESULT_EPAIRING_NO_TELEMETRY;
   // A time of the telemetry that is in the future (compared to the current
   // time of the router) is not a fresh telemetry, it is a clock that
   // wrapped around or a corrupted packet.
   if ( (uTimeNowMs < uTimeMsLastTelemetry) || (uTimeNowMs - uTimeMsLastTelemetry > PAIRING_TELEMETRY_MAX_FRESHNESS_MS) )
      return PAIRING_TELEMETRY_RESULT_EPAIRING_TELEMETRY_OLD;
   if ( 0 == uRubyVersionByte )
      return PAIRING_TELEMETRY_RESULT_EPAIRING_UNKNOWN_SOFTWARE;
   if ( (0 == uVehicleIdExpected) || (uVehicleIdExpected != uTelemetryVehicleId) )
      return PAIRING_TELEMETRY_RESULT_EPAIRING_VEHICLE_MISMATCH;
   return PAIRING_TELEMETRY_RESULT_PAIR_VEHICLE;
}
