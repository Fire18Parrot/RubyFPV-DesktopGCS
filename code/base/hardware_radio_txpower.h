#pragma once

// Driver specific raw TX power setters.
//
// The setters apply a raw TX power index to a radio interface. They return 1 on
// success and 0 on failure, so a caller can report an unsupported card or a
// failed command instead of only logging it.

#ifdef __cplusplus
extern "C" {
#endif

int hardware_radio_set_txpower_raw_rtl8812au(int iCardIndex, int iTxPower);
int hardware_radio_set_txpower_raw_rtl8812eu(int iCardIndex, int iTxPower);
int hardware_radio_set_txpower_raw_rtl8733bu(int iCardIndex, int iTxPower);
int hardware_radio_set_txpower_raw_atheros(int iCardIndex, int iTxPower);

// Decides if a radio interface belongs to the family of a driver, so a raw
// TX power setter is applied only to the cards it is meant for.
//
// iDriverFilter is the driver the command is meant for, one of the
// RADIO_HW_DRIVER_* constants. The family helpers (hardware_radio_driver_is_*)
// decide if the card is a member of the family of that driver, so all the
// members of a family are handled and not only the exact driver.
// If bAllowRalink is set, the Ralink cards are handled too, because they are
// configured with the same command.
//
// Returns 1 if the card is handled by the command, 0 if it is not.
//
// It is exposed so the decision can be tested without a radio attached.
int hardware_radio_txpower_driver_matches(int iDriverFilter, int iCardDriver, int iCardType, int bAllowRalink);

// Decides if a raw TX power command succeeded.
//
// iCommandResult is the result of the command runner: 1 if the command ran
// and exited with the status 0, 0 if the command could not be started or
// closed and -1 if the output read timed out.
// szOutput is the raw output of the command.
//
// Returns 1 only if the command ran, exited with the status 0 and did not
// report an error in its output. A command that could not be started, that
// timed out or that reported an error is never a success.
//
// It is exposed so the decision can be tested without a radio attached.
int hardware_radio_txpower_command_succeeded(int iCommandResult, const char* szOutput);

#ifdef __cplusplus
}
#endif
