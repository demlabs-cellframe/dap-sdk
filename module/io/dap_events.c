/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Limited   https://demlabs.net
 * Cellframe https://cellframe.net
 * Copyright  (c) 2017-2020
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#define __USE_GNU

#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#ifdef DAP_OS_UNIX
#include <pthread.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

//#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>

#if defined(DAP_OS_LINUX)
#include <sys/timerfd.h>
#endif

#if defined(DAP_OS_BSD)
#include <sys/event.h>
#include <err.h>
#endif

#if defined(DAP_OS_DARWIN)
#include <sys/types.h>
#include <sys/sysctl.h>

#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#elif defined (DAP_OS_BSD)
#include <pthread_np.h>
typedef cpuset_t cpu_set_t; // Adopt BSD CPU setstructure to POSIX variant
#endif




#if defined(DAP_OS_ANDROID)
#define NO_POSIX_SHED
#define NO_TIMER
#else
#endif

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <utlist.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_events.h"
#include "dap_context.h"
#include "dap_events_socket.h"
#include "dap_proc_thread.h"
#include "dap_config.h"

#define LOG_TAG "dap_events"

#ifdef DAP_EVENTS_CAPS_IOCP
LPFN_ACCEPTEX             pfnAcceptEx               = NULL;
LPFN_GETACCEPTEXSOCKADDRS pfnGetAcceptExSockaddrs   = NULL;
LPFN_CONNECTEX            pfnConnectEx              = NULL; 
LPFN_DISCONNECTEX         pfnDisconnectEx           = NULL;
pfn_RtlNtStatusToDosError pfnRtlNtStatusToDosError  = NULL;
#endif

bool g_debug_reactor = false;
static atomic_int_fast32_t  s_workers_init = 0;
static uint32_t s_threads_count = 1;
static pthread_t *s_threads_id = NULL;
static dap_worker_t **s_workers = NULL;

/**
 * @brief
 *
 * @return bool
 */
bool dap_events_workers_init_status(){
    return s_workers_init != 0 ? true : false;
}

/**
 * @brief dap_get_cpu_count
 *
 * @return uint32_t
 */
uint32_t dap_get_cpu_count( )
{
#ifdef DAP_OS_WINDOWS
  SYSTEM_INFO si;

  GetSystemInfo( &si );
  return si.dwNumberOfProcessors;
#else
#ifndef NO_POSIX_SHED
#ifndef DAP_OS_DARWIN
  cpu_set_t cs;
  CPU_ZERO( &cs );
#endif

#if defined (DAP_OS_ANDROID)
  sched_getaffinity( 0, sizeof(cs), &cs );
#elif defined (DAP_OS_DARWIN)
  int count=0;
  size_t count_len = sizeof(count);
  sysctlbyname("hw.logicalcpu", &count, &count_len, NULL, 0);

#else
  pthread_getaffinity_np(pthread_self(), sizeof(cs), &cs);
#endif

#ifndef DAP_OS_DARWIN
  uint32_t count = 0;
  for ( int i = 0; i < 32; i++ ){
    if ( CPU_ISSET(i, &cs) )
    count ++;
  }
#endif
  return count;

#else
  return 1;
#endif
#endif
}

/**
 * @brief dap_cpu_assign_thread_on
 *
 * @param a_cpu_id
 */
void dap_cpu_assign_thread_on(uint32_t a_cpu_id)
{
#ifndef DAP_OS_WINDOWS
#ifndef NO_POSIX_SHED

#ifdef DAP_OS_DARWIN
    pthread_t l_pthread_id = pthread_self();
    mach_port_t l_pthread_mach_port = pthread_mach_thread_np(l_pthread_id);
#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(a_cpu_id, &mask);
#endif
    int l_retcode;
#ifdef DAP_OS_DARWIN
    thread_affinity_policy_data_t l_policy_data={.affinity_tag = a_cpu_id};
    l_retcode = thread_policy_set(l_pthread_mach_port , THREAD_AFFINITY_POLICY, (thread_policy_t)&l_policy_data , 1);
#elif defined(DAP_OS_ANDROID)
    l_retcode = sched_setaffinity(pthread_self(), sizeof(cpu_set_t), &mask);
#else
    l_retcode = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &mask);
#endif
#ifdef DAP_OS_DARWIN
    if(l_retcode != 0 && l_retcode != EPFNOSUPPORT)
#else
    if(l_retcode != 0)
#endif
    {
        log_it(L_ERROR, "Set affinity error %d: \"%s\"", l_retcode, dap_strerror(l_retcode));
    }
#endif
#else

  if ( !SetThreadAffinityMask( GetCurrentThread(), (DWORD_PTR)(1 << a_cpu_id) ) ) {
    log_it( L_CRITICAL, "Error pthread_setaffinity_np() You really have %d or more core in CPU?", a_cpu_id );
    abort();
  }
