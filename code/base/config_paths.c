/*
    Ruby Licence
    Copyright (c) 2020-2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source code and/or binary forms, with or
    without modification, are permitted provided that the following conditions are met:
        * Redistributions of use of source code (partially or complete) must retain
          the above copyright notice, this list of conditions and the following disclaimer
          in the documentation and/or other materials provided with the distribution.
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

#include "config_paths.h"
#include "config_file_names.h"
#include "base.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// The size of the buffers where the paths are built. The paths are copied by
// the callers in buffers of MAX_FILE_PATH_SIZE bytes, so a path that does not
// fit in that size is never returned.
#define CONFIG_PATH_SIZE 256

static char s_szConfigFolder[CONFIG_PATH_SIZE] = {0};
static char s_szConfigModelsFolder[CONFIG_PATH_SIZE] = {0};
static char s_szOsdPluginsFolder[CONFIG_PATH_SIZE] = {0};
static char s_szCorePluginsFolder[CONFIG_PATH_SIZE] = {0};
static char s_szVehicleHistoryFolder[CONFIG_PATH_SIZE] = {0};
static int s_bFoldersResolved = 0;

// Creates a folder and all its parent folders, if they do not exist. The
// permissions of the new folders are the default ones of the system, the
// permissions of the existing folders are not changed.
static void _config_make_folder(const char* szPath)
{
   if ( (NULL == szPath) || (0 == strlen(szPath)) )
      return;

   char szTmp[CONFIG_PATH_SIZE];
   snprintf(szTmp, sizeof(szTmp)/sizeof(szTmp[0]), "%s", szPath);

   for( char* p = szTmp + 1; *p; p++ )
   {
      if ( '/' != *p )
         continue;
      *p = 0;
      mkdir(szTmp, 0755);
      *p = '/';
   }
   mkdir(szTmp, 0755);
}

// Returns the home folder of the user that runs the application, or NULL if
// it can not be found.
static const char* _config_get_home_folder()
{
   const char* szHome = getenv("HOME");
   if ( (NULL != szHome) && (0 != strlen(szHome)) )
      return szHome;

   struct passwd* pPasswd = getpwuid(getuid());
   if ( NULL != pPasswd )
      if ( (NULL != pPasswd->pw_dir) && (0 != strlen(pPasswd->pw_dir)) )
         return pPasswd->pw_dir;

   return NULL;
}

static void _config_resolve_folders()
{
   if ( s_bFoldersResolved )
      return;
   s_bFoldersResolved = 1;

#if defined(HW_PLATFORM_LINUX_GENERIC)

   // The desktop build stores the configuration in the configuration folder of
   // the user, so that the settings survive a restart of the computer and are
   // not lost when the temporary folder is cleaned.
   const char* szHome = _config_get_home_folder();
   if ( NULL == szHome )
   {
      // Without a home folder there is no place where the settings could be
      // kept between two runs of the application. The old temporary folder is
      // used, so that the application still works, but the settings are lost
      // when the computer is restarted.
      log_line("Could not find the home folder of the user: the settings of the application are NOT stored permanently (they are stored in %s).", FOLDER_CONFIG_LEGACY);
      snprintf(s_szConfigFolder, sizeof(s_szConfigFolder)/sizeof(s_szConfigFolder[0]), "%s", FOLDER_CONFIG_LEGACY);
   }
   else
   {
      snprintf(s_szConfigFolder, sizeof(s_szConfigFolder)/sizeof(s_szConfigFolder[0]), "%s/.config/rubyfpv/", szHome);
      if ( strlen(s_szConfigFolder) >= MAX_FILE_PATH_SIZE )
      {
         // The path is too long for the buffers of the application. The
         // temporary folder is used instead and it is said clearly that the
         // settings are not stored permanently.
         log_line("The path of the configuration folder (%s) is too long: the settings of the application are NOT stored permanently (they are stored in %s).", s_szConfigFolder, FOLDER_CONFIG_LEGACY);
         snprintf(s_szConfigFolder, sizeof(s_szConfigFolder)/sizeof(s_szConfigFolder[0]), "%s", FOLDER_CONFIG_LEGACY);
      }
   }

   snprintf(s_szConfigModelsFolder, sizeof(s_szConfigModelsFolder)/sizeof(s_szConfigModelsFolder[0]), "%smodels/", s_szConfigFolder);
   snprintf(s_szOsdPluginsFolder, sizeof(s_szOsdPluginsFolder)/sizeof(s_szOsdPluginsFolder[0]), "%splugins/osd/", s_szConfigFolder);
   snprintf(s_szCorePluginsFolder, sizeof(s_szCorePluginsFolder)/sizeof(s_szCorePluginsFolder[0]), "%splugins/core/", s_szConfigFolder);

#else

   // The embedded platforms have fixed folders for the configuration.
   snprintf(s_szConfigFolder, sizeof(s_szConfigFolder)/sizeof(s_szConfigFolder[0]), "%s", FOLDER_CONFIG_PATH);
   snprintf(s_szConfigModelsFolder, sizeof(s_szConfigModelsFolder)/sizeof(s_szConfigModelsFolder[0]), "%s", FOLDER_CONFIG_MODELS_PATH);
   snprintf(s_szOsdPluginsFolder, sizeof(s_szOsdPluginsFolder)/sizeof(s_szOsdPluginsFolder[0]), "%s", FOLDER_OSD_PLUGINS_PATH);
   snprintf(s_szCorePluginsFolder, sizeof(s_szCorePluginsFolder)/sizeof(s_szCorePluginsFolder[0]), "%s", FOLDER_CORE_PLUGINS_PATH);

#endif

   _config_make_folder(s_szConfigFolder);
   _config_make_folder(s_szConfigModelsFolder);
   _config_make_folder(s_szOsdPluginsFolder);
   _config_make_folder(s_szCorePluginsFolder);
}

const char* config_get_config_folder()
{
   _config_resolve_folders();
   return s_szConfigFolder;
}

const char* config_get_config_models_folder()
{
   _config_resolve_folders();
   return s_szConfigModelsFolder;
}

const char* config_get_vehicle_history_folder(int iVehicleId)
{
   _config_resolve_folders();
   snprintf(s_szVehicleHistoryFolder, sizeof(s_szVehicleHistoryFolder)/sizeof(s_szVehicleHistoryFolder[0]),
      "%smodels/history-%d/", s_szConfigFolder, iVehicleId);
   return s_szVehicleHistoryFolder;
}

const char* config_get_osd_plugins_folder()
{
   _config_resolve_folders();
   return s_szOsdPluginsFolder;
}

const char* config_get_core_plugins_folder()
{
   _config_resolve_folders();
   return s_szCorePluginsFolder;
}

// Copies one file. The copy is made in a temporary file that is renamed at
// the end, so that a file is never left partially written.
static int _config_copy_file(const char* szSource, const char* szDest)
{
   FILE* fdSource = fopen(szSource, "rb");
   if ( NULL == fdSource )
      return 0;

   char szTempFile[CONFIG_PATH_SIZE];
   snprintf(szTempFile, sizeof(szTempFile)/sizeof(szTempFile[0]), "%s.tmp", szDest);

   FILE* fdDest = fopen(szTempFile, "wb");
   if ( NULL == fdDest )
   {
      fclose(fdSource);
      return 0;
   }

   char szBuff[4096];
   size_t uRead = 0;
   while ( 0 < (uRead = fread(szBuff, 1, sizeof(szBuff), fdSource)) )
   {
      if ( uRead != fwrite(szBuff, 1, uRead, fdDest) )
      {
         fclose(fdSource);
         fclose(fdDest);
         remove(szTempFile);
         return 0;
      }
   }
   fclose(fdSource);
   fflush(fdDest);
   fclose(fdDest);

   if ( 0 != rename(szTempFile, szDest) )
   {
      remove(szTempFile);
      return 0;
   }

   return 1;
}

int config_copy_settings_folder(const char* szSourceFolder, const char* szDestFolder)
{
   if ( (NULL == szSourceFolder) || (NULL == szDestFolder) )
      return 0;
   if ( (0 == strlen(szSourceFolder)) || (0 == strlen(szDestFolder)) )
      return 0;

   DIR* pDir = opendir(szSourceFolder);
   if ( NULL == pDir )
      return 0;

   _config_make_folder(szDestFolder);

   // The separator is added if the folder does not end with one.
   char szSourceFolderPath[CONFIG_PATH_SIZE];
   char szDestFolderPath[CONFIG_PATH_SIZE];
   snprintf(szSourceFolderPath, sizeof(szSourceFolderPath)/sizeof(szSourceFolderPath[0]), "%s", szSourceFolder);
   if ( ('/' != szSourceFolderPath[strlen(szSourceFolderPath)-1]) && (0 < strlen(szSourceFolderPath)) )
      strncat(szSourceFolderPath, "/", CONFIG_PATH_SIZE - strlen(szSourceFolderPath) - 1);
   snprintf(szDestFolderPath, sizeof(szDestFolderPath)/sizeof(szDestFolderPath[0]), "%s", szDestFolder);
   if ( ('/' != szDestFolderPath[strlen(szDestFolderPath)-1]) && (0 < strlen(szDestFolderPath)) )
      strncat(szDestFolderPath, "/", CONFIG_PATH_SIZE - strlen(szDestFolderPath) - 1);

   int iCopied = 0;
   struct dirent* pEntry = NULL;
   while ( NULL != (pEntry = readdir(pDir)) )
   {
      if ( '.' == pEntry->d_name[0] )
         continue;

      char szSource[CONFIG_PATH_SIZE];
      char szDest[CONFIG_PATH_SIZE];
      snprintf(szSource, sizeof(szSource)/sizeof(szSource[0]), "%s%s", szSourceFolderPath, pEntry->d_name);
      snprintf(szDest, sizeof(szDest)/sizeof(szDest[0]), "%s%s", szDestFolderPath, pEntry->d_name);

      struct stat oFileStat;
      if ( 0 != stat(szSource, &oFileStat) )
         continue;
      if ( ! S_ISREG(oFileStat.st_mode) )
         continue;

      // The files that are already in the destination are never overwritten.
      if ( 0 == access(szDest, F_OK) )
         continue;

      if ( _config_copy_file(szSource, szDest) )
         iCopied++;
   }
   closedir(pDir);

   return iCopied;
}

int config_migrate_legacy_desktop_config()
{
#if defined(HW_PLATFORM_LINUX_GENERIC)

   const char* szDest = config_get_config_folder();
   if ( 0 == strcmp(FOLDER_CONFIG_LEGACY, szDest) )
      return 0;

   int iCopied = config_copy_settings_folder(FOLDER_CONFIG_LEGACY, szDest);

   // The models and the plugins of the old installation are migrated too.
   iCopied += config_copy_settings_folder(FOLDER_CONFIG_MODELS_LEGACY, config_get_config_models_folder());
   iCopied += config_copy_settings_folder(FOLDER_OSD_PLUGINS_LEGACY, config_get_osd_plugins_folder());
   iCopied += config_copy_settings_folder(FOLDER_CORE_PLUGINS_LEGACY, config_get_core_plugins_folder());

   if ( iCopied > 0 )
      log_line("Migrated %d settings file(s) from the old folder %s to %s (the files of the old folder were not deleted).", iCopied, FOLDER_CONFIG_LEGACY, szDest);

   return iCopied;

#else
   return 0;
#endif
}
