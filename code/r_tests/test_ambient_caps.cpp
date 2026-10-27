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

// The regression test of the ambient capabilities of the ground station.
//
// The ground station is not run as root: it gets CAP_NET_ADMIN and
// CAP_NET_RAW from the file capabilities of its binaries, in a runtime
// directory. A child process does not inherit the effective set of its
// parent: at execve() the effective set of the child is empty, and only the
// capabilities that are in the inheritable set and that are raised as
// ambient are kept. The helper that the ground station runs to configure its
// radios ("iw") therefore ran without CAP_NET_ADMIN, and every attempt to
// tune the radio failed with "RTNETLINK answers: Operation not permitted".
//
// The test calls the real function of the ground station
// ("hardware_raise_ambient_capabilities()") and it checks that a child
// process inherits the capabilities.
//
// The test needs no privilege: without capabilities it checks that the
// function is harmless. To check the inheritance with real capabilities, run
// it in a user namespace that has a capability (this uses setpriv; the
// "capsh --keep=1 --uid=1000 ..." form of the first versions of this test
// fails to exec any child on this kernel 7.3 machine with
// "cannot execute binary file"):
//
//   unshare -U --map-root-user --map-users 1000:100000:1 --map-groups 1000:100000:1 setpriv --reuid=1000 --regid=1000 --clear-groups --bounding-set=-all,+net_raw --inh-caps=+net_raw --ambient-caps=+net_raw ./test_ambient_caps
//
// The production way of the ground station: the binary has the file
// capabilities "…=ep" and the inheritable set is empty. Give the test binary
// the same file capabilities and run it as the normal user (it needs
// root privileges to set the capability, but none to run):
//
//   setcap cap_net_admin,cap_net_raw,cap_setpcap=ep ./test_ambient_caps
//   ./test_ambient_caps
//
// The child process must then show the permitted capabilities (but not
// CAP_SETPCAP) as its ambient set.

#include "../base/base.h"
#include "../base/hardware.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <linux/capability.h>

static int s_iTests = 0;
static int s_iFailures = 0;

static void _check(const char* szName, unsigned long long uGot, unsigned long long uExpected)
{
   s_iTests++;
   if ( uGot == uExpected )
   {
      printf("  PASS  %-60s got=0x%llx\n", szName, uGot);
      return;
   }
   s_iFailures++;
   printf("  FAIL  %-60s got=0x%llx expected=0x%llx\n", szName, uGot, uExpected);
}

// Reads one of the capability sets of this process from /proc/self/status
static unsigned long long _read_cap_set(const char* szName)
{
   unsigned long long uValue = 0;
   char szLine[256];
   FILE* fd = fopen("/proc/self/status", "r");
   if ( NULL == fd )
      return 0;
   while ( NULL != fgets(szLine, sizeof(szLine)/sizeof(szLine[0]), fd) )
   {
      if ( 0 == strncmp(szLine, szName, strlen(szName)) )
      {
         sscanf(szLine+strlen(szName), "%llx", &uValue);
         break;
      }
   }
   fclose(fd);
   return uValue;
}

// Starts a child process (a shell that prints its own capability sets) and
// returns the capability sets of the child. The child is a normal program
// without any file capability, exactly like the "iw" helper.
static unsigned long long _child_cap_set(const char* szName, unsigned long long* pAmbient)
{
   int fds[2];
   if ( 0 != pipe(fds) )
      return 0;

   pid_t pid = fork();
   if ( 0 == pid )
   {
      close(fds[0]);
      dup2(fds[1], STDOUT_FILENO);
      close(fds[1]);
      execl("/bin/sh", "sh", "-c", "grep -E '^Cap(Eff|Amb):' /proc/self/status", (char*)NULL);
      _exit(127);
   }
   close(fds[1]);
   char szBuffer[512];
   memset(szBuffer, 0, sizeof(szBuffer));
   ssize_t iRead = read(fds[0], szBuffer, sizeof(szBuffer)-1);
   close(fds[0]);
   int iStatus = 0;
   waitpid(pid, &iStatus, 0);
   if ( iRead <= 0 )
      return 0;

   unsigned long long uValue = 0;
   const char* p = strstr(szBuffer, szName);
   if ( NULL != p )
      sscanf(p+strlen(szName), "%llx", &uValue);
   if ( NULL != pAmbient )
   {
      *pAmbient = 0;
      const char* pAmb = strstr(szBuffer, "CapAmb:");
      if ( NULL != pAmb )
         sscanf(pAmb+7, "%llx", pAmbient);
   }
   return uValue;
}

