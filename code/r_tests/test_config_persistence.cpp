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

// The persistent configuration regression test.
//
// It checks that:
//   - the configuration of the application is stored in the configuration
//     folder of the user (~/.config/rubyfpv/) and not in the temporary
//     folder of the machine,
//   - the settings written with the real production code are read back by a
//     NEW process (a restart of the application), so that they survive a
//     restart,
//   - the settings of an old installation are migrated to the new folder
//     without overwriting the settings that are already there and without
//     deleting the old ones.
//
// The test uses a home folder of its own (a folder of the temporary folder,
// with the pid of the process in its name), so the configuration of the user
// of the machine is never touched.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../base/base.h"
#include "../base/config.h"
#include "../base/config_file_names.h"
#include "../base/config_paths.h"
#include "../base/radio_profiles.h"

static int s_iFailures = 0;
static int s_iChecks = 0;

static void check(const char* szName, int iGot, int iExpected)
{
   s_iChecks++;
   if ( iGot == iExpected )
      printf("  PASS  %-58s got=%d expect=%d\n", szName, iGot, iExpected);
   else
   {
      printf("  FAIL  %-58s got=%d expect=%d\n", szName, iGot, iExpected);
      s_iFailures++;
   }
}

static void checkStr(const char* szName, const char* szGot, const char* szExpected)
{
   s_iChecks++;
   if ( (NULL != szGot) && (NULL != szExpected) && (0 == strcmp(szGot, szExpected)) )
      printf("  PASS  %-58s got=[%s]\n", szName, szGot);
   else
   {
      printf("  FAIL  %-58s got=[%s] expect=[%s]\n", szName, (NULL != szGot) ? szGot : "(null)", (NULL != szExpected) ? szExpected : "(null)");
      s_iFailures++;
   }
}

// Creates a folder (and its parents).
static int _make_folder(const char* szPath)
{
   char szTmp[512];
   snprintf(szTmp, sizeof(szTmp)/sizeof(szTmp[0]), "%s", szPath);
   for( char* p = szTmp + 1; *p; p++ )
   {
      if ( '/' != *p )
         continue;
      *p = 0;
      mkdir(szTmp, 0755);
      *p = '/';
   }
   return (0 == mkdir(szTmp, 0755)) ? 1 : ((0 == access(szTmp, F_OK)) ? 1 : 0);
}

// Removes a file or a folder tree. It is used only to clean up the temporary
// folder of the test, that is created by the test itself.
static void _remove_tree(const char* szPath)
{
   DIR* pDir = opendir(szPath);
   if ( NULL != pDir )
   {
      struct dirent* pEntry = NULL;
      while ( NULL != (pEntry = readdir(pDir)) )
      {
         if ( '.' == pEntry->d_name[0] )
            continue;
         char szChild[1024];
         snprintf(szChild, sizeof(szChild)/sizeof(szChild[0]), "%s/%s", szPath, pEntry->d_name);
         struct stat oStat;
         if ( 0 != stat(szChild, &oStat) )
            continue;
         if ( S_ISDIR(oStat.st_mode) )
            _remove_tree(szChild);
         else
            remove(szChild);
      }
      closedir(pDir);
   }
   rmdir(szPath);
}

static int _write_file(const char* szFile, const char* szContent)
{
   FILE* fd = fopen(szFile, "w");
   if ( NULL == fd )
      return 0;
   fprintf(fd, "%s", szContent);
   fclose(fd);
   return 1;
}

static int _read_file(const char* szFile, char* szOutput, int iSize)
{
   szOutput[0] = 0;
   FILE* fd = fopen(szFile, "r");
   if ( NULL == fd )
      return 0;
   if ( NULL == fgets(szOutput, iSize, fd) )
   {
      fclose(fd);
      return 0;
   }
   fclose(fd);
   return 1;
}

