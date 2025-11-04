/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifdef _WIN32
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#include <pthread.h>
#endif

#include "dap_common.h"
#include "dap_stream_session.h"
#include "dap_config.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_stream_session"

static dap_stream_session_t *s_sessions = NULL;
static pthread_mutex_t s_sessions_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_debug_more = false;




int stream_session_close2(dap_stream_session_t * s);
static void * session_check(void * data);

void dap_stream_session_init()
{
    s_debug_more = dap_config_get_item_bool_default(g_config, "stream", "debug_more", false);
    log_it(L_INFO,"Init module");
    srand ( time(NULL) );
}

void dap_stream_session_deinit()
{
    dap_stream_session_t *current, *tmp;
    log_it(L_INFO,"Destroy all the sessions");
    pthread_mutex_lock(&s_sessions_mutex);
      HASH_ITER(hh, s_sessions, current, tmp) {
          // Clang bug at this, current should change at every loop cycle
          HASH_DEL(s_sessions,current);
          if (current->callback_delete)
              current->callback_delete(current, NULL);
          if (current->_inheritor )
              DAP_DELETE(current->_inheritor);
          DAP_DELETE(current);
      }
    pthread_mutex_unlock(&s_sessions_mutex);
}

/**
 *
 * note: dap_stream_session_get_list_sessions_unlock() must be run after this function
 */
dap_list_t* dap_stream_session_get_list_sessions(void)
{
    dap_list_t *l_list = NULL;
    dap_stream_session_t *current, *tmp;

    pthread_mutex_lock(&s_sessions_mutex);
    HASH_ITER(hh, s_sessions, current, tmp)
        l_list = dap_list_append(l_list, current);

    /* pthread_mutex_lock(&s_sessions_mutex); Don't forget do it some out-of-here !!! */

    return l_list;
}


void dap_stream_session_get_list_sessions_unlock(void)
{
    pthread_mutex_unlock(&s_sessions_mutex);
}


dap_stream_session_t * dap_stream_session_pure_new()
{
dap_stream_session_t *l_stm_sess, *l_stm_tmp = NULL;
uint32_t session_id = 0;

    if ( !(l_stm_sess = DAP_NEW_Z(dap_stream_session_t)) )              /* Preallocate new session context */
           return  log_it(L_ERROR, "Cannot alocate memory for a new session context, errno=%d", errno), NULL;

       /*
        * Generate session id, check uniqueness against sessions hash table,
        * add new session id into the table
        */
       pthread_mutex_lock(&s_sessions_mutex);

       do {
           session_id = random_uint32_t(RAND_MAX);
           HASH_FIND(hh, s_sessions, &session_id, sizeof(uint32_t), l_stm_tmp);
       } while(l_stm_tmp);

       l_stm_sess->id = session_id;
       HASH_ADD(hh, s_sessions, id, sizeof(uint32_t), l_stm_sess);
       pthread_mutex_unlock(&s_sessions_mutex);                            /* Unlock ASAP ! */

       /* Prefill session context with data ... */
       pthread_mutex_init(&l_stm_sess->mutex, NULL);
       l_stm_sess->time_created = time(NULL);
       l_stm_sess->create_empty = true;

       log_it(L_INFO, "Created session context [stm_sess:%p, id:%u, ts:%"DAP_UINT64_FORMAT_U"]",  l_stm_sess, l_stm_sess->id, l_stm_sess->time_created);

       return l_stm_sess;
}

dap_stream_session_t * dap_stream_session_new(unsigned int media_id, bool open_preview)
{
    dap_stream_session_t * ret=dap_stream_session_pure_new();
    ret->media_id=media_id;
    ret->open_preview=open_preview;
    ret->create_empty=false;

    return ret;
}

/**
 * @brief dap_stream_session_id_mt
 * @param id
 * @return
 */
dap_stream_session_t *dap_stream_session_id_mt(uint32_t a_id)
{
    dap_stream_session_t *l_ret = NULL;
    dap_stream_session_lock();
    HASH_FIND(hh, s_sessions, &a_id, sizeof(uint32_t), l_ret);
    dap_stream_session_unlock();
    return l_ret;
}

/**
 * @brief dap_stream_session_id_unsafe
 * @param id
 * @return
 */
dap_stream_session_t *dap_stream_session_id_unsafe(uint32_t id )
{
    dap_stream_session_t *ret = NULL;
    HASH_FIND(hh, s_sessions, &id, sizeof(uint32_t), ret);
    return ret;
}

/**
 * @brief dap_stream_session_lock
 */
void dap_stream_session_lock()
{
    pthread_mutex_lock(&s_sessions_mutex);
}

/**
 * @brief dap_stream_session_unlock
 */
void dap_stream_session_unlock()
{
    pthread_mutex_unlock(&s_sessions_mutex);
}


int dap_stream_session_close_mt(uint32_t id)
{
dap_stream_session_t *l_stm_sess;

    log_it(L_INFO, "Close session id %u ...", id);

    dap_stream_session_lock();
    if ( !(l_stm_sess = dap_stream_session_id_unsafe( id )) )
    {
        dap_stream_session_unlock();
        log_it(L_WARNING, "Session id %u not found", id);

        return -1;
    }

    HASH_DEL(s_sessions, l_stm_sess);
    dap_stream_session_unlock();

    log_it(L_INFO, "Delete session context [stm_sess:%p, id:%u, ts:%"DAP_UINT64_FORMAT_U"]",  l_stm_sess, l_stm_sess->id, l_stm_sess->time_created);

    if (l_stm_sess->callback_delete)
        l_stm_sess->callback_delete(l_stm_sess, NULL);

    DAP_DEL_Z(l_stm_sess->_inheritor);
    if (l_stm_sess->key)
        dap_enc_key_delete(l_stm_sess->key);
    DAP_DEL_Z(l_stm_sess->acl);

    DAP_DELETE(l_stm_sess);

    return  0;
}


/**
 * @brief dap_stream_session_open
 * @param a_session
 * @return
 */
int dap_stream_session_open(dap_stream_session_t * a_session)
{
    if (!a_session) {
        log_it(L_ERROR, "dap_stream_session_open: session is NULL");
        return -1;
    }
    int ret;
    debug_if(s_debug_more, L_DEBUG, "dap_stream_session_open: locking mutex for session %u", a_session->id);
    pthread_mutex_lock(&a_session->mutex);
    debug_if(s_debug_more, L_DEBUG, "dap_stream_session_open: mutex locked, opened=%d", a_session->opened);
    ret=a_session->opened;
    if(a_session->opened==0) a_session->opened=1;
    pthread_mutex_unlock(&a_session->mutex);
    debug_if(s_debug_more, L_DEBUG, "dap_stream_session_open: returning %d", ret);
    return ret;
}
