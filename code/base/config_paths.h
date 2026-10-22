#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// The folders where the configuration of the application is stored.
//
// On the embedded platforms they are fixed folders of the file system. On the
// desktop build they are in the configuration folder of the user
// (~/.config/rubyfpv/), so that the settings of the application survive a
// restart of the computer and are not lost when the temporary folder is
// cleaned.
//
// The paths are resolved at runtime, because the home folder of the user is
// known only at runtime. The accessors below are used through the
// FOLDER_CONFIG, FOLDER_CONFIG_MODELS, FOLDER_OSD_PLUGINS and
// FOLDER_CORE_PLUGINS macros of config_file_names.h.
// ---------------------------------------------------------------------------

// The folder where the configuration files are stored.
const char* config_get_config_folder();

// The folder where the models (the vehicles) are stored.
const char* config_get_config_models_folder();

// The folder where the history of the models of a vehicle is stored.
const char* config_get_vehicle_history_folder(int iVehicleId);

// The folders where the OSD and the core plugins are stored.
const char* config_get_osd_plugins_folder();
const char* config_get_core_plugins_folder();

// Copies the settings of an older desktop installation (that were stored in
// the temporary folder) to the new location. The files that are already in
// the destination are never overwritten and the files of the source are never
// deleted. It returns the number of the files that were copied.
int config_migrate_legacy_desktop_config();

// Copies the files of a folder to another folder. Only the files are copied,
// the subfolders are not. The files that are already in the destination are
// not overwritten. It returns the number of the files that were copied.
int config_copy_settings_folder(const char* szSourceFolder, const char* szDestFolder);

#ifdef __cplusplus
}
#endif