int main()
{
   printf("\n== Test: the ambient capabilities of the ground station ==\n");

   unsigned long long uPermitted = _read_cap_set("CapPrm:");
   unsigned long long uInheritable = _read_cap_set("CapInh:");
   unsigned long long uAmbientBefore = _read_cap_set("CapAmb:");
   printf("  the capability sets of this process: permitted=0x%llx inheritable=0x%llx ambient=0x%llx\n",
      uPermitted, uInheritable, uAmbientBefore);

   // The ground station starts with an empty ambient set. The test makes sure
   // of that, so that the raise is the only thing that can give the
   // capability to the child process.
   prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);
   unsigned long long uAmbientCleared = _read_cap_set("CapAmb:");
   _check("the ambient set was empty before the raise", uAmbientCleared, 0ULL);

   // What the child process must get:
   //   - the legacy way (setpriv/capsh in a user namespace): the capabilities
   //     are already in the permitted AND in the inheritable set, and only
   //     those can be raised as ambient;
   //   - the production way (the file capabilities "…=ep" of the binaries):
   //     the capabilities are only in the permitted set and
   //     hardware_raise_ambient_capabilities() moves them into the
   //     inheritable set with capset() first, which requires CAP_SETPCAP in
   //     the effective set. CAP_SETPCAP itself is the meta-capability used
   //     to pass the privileges on: it is never raised as ambient.
   //     When CAP_SETPCAP is missing, the permitted capabilities cannot be
   //     passed on at all and this test must fail.
   unsigned long long uExpected = uPermitted;
   unsigned long long uSetPcap = ((unsigned long long)1) << CAP_SETPCAP;
   if ( 0 == (_read_cap_set("CapEff:") & uSetPcap) )
      uExpected = uPermitted & uInheritable;
   // CAP_SETPCAP is never raised as ambient: the helper processes don't need
   // it (it is used here only to pass the other privileges on):
   uExpected &= ~uSetPcap;
   uExpected &= ~uAmbientCleared;

   // The real function of the ground station
   hardware_raise_ambient_capabilities();

   // The ambient set of this process is not changed by the raise: the raised
   // capabilities are the ones that the NEXT executed program gets. So the
   // proof is the child process: it is an ordinary program, without any file
   // capability, exactly like the "iw" helper of the ground station.
   unsigned long long uChildAmbient = 0;
   unsigned long long uChild = _child_cap_set("CapEff:", &uChildAmbient);
   printf("  the child process: ambient=0x%llx effective=0x%llx (it must contain 0x%llx)\n",
      uChildAmbient, uChild, uExpected);
   _check("the child process gets the capabilities as ambient", (uChildAmbient & uExpected), uExpected);
   _check("the child process inherits the capabilities", (uChild & uExpected), uExpected);

   if ( 0 == uExpected )
      printf("  NOTE: this process has no capability to pass on. The check of the inheritance\n");
   if ( 0 == uExpected )
      printf("        is a real check only when the test is run with a capability (see the header of the test).\n");
   if ( 0 != getuid() )
      printf("  NOTE: this process is not root, so the child gets exactly the ambient capabilities.\n");

   printf("\n=========================================================\n");
   printf("Tests: %d, failures: %d\n", s_iTests, s_iFailures);
   if ( 0 == s_iFailures )
      printf("ALL TESTS PASSED\n");
   else
      printf("TEST FAILURES\n");
   printf("=========================================================\n");
   return ( 0 == s_iFailures ) ? 0 : 1;
}