// The child process: it is a NEW process, so the path of the configuration is
// resolved again, from the environment of the user. It reads the settings
// with the real production code.
static int _verify_after_restart()
{
   const char* szHome = getenv("HOME");
   if ( (NULL == szHome) || (0 == strlen(szHome)) )
   {
      printf("  FAIL  %-58s (no home folder in the environment)\n", "the restarted process has a home folder");
      return 1;
   }

   const char* szConfig = config_get_config_folder();
   printf("STEP: the restarted process uses the configuration folder: [%s]\n", szConfig);

   char szExpected[512];
   snprintf(szExpected, sizeof(szExpected)/sizeof(szExpected[0]), "%s/.config/rubyfpv/", szHome);
   if ( 0 != strcmp(szConfig, szExpected) )
   {
      printf("  FAIL  %-58s got=[%s] expect=[%s]\n", "the restarted process uses the folder of the user", szConfig, szExpected);
      return 1;
   }
   printf("  PASS  %-58s got=[%s]\n", "the restarted process uses the folder of the user", szConfig);

   radio_profiles_init();
   int iCount = radio_profiles_load();
   printf("STEP: the restarted process loaded %d RF profile(s)\n", iCount);

   int iIndex = radio_profiles_find_by_name("PersistenceTestProfile");
   if ( iIndex < 0 )
   {
      printf("  FAIL  %-58s (the profile was not found)\n", "the settings are read back after a restart");
      return 1;
   }
   printf("  PASS  %-58s got=%d\n", "the settings are read back after a restart", iIndex);

   const radio_profile_t* pProfile = radio_profiles_get(iIndex);
   if ( (NULL == pProfile) || (42 != pProfile->iChannel) )
   {
      printf("  FAIL  %-58s got=%d expect=42\n", "the value of the settings is the saved one", (NULL != pProfile) ? pProfile->iChannel : -1);
      return 1;
   }
   printf("  PASS  %-58s got=%d\n", "the value of the settings is the saved one", pProfile->iChannel);

   return 0;
}

