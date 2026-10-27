#pragma once
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif 

#define CORE_AFFINITY_RX_RADIO 0
#define CORE_AFFINITY_AUDIO 4

#define CORE_AFFINITY_VEHICLE_ROUTER 1
#define CORE_AFFINITY_VIDEO_CAPTURE 2
#define CORE_AFFINITY_RC_RX 3
#define CORE_AFFINITY_RX_COMMANDS 3
#define CORE_AFFINITY_TELEMETRY_TX 3

#define CORE_AFFINITY_STATION_GS 1
#define CORE_AFFINITY_VIDEO_OUTPUT 2
#define CORE_AFFINITY_CENTRAL_UI 3
#define CORE_AFFINITY_I2C 3
#define CORE_AFFINITY_TELEMETRY_RX 3
#define CORE_AFFINITY_RC_TX 3

#define CORE_AFFINITY_OTHERS 3

#define CORE_AFFINITY_OTHERS_OIPC 1
#define CORE_AFFINITY_MAJESTIC 0

void hw_log_processes(int argc, char *argv[]);

int hw_execute_process(const char *szCommand, int iTimeoutMs, char* szOutput, int iMaxOutputLength);
int hw_execute_process_wait(const char *szCommand);
int hw_process_exists(const char* szProcName);
char* hw_process_get_pids_inline(const char* szProcName);
void hw_process_get_pids(const char* szProcName, char* szOutput);
int hw_process_get_current_core(int iPID);

void hw_stop_process(const char* szProcName);

// Stops a process, giving it the time to stop by itself. The process is asked
// to stop with the SIGTERM signal and it is killed only if it does not exit
// in the given time.
void hw_stop_process_graceful(const char* szProcName, u32 uGraceMs);
int hw_kill_process(const char* szProcName, int iSignal);

int hw_execute_bash_command_nonblock(const char* command, char* outBuffer);
int hw_execute_bash_command(const char* command, char* outBuffer);
int hw_execute_bash_command_timeout(const char* command, char* outBuffer, u32 uTimeoutMs);
int hw_execute_bash_command_silent(const char* command, char* outBuffer);
int hw_execute_bash_command_raw(const char* command, char* outBuffer);
int hw_execute_bash_command_raw_timeout(const char* command, char* outBuffer, u32 uTimeoutMs);
int hw_execute_bash_command_raw_silent(const char* command, char* outBuffer);

void hw_execute_ruby_process(const char* szPrefixes, const char* szProcess, const char* szParams, char* szOutput);
void hw_execute_ruby_process_wait(const char* szPrefixes, const char* szProcess, const char* szParams, char* szOutput, int iWait);

// Resolves the full path of a Ruby binary, using the same lookup order as
// the process execution: the path as it is given, the binaries folder and
// the debug folder. Returns 1 if the binary was found and the full path was
// written to szFullPath.
int hw_get_ruby_process_full_path(const char* szProcess, char* szFullPath, int iFullPathSize);

int hw_procs_get_cpu_count();
// Returns 1 if the process has the CAP_NET_ADMIN capability, which is needed
// in order to configure the wireless interfaces (mode, channel, frequency,
// power). The check only reads the capabilities of the process: it does not
// change any privilege.
int hw_process_has_net_admin_capability();
int hw_get_current_thread_id();
int hw_init_worker_thread_attrs(pthread_attr_t* pAttr, int iDesiredCore, int iStackSizeBytes, int iSchedulingClass, int iRawPriority, const char* szSource);
void hw_log_current_thread_attributes(const char* szPrefix);

void hw_set_process_affinity(const char* szProcName, int iExceptThreadId, int iCoreStart, int iCoreEnd);
void hw_set_current_thread_affinity(const char* szLogPrefix, int iCoreStart, int iCoreEnd);

void hw_set_priority_current_proc(int iRawPrioriry);

// Returns the nice value to be used to start a process with the given raw
// thread priority. The value is limited to what the user of the application
// is allowed to set.
int hw_get_nice_value_for_raw_priority(int iRawPriority);
void hw_get_process_priority(const char* szProcName, char* szOutput);

int  hw_get_current_thread_rt_priority(const char* szLogPrefix);
void hw_set_current_thread_raw_priority(const char* szLogPrefix, int iNewRawPriority);
void hw_set_current_thread_rt_priority(const char* szLogPrefix, int iNewRawPriority);
void hw_set_current_thread_nice_priority(const char* szLogPrefix, int iNewRawPriority);
void hw_set_current_proc_nice_priority(const char* szLogPrefix, int iNewRawPriority);

// Elevate the calling process/thread to SCHED_FIFO real-time priority 50.
// This is used at GCS startup to prevent CPU contention from starving
// radio TX/RX threads. Requires cap_sys_nice capability (set via ruby_setcaps).
void hw_set_realtime_priority(void);

#ifdef __cplusplus
}  
#endif