#endif

}

/**
 * @brief sa_server_init Init server module
 * @arg a_threads_count  number of events processor workers in parallel threads
 * @return Zero if ok others if no
 */
int dap_events_init( uint32_t a_threads_count, size_t a_conn_timeout )
{
#ifdef DAP_OS_WINDOWS
    WSADATA wsaData;
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (ret)
        return _set_errno(WSAGetLastError()),
            log_it(L_CRITICAL, "Couldn't init Winsock DLL, error %d: %s", errno, dap_strerror(errno)),
            -1;
#ifdef DAP_EVENTS_CAPS_IOCP
    HMODULE ntdll = GetModuleHandle("ntdll.dll");
    if ( !ntdll || !(pfnRtlNtStatusToDosError = (pfn_RtlNtStatusToDosError)GetProcAddress(ntdll, "RtlNtStatusToDosError")) )
        return log_it(L_CRITICAL, "NtDll error \"%s\"", dap_strerror(GetLastError())), -1;

    SOCKET l_socket = socket(AF_INET, SOCK_STREAM, 0);
    DWORD l_bytes = 0;
    static GUID guidAcceptEx             = WSAID_ACCEPTEX,
                guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS,
                guidConnectEx            = WSAID_CONNECTEX,
                guidDisconnectEx         = WSAID_DISCONNECTEX;

#define GetExtFuncPtr(func) ({                                          \
    if ((ret = WSAIoctl(l_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,   \
                        &guid##func, sizeof(guid##func),                \
                        &pfn##func, sizeof(pfn##func),                  \
                        &l_bytes, NULL, NULL)) == SOCKET_ERROR)         \
    {                                                                   \
        _set_errno(WSAGetLastError());                                  \
        log_it(L_ERROR,"WSAIoctl error %d getting function \"%s\": %s", \
                       errno, DAP_STRINGIFY(func), dap_strerror(errno));\
        closesocket(l_socket);                                          \
    }                                                                   \
    ret; })

    if ( GetExtFuncPtr(AcceptEx)             ||
         GetExtFuncPtr(GetAcceptExSockaddrs) ||
         GetExtFuncPtr(ConnectEx)            ||
         GetExtFuncPtr(DisconnectEx) ) 
            return -2;
    closesocket(l_socket);
#undef GetExtFuncPtr
#endif // DAP_EVENTS_CAPS_IOCP
#endif // DAP_OS_WINDOWS
    g_debug_reactor = dap_config_get_item_bool_default(g_config, "general", "debug_reactor", false);
    uint32_t l_cpu_count = dap_get_cpu_count();
    if (a_threads_count > l_cpu_count)
        a_threads_count = l_cpu_count;

    s_threads_count = a_threads_count ? a_threads_count : l_cpu_count;
    assert(s_threads_count);
    s_workers =  DAP_NEW_Z_SIZE(dap_worker_t*,s_threads_count*sizeof (dap_worker_t*) );
    if ( !s_workers  )
        return -1;

    if(dap_context_init() != 0){
        log_it( L_CRITICAL, "Can't init client submodule dap_context( )" );
        goto err;
    }

    dap_worker_init(a_conn_timeout);
    if ( dap_events_socket_init() != 0 ) {
        log_it( L_CRITICAL, "Can't init client submodule dap_events_socket_init( )" );
        goto err;
    }

    log_it( L_NOTICE, "Initialized event socket reactor for %u threads", s_threads_count );

    s_workers_init = 1;

    return 0;

err:
    log_it(L_ERROR,"Deinit events subsystem");
    dap_events_deinit();
    return -1;
}

/**
 * @brief sa_server_deinit Deinit server module
 */
void dap_events_deinit( )
{
    dap_proc_thread_deinit();
    dap_events_socket_deinit();
    dap_worker_deinit();

    DAP_DEL_Z( s_workers );
    s_workers_init = 0;
#ifdef DAP_OS_WINDOWS
    WSACleanup();
#endif
}


/**
 * @brief dap_events_start  Run main server loop
 * @return Zero if ok others if not
 */
