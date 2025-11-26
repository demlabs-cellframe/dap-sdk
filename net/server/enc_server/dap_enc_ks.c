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

#include "uthash.h"
#include "dap_common.h"

#include "dap_http_client.h"
#include "dap_http_header_server.h"

#include "dap_enc.h"
#include "include/dap_enc_ks.h"
#include "dap_enc_key.h"
#include "rand/dap_rand.h"

#define LOG_TAG "dap_enc_ks"

static dap_enc_ks_key_t * _ks = NULL;
static pthread_mutex_t s_ks_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_memcache_enable = false;
static time_t s_memcache_expiration_key = 0;

static void s_enc_key_free(dap_enc_ks_key_t **ptr);

void dap_enc_ks_deinit()
{
    pthread_mutex_lock(&s_ks_mutex);
    if (_ks) {
        dap_enc_ks_key_t *cur_item, *tmp;
        HASH_ITER(hh, _ks, cur_item, tmp) {
            // Clang bug at this, cur_item should change at every loop cycle
            HASH_DEL(_ks, cur_item);
            s_enc_key_free(&cur_item);
        }
    }
    pthread_mutex_unlock(&s_ks_mutex);
}

inline static void s_gen_session_id(char a_id_buf[DAP_ENC_KS_KEY_ID_SIZE])
{
    // Use thread-safe randombytes instead of rand() to ensure unique KeyID
    // for parallel clients. rand() is not thread-safe and can generate
    // identical values for concurrent requests.
    uint8_t l_random_bytes[DAP_ENC_KS_KEY_ID_SIZE];
    if (randombytes(l_random_bytes, DAP_ENC_KS_KEY_ID_SIZE) == 0) {
        // Map random bytes to ASCII uppercase letters (A-Z)
        for(short i = 0; i < DAP_ENC_KS_KEY_ID_SIZE; i++)
            a_id_buf[i] = 65 + (l_random_bytes[i] % 25);
    } else {
        // Fallback to rand() if randombytes fails (shouldn't happen)
        log_it(L_WARNING, "randombytes failed, using rand() fallback");
        for(short i = 0; i < DAP_ENC_KS_KEY_ID_SIZE; i++)
            a_id_buf[i] = 65 + rand() % 25;
    }
}

static void s_save_key_in_storge_unsafe(dap_enc_ks_key_t *a_key)
{
    HASH_ADD_STR(_ks,id,a_key);
    if(s_memcache_enable) {
        uint8_t* l_serialize_key = dap_enc_key_serialize(a_key->key, NULL);
        //dap_memcache_put(a_key->id, l_serialize_key, sizeof (dap_enc_key_serialize_t), s_memcache_expiration_key);
        free(l_serialize_key);
    }
}

static dap_enc_ks_key_t * s_enc_ks_find_unsafe(const char * v_id)
{
    dap_enc_ks_key_t * ret = NULL;
    HASH_FIND_STR(_ks,v_id,ret);
    if(ret == NULL) {
        if(s_memcache_enable) {
            /*void* l_key_buf;
            size_t l_val_length;
            bool find = dap_memcache_get(v_id, &l_val_length, (void**)&l_key_buf);
            if(find) {
                if(l_val_length != sizeof (dap_enc_key_serialize_t)) {
                    log_it(L_WARNING, "Data can be broken");
                }
                dap_enc_key_t* key = dap_enc_key_deserialize(l_key_buf, l_val_length);
                ret = DAP_NEW_Z(dap_enc_ks_key_t);
                strncpy(ret->id, v_id, DAP_ENC_KS_KEY_ID_SIZE);
                pthread_mutex_init(&ret->mutex,NULL);
                ret->key = key;
                HASH_ADD_STR(_ks,id,ret);
                free(l_key_buf);
                return ret;
            }*/
        }
    }
    return ret;
}

dap_enc_ks_key_t * dap_enc_ks_find(const char * v_id)
{
    pthread_mutex_lock(&s_ks_mutex);
    dap_enc_ks_key_t * ret = s_enc_ks_find_unsafe(v_id);
    pthread_mutex_unlock(&s_ks_mutex);
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
    pthread_mutex_init(&ret->mutex,NULL);
    return ret;
}

bool dap_enc_ks_save_in_storage(dap_enc_ks_key_t* key)
{
    pthread_mutex_lock(&s_ks_mutex);
    if(s_enc_ks_find_unsafe(key->id) != NULL) {
        pthread_mutex_unlock(&s_ks_mutex);
        log_it(L_WARNING, "key is already saved in storage");
        return false;
    }
    s_save_key_in_storge_unsafe(key);
    pthread_mutex_unlock(&s_ks_mutex);
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
    dap_enc_ks_save_in_storage(ret);
    return ret;
}

void dap_enc_ks_delete(const char *id)
{
    pthread_mutex_lock(&s_ks_mutex);
    dap_enc_ks_key_t *delItem = s_enc_ks_find_unsafe(id);
    if (delItem) {
        HASH_DEL (_ks, delItem);
        pthread_mutex_unlock(&s_ks_mutex);
        pthread_mutex_destroy(&delItem->mutex);
        s_enc_key_free(&delItem);
        return;
    }
    pthread_mutex_unlock(&s_ks_mutex);
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
