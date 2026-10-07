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
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#include <time.h>
#endif

#include <pthread.h>
#include <time.h>

#include "uthash.h"
#include "dap_common.h"
#include "rand/dap_rand.h"

#include "dap_http_client.h"
#include "dap_http_header_server.h"

#include "dap_enc.h"
#include "include/dap_enc_ks.h"
#include "dap_enc_key.h"

#define LOG_TAG "dap_enc_ks"

/* Session-key store hygiene: the KEX handshake is unauthenticated, so any
 * client can mint sessions. Expire idle ones and cap the total to keep the
 * store from growing without bound. */
#define DAP_ENC_KS_SESSION_TTL_SEC 3600
#define DAP_ENC_KS_SESSIONS_MAX 10000

static dap_enc_ks_key_t * _ks = NULL;
static bool s_memcache_enable = false;
static time_t s_memcache_expiration_key = 0;
static pthread_mutex_t s_ks_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t s_ks_count = 0;

static void s_enc_key_free(dap_enc_ks_key_t **ptr);

void dap_enc_ks_deinit()
{
    pthread_mutex_lock(&s_ks_lock);
    if (_ks) {
        dap_enc_ks_key_t *cur_item, *tmp;
        HASH_ITER(hh, _ks, cur_item, tmp) {
            // Clang bug at this, cur_item should change at every loop cycle
            HASH_DEL(_ks, cur_item);
            s_enc_key_free(&cur_item);
        }
        s_ks_count = 0;
    }
    pthread_mutex_unlock(&s_ks_lock);
}

inline static void s_gen_session_id(char a_id_buf[DAP_ENC_KS_KEY_ID_SIZE])
{
    // Session ids must not be predictable: rand() is seeded predictably and
    // would let an attacker guess live KeyIDs
    uint8_t l_rand[DAP_ENC_KS_KEY_ID_SIZE] = {0};
    if (randombytes(l_rand, sizeof(l_rand)) != 0)
        log_it(L_ERROR, "Can't generate random session id");
    for(short i = 0; i < DAP_ENC_KS_KEY_ID_SIZE; i++)
        a_id_buf[i] = 'A' + (l_rand[i] % 26);
}

static dap_enc_ks_key_t *s_ks_find_locked(const char *a_id)
{
    dap_enc_ks_key_t *l_ret = NULL;
    HASH_FIND_STR(_ks, a_id, l_ret);
    return l_ret;
}

static void s_ks_evict_stale_locked(void)
{
    time_t l_now = time(NULL);
    dap_enc_ks_key_t *cur_item, *tmp;
    HASH_ITER(hh, _ks, cur_item, tmp) {
        if (cur_item->time_created && l_now - cur_item->time_created > DAP_ENC_KS_SESSION_TTL_SEC) {
            HASH_DEL(_ks, cur_item);
            s_enc_key_free(&cur_item);
            s_ks_count--;
        }
    }
    // Enforce the hard cap: drop the oldest sessions first
    while (s_ks_count >= DAP_ENC_KS_SESSIONS_MAX) {
        dap_enc_ks_key_t *l_oldest = NULL;
        HASH_ITER(hh, _ks, cur_item, tmp) {
            if (!l_oldest || (cur_item->time_created && cur_item->time_created < l_oldest->time_created))
                l_oldest = cur_item;
        }
        if (!l_oldest)
            break;
        HASH_DEL(_ks, l_oldest);
        s_enc_key_free(&l_oldest);
        s_ks_count--;
    }
}

void s_save_key_in_storge(dap_enc_ks_key_t *a_key)
{
    HASH_ADD_STR(_ks,id,a_key);
    s_ks_count++;
    if(s_memcache_enable) {
        uint8_t* l_serialize_key = dap_enc_key_serialize(a_key->key, NULL);
        //dap_memcache_put(a_key->id, l_serialize_key, sizeof (dap_enc_key_serialize_t), s_memcache_expiration_key);
        free(l_serialize_key);
    }
}


dap_enc_ks_key_t * dap_enc_ks_find(const char * v_id)
{
    pthread_mutex_lock(&s_ks_lock);
    dap_enc_ks_key_t * ret = s_ks_find_locked(v_id);
    pthread_mutex_unlock(&s_ks_lock);
    return ret;
}

dap_enc_key_t * dap_enc_ks_find_http(struct dap_http_client * a_http_client)
{
    dap_http_header_t * hdr_key_id=dap_http_header_find(a_http_client->in_headers,"KeyID");

    if(hdr_key_id){
        
        dap_enc_ks_key_t * ks_key=dap_enc_ks_find(hdr_key_id->value);
        if(ks_key)
            return ks_key->key;
        else{
            log_it(L_WARNING, "Not found keyID %s in storage", hdr_key_id->value);
            return NULL;
        }
    }else{
        log_it(L_WARNING, "No KeyID in HTTP headers");
        return NULL;
    }
}

dap_enc_ks_key_t * dap_enc_ks_new()
{
    dap_enc_ks_key_t * ret = DAP_NEW_Z(dap_enc_ks_key_t);
    if (!ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    s_gen_session_id(ret->id);
    ret->time_created = time(NULL);
    pthread_mutex_init(&ret->mutex,NULL);
    return ret;
}

bool dap_enc_ks_save_in_storage(dap_enc_ks_key_t* key)
{
    pthread_mutex_lock(&s_ks_lock);
    if(s_ks_find_locked(key->id) != NULL) {
        pthread_mutex_unlock(&s_ks_lock);
        log_it(L_WARNING, "key is already saved in storage");
        return false;
    }
    if (!key->time_created)
        key->time_created = time(NULL);
    s_ks_evict_stale_locked();
    s_save_key_in_storge(key);
    pthread_mutex_unlock(&s_ks_lock);
    return true;
}

dap_enc_ks_key_t * dap_enc_ks_add(struct dap_enc_key * key)
{
    dap_enc_ks_key_t * ret = DAP_NEW_Z(dap_enc_ks_key_t);
    if (!ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    ret->key = key;
    pthread_mutex_init(&ret->mutex, NULL);
    s_gen_session_id(ret->id);
    ret->time_created = time(NULL);
    dap_enc_ks_save_in_storage(ret);
    return ret;
}

void dap_enc_ks_delete(const char *id)
{
    pthread_mutex_lock(&s_ks_lock);
    dap_enc_ks_key_t *delItem = s_ks_find_locked(id);
    if (delItem) {
        HASH_DEL (_ks, delItem);
        s_ks_count--;
        pthread_mutex_destroy(&delItem->mutex);
        s_enc_key_free(&delItem);
        pthread_mutex_unlock(&s_ks_lock);
        return;
    }
    pthread_mutex_unlock(&s_ks_lock);
    log_it(L_WARNING, "Can't delete key by id: %s. Key not found", id);
}

static void s_enc_key_free(dap_enc_ks_key_t **ptr)
{
    if (*ptr){
        if((*ptr)->key)
            dap_enc_key_delete((*ptr)->key);
        DAP_DELETE(*ptr);
    }
}