int dap_events_start()
{
    int l_ret = -1;
    if ( !s_workers_init ){
        log_it(L_CRITICAL, "Event socket reactor has not been fired, use dap_events_init() first");
        goto lb_err;
    }
    if (s_threads_id) {
        log_it(L_ERROR, "Threads id already initialized");
        goto lb_err;
    }
    s_threads_id = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(pthread_t, s_threads_count, -2);
    for( uint32_t i = 0; i < s_threads_count; i++) {
        dap_worker_t * l_worker = DAP_NEW_Z(dap_worker_t);
        if (!l_worker) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            l_ret = -6;
            goto lb_err;
        }

        l_worker->id = i;
        l_worker->context = dap_context_new(DAP_CONTEXT_TYPE_WORKER);
        l_worker->context->_inheritor = l_worker;

        s_workers[i] = l_worker;

        l_ret = dap_context_run(l_worker->context, i, DAP_CONTEXT_POLICY_FIFO, DAP_CONTEXT_PRIORITY_HIGH,
                                DAP_CONTEXT_FLAG_WAIT_FOR_STARTED, dap_worker_context_callback_started,
                                dap_worker_context_callback_stopped, l_worker);
        s_threads_id[i] = l_worker->context->thread_id;
        if(l_ret != 0){
            log_it(L_CRITICAL, "Can't run worker #%u",i);
            goto lb_err;
        }
    }
    // Init callback processor
    if (dap_proc_thread_init(s_threads_count) != 0 ){
        log_it( L_CRITICAL, "Can't init proc threads" );
        l_ret = -4;
        goto lb_err;
    }

    return 0;
lb_err:
    log_it(L_CRITICAL,"Events init failed with code %d", l_ret);
    for( uint32_t j = 0; j < s_threads_count; j++)
        DAP_DEL_Z(s_workers[j]);
    DAP_DEL_Z(s_threads_id);
    return l_ret;
}



#ifdef  DAP_SYS_DEBUG
void    *s_th_memstat_show  (void *a_arg)
{
(void) a_arg;

    while ( 1 )
    {
        for ( int j = 5; (j = sleep(j)); );                             /* Hibernate for 5 seconds ... */
        dap_memstat_show ();
    }
}
#endif


/**
 * @brief dap_events_wait
 * @return
 */
int dap_events_wait( )
{
#ifdef  DAP_SYS_DEBUG                                                    /* @RRL: 6901, 7202 Start of memstat show at interval basis */
pthread_attr_t  l_tattr;
pthread_t       l_tid;

    pthread_attr_init(&l_tattr);
    pthread_attr_setdetachstate(&l_tattr, PTHREAD_CREATE_DETACHED);
    pthread_create(&l_tid, &l_tattr, s_th_memstat_show, NULL);

#endif

    // Check if workers are properly initialized before waiting
    if (!s_workers_init || !s_workers) {
        log_it(L_WARNING, "dap_events_wait(): Workers not initialized, skipping wait");
        return 0;
    }

    for( uint32_t i = 0; i < s_threads_count; i++ ) {
        pthread_join(s_threads_id[i] , NULL );
    }
    DAP_DEL_Z(s_threads_id);
    return 0;
}

/**
 * @brief dap_events_stop
 * @param
 */
void dap_events_stop_all( )
{
    if ( !s_workers_init )
        log_it(L_CRITICAL, "Event socket reactor has not been fired, use dap_events_init() first");

    for( uint32_t i = 0; i < s_threads_count; i++ ) {
        dap_events_socket_event_signal( s_workers[i]->context->event_exit, 1);
    }
    s_workers_init = 0;
}


/**
 * @brief dap_worker_get_index_min
 * @return
 */
uint32_t dap_events_worker_get_index_min() {
    uint32_t min = 0;

    if (!s_workers_init) {
        log_it(L_CRITICAL, "Event socket reactor has not been fired, use dap_events_init() first");
        return -1;
    }
    for(uint32_t i = 0; i < s_threads_count; i++) {
        if (s_workers[min]->context->event_sockets_count > s_workers[i]->context->event_sockets_count)
            min = i;
    }

    return min;
}

uint32_t dap_events_thread_get_count()
{
    return  s_threads_count;
}

/**
 * @brief dap_worker_get_min
 * @return
 */
dap_worker_t *dap_events_worker_get_auto( )
{
    if ( !s_workers_init )
        log_it(L_CRITICAL, "Event socket reactor has not been fired, use dap_events_init() first");

    return s_workers[dap_events_worker_get_index_min()];
}

/**
 * @brief dap_worker_get_index
 * @param a_index
 * @return
 */
dap_worker_t * dap_events_worker_get(uint8_t a_index)
{
    if ( !s_workers_init ) {
        log_it(L_CRITICAL, "Event socket reactor has not been fired, use dap_events_init() first");
        return NULL;
    }

    if (a_index >= s_threads_count) {
        log_it(L_ERROR, "dap_events_worker_get(): Requested worker index %u >= threads_count %u", 
               (uint32_t)a_index, s_threads_count);
        return NULL;
    }

    return s_workers[a_index];
}

/**
 * @brief dap_worker_print_all
 */
void dap_worker_print_all( )
{
    if ( !s_workers_init )
        log_it(L_CRITICAL, "Event socket reactor has not been fired, use dap_events_init() first");

    for( uint32_t i = 0; i < s_threads_count; i ++ ) {
        log_it( L_INFO, "Worker: %d, count open connections: %d", s_workers[i]->id, s_workers[i]->context->event_sockets_count );
    }
}