int main(int argc, char* argv[])
{
   if ( (argc > 1) && (0 == strcmp(argv[1], "--verify-restart")) )
      return _verify_after_restart();

   printf("== Persistent configuration test ==\n");

   const char* szRealHome = getenv("HOME");
   printf("STEP: the home folder of the user is: [%s]\n", (NULL != szRealHome) ? szRealHome : "(none)");

   // The test uses a home folder of its own, so that the configuration of the
   // user of the machine is never touched.
   char szTestHome[512];
   snprintf(szTestHome, sizeof(szTestHome)/sizeof(szTestHome[0]), "/tmp/rubyfpv_cfg_test_%d", (int)getpid());
   _remove_tree(szTestHome);
   _make_folder(szTestHome);
   setenv("HOME", szTestHome, 1);

   // 1) The configuration is in the configuration folder of the user
   const char* szConfig = config_get_config_folder();
   printf("STEP: the configuration folder is: [%s]\n", szConfig);

   char szExpected[512];
   snprintf(szExpected, sizeof(szExpected)/sizeof(szExpected[0]), "%s/.config/rubyfpv/", szTestHome);
   checkStr("the configuration folder is in the home folder of the user", szConfig, szExpected);
   #if defined(HW_PLATFORM_LINUX_GENERIC)
   // The settings are not in the temporary folder of the application, where
   // they were stored by the old desktop installation.
   check("the configuration folder is not in the temporary folder of the app",
      (0 == strncmp(szConfig, FOLDER_CONFIG_LEGACY, strlen(FOLDER_CONFIG_LEGACY))) ? 0 : 1, 1);
   #endif
   check("the configuration folder was created", (0 == access(szConfig, F_OK)) ? 1 : 0, 1);

   // The models and the plugins are in the same place
   const char* szModels = config_get_config_models_folder();
   char szExpectedModels[512];
   snprintf(szExpectedModels, sizeof(szExpectedModels)/sizeof(szExpectedModels[0]), "%smodels/", szExpected);
   checkStr("the models folder is in the home folder of the user", szModels, szExpectedModels);
   check("the models folder was created", (0 == access(szModels, F_OK)) ? 1 : 0, 1);

   const char* szOsdPlugins = config_get_osd_plugins_folder();
   char szExpectedOsd[512];
   snprintf(szExpectedOsd, sizeof(szExpectedOsd)/sizeof(szExpectedOsd[0]), "%splugins/osd/", szExpected);
   checkStr("the osd plugins folder is in the home folder of the user", szOsdPlugins, szExpectedOsd);

   const char* szCorePlugins = config_get_core_plugins_folder();
   char szExpectedCore[512];
   snprintf(szExpectedCore, sizeof(szExpectedCore)/sizeof(szExpectedCore[0]), "%splugins/core/", szExpected);
   checkStr("the core plugins folder is in the home folder of the user", szCorePlugins, szExpectedCore);

   const char* szHistory = config_get_vehicle_history_folder(7);
   char szExpectedHistory[512];
   snprintf(szExpectedHistory, sizeof(szExpectedHistory)/sizeof(szExpectedHistory[0]), "%smodels/history-7/", szExpected);
   checkStr("the history folder of a vehicle is in the home folder", szHistory, szExpectedHistory);

   // 2) A setting is written with the real production code
   printf("STEP: the settings are written with the production code\n");
   radio_profiles_init();
   radio_profile_t oProfile;
   radio_profiles_get_default_profile(&oProfile);
   snprintf(oProfile.szName, sizeof(oProfile.szName), "%s", "PersistenceTestProfile");
   oProfile.iChannel = 42;
   oProfile.iEnabled = 1;
   int iIndex = radio_profiles_add(&oProfile);
   check("the profile was added", (iIndex >= 0) ? 1 : 0, 1);
   check("the profile was saved", radio_profiles_save(), 1);

   const char* szProfilesFile = radio_profiles_get_file_path();
   printf("STEP: the settings file is: [%s]\n", szProfilesFile);
   check("the settings file is in the home folder of the user", (NULL != strstr(szProfilesFile, szTestHome)) ? 1 : 0, 1);
   check("the settings file exists", (0 == access(szProfilesFile, R_OK)) ? 1 : 0, 1);

   // 3) A NEW process (a restart of the application) reads the settings back
   printf("STEP: the settings are read back by a new process\n");
   pid_t iChild = fork();
   if ( 0 == iChild )
   {
      // The child is a new process: the path of the configuration is resolved
      // again, from the environment of the user.
      execl("/proc/self/exe", "test_config_persistence", "--verify-restart", (char*)NULL);
      _exit(127);
   }
   check("the restarted process was started", (iChild > 0) ? 1 : 0, 1);
   if ( iChild > 0 )
   {
      int iStatus = 0;
      waitpid(iChild, &iStatus, 0);
      check("the settings survived the restart", (WIFEXITED(iStatus) && (0 == WEXITSTATUS(iStatus))) ? 1 : 0, 1);
   }

   // 4) The settings of an old installation are migrated, without overwriting
   printf("STEP: the settings of an old installation are migrated\n");
   char szLegacy[512];
   char szMigrated[512];
   snprintf(szLegacy, sizeof(szLegacy)/sizeof(szLegacy[0]), "/tmp/rubyfpv_cfg_legacy_%d", (int)getpid());
   snprintf(szMigrated, sizeof(szMigrated)/sizeof(szMigrated[0]), "/tmp/rubyfpv_cfg_migrated_%d", (int)getpid());
   _remove_tree(szLegacy);
   _remove_tree(szMigrated);
   _make_folder(szLegacy);
   _make_folder(szMigrated);

   char szOldFile[600];
   char szNewFile[600];
   snprintf(szOldFile, sizeof(szOldFile)/sizeof(szOldFile[0]), "%s/old_setting.cfg", szLegacy);
   snprintf(szNewFile, sizeof(szNewFile)/sizeof(szNewFile[0]), "%s/old_setting.cfg", szMigrated);
   _write_file(szOldFile, "OLDVALUE");
   // A setting that is already in the new place must not be overwritten
   char szKeptFile[600];
   snprintf(szKeptFile, sizeof(szKeptFile)/sizeof(szKeptFile[0]), "%s/kept_setting.cfg", szMigrated);
   _write_file(szKeptFile, "KEPTVALUE");
   char szKeptOld[600];
   snprintf(szKeptOld, sizeof(szKeptOld)/sizeof(szKeptOld[0]), "%s/kept_setting.cfg", szLegacy);
   _write_file(szKeptOld, "OLDVALUE");

   int iCopied = config_copy_settings_folder(szLegacy, szMigrated);
   check("the settings of the old installation were copied", (iCopied > 0) ? 1 : 0, 1);

   char szContent[256];
   check("the copied setting is in the new place", _read_file(szNewFile, szContent, sizeof(szContent)), 1);
   checkStr("the copied setting has the right content", szContent, "OLDVALUE");

   check("the old setting was not deleted", (0 == access(szOldFile, R_OK)) ? 1 : 0, 1);

   _read_file(szKeptFile, szContent, sizeof(szContent));
   checkStr("the setting that was already there was not overwritten", szContent, "KEPTVALUE");

   // 5) The real migration of the desktop installation does not delete the
   //    files of the old folder
   int iMigrated = config_migrate_legacy_desktop_config();
   printf("STEP: the migration of the old desktop installation copied %d file(s)\n", iMigrated);
   check("the migration of the old desktop installation did not fail", (iMigrated >= 0) ? 1 : 0, 1);

   // Clean up the temporary folders of the test
   _remove_tree(szLegacy);
   _remove_tree(szMigrated);
   _remove_tree(szTestHome);

   printf("== Persistent configuration test: %d checks, %d failures ==\n", s_iChecks, s_iFailures);
   return (0 == s_iFailures) ? 0 : 1;
}
