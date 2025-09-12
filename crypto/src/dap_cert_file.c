/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2017-2018
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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "dap_common.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_file_utils.h"
#include "dap_strfuncs.h"
#include "dap_cert_file.h"

#define LOG_TAG "dap_cert_file"

static const char s_key_inheritor[] = "inheritor";

/**
 * @brief dap_cert_file_save
 * @param a_cert dap_cert_t certificate struucture
 * @param a_cert_file_path path to certificate
 * @return int
 */
int dap_cert_file_save(dap_cert_t * a_cert, const char * a_cert_file_path)
{
    char * l_file_dir = dap_path_get_dirname(a_cert_file_path);
    int l_err = dap_mkdir_with_parents(l_file_dir);
    DAP_DELETE(l_file_dir);
    if ( l_err )
        return log_it(L_ERROR, "Can't create dir \"%s\"", a_cert_file_path), -1;
    FILE *l_file = fopen(a_cert_file_path, "wb");
    if (!l_file) {
#ifdef DAP_OS_WINDOWS
        l_err = GetLastError();
#else
        l_err = errno;
#endif
        return log_it(L_ERROR, "Can't open file '%s' for write, error %d: \"%s\"",
                        a_cert_file_path, l_err, dap_strerror(l_err)), -2;
    }
    uint32_t l_data_size;
    byte_t *l_data = dap_cert_mem_save(a_cert, &l_data_size);
    if ( !l_data ) {
        l_err = -3;
        log_it(L_ERROR,"Can't serialize certificate in memory");
    } else if ( l_data_size -= fwrite(l_data, 1, l_data_size, l_file) ) {
        l_err = -4;
        log_it(L_ERROR, "Can't write cert to disk, unprocessed %u bytes", l_data_size);
    }
    fclose(l_file);
    DAP_DELETE(l_data);
    if (l_err)
        remove(a_cert_file_path);
    return l_err;
}

// balance the binary tree

/**
 * @brief s_balance_the_tree
 * 
 * @param a_reorder dap_cert_file_aux_t
 * @param a_left_idx size_t left tree node
 * @param a_right_idx size_t right tree node
 */
void s_balance_the_tree(dap_cert_file_aux_t *a_reorder, size_t a_left_idx, size_t a_right_idx)
{
    if (a_left_idx == a_right_idx) {
        a_reorder->buf[a_reorder->idx++] = a_left_idx;
        return;
    }
    size_t i = (a_left_idx + a_right_idx) / 2 + 1;
    a_reorder->buf[a_reorder->idx++] = i;
    s_balance_the_tree(a_reorder, a_left_idx, i - 1);
    if (i < a_right_idx) {
        s_balance_the_tree(a_reorder, i + 1, a_right_idx);
    }
}

/**
 * @brief dap_cert_deserialize_meta
 * 
 * @param a_cert 
 * @param a_data 
 * @param a_size 
 */
void dap_cert_deserialize_meta(dap_cert_t *a_cert, const uint8_t *a_data, size_t a_size)
{
    dap_cert_metadata_t **l_meta_arr = NULL, **l_newer_arr;
    int l_err = 0, q = 0;
    uint8_t *l_pos = (uint8_t*)a_data, *l_end = l_pos + a_size;
    while (!l_err && l_pos < l_end) {
        char *l_key_str = (char*)l_pos;
        if (!( l_pos = memchr(l_pos, '\0', (size_t)(l_end - l_pos)) ) || ++l_pos == l_end) {
            //l_pos = (uint8_t*)l_key_str;
            break;
        }
        uint32_t l_value_size = le32toh(*l_pos);
        if (l_pos + sizeof(uint32_t) + /* sizeof(dap_cert_metadata_type_t) */ + 1 + l_value_size > l_end) {
            //l_pos = (uint8_t*)l_key_str;
            break;
        }
        l_pos += sizeof(uint32_t);
        dap_cert_metadata_type_t l_meta_type = *(dap_cert_metadata_type_t*)l_pos;
        l_pos += /*sizeof(dap_cert_metadata_type_t)*/ 1;
        union { uint16_t l_tmp16; uint32_t l_tmp32; uint64_t l_tmp64; } l_tmp = { };
        uint8_t *l_value = l_pos;
        switch ( l_meta_type ) {
        case DAP_CERT_META_STRING:
        case DAP_CERT_META_SIGN:
            break;
        case DAP_CERT_META_CUSTOM:
            if ( !strcmp(l_key_str, s_key_inheritor) ) {
                if (a_cert->enc_key->_inheritor) {
                    log_it(L_DEBUG, "Few inheritor records in cert metadata");
                    l_err = -2;
                    break;
                }
                if (!( a_cert->enc_key->_inheritor = DAP_DUP_SIZE(l_pos, l_value_size) )) {
                    l_err = -3;
                    break;
                }
                a_cert->enc_key->_inheritor_size = l_value_size;
                l_pos += l_value_size;
                continue;
            }
            break;
        default:
            switch (l_value_size) {
            case 1:
                break;
            case 2:
                l_tmp.l_tmp16 = le16toh(*l_value);
            case 4:
                l_tmp.l_tmp32 = le32toh(*l_value);
                break;
            case 8:
            default:
                l_tmp.l_tmp64 = le64toh(*l_value);
                break;
            }
            l_value = (uint8_t*)&l_tmp;
            break;
        }
        if (l_err) {
            //l_pos = (uint8_t*)l_key_str;
            break;
        }
        l_pos += l_value_size;
        dap_cert_metadata_t *l_new_meta = dap_cert_new_meta(l_key_str, l_meta_type, l_value, l_value_size);
        if ( !l_new_meta )
            break;
        if (l_meta_arr) {
            l_newer_arr = DAP_REALLOC_COUNT_RET_IF_FAIL(l_meta_arr, q + 1, l_meta_arr);
            l_meta_arr = l_newer_arr;
        } else
            l_meta_arr = DAP_NEW_Z_RET_IF_FAIL(dap_cert_metadata_t*);
        l_meta_arr[q++] = l_new_meta;
    }
    if (q) {
        size_t l_reorder_arr[q];
        dap_cert_file_aux_t l_reorder = {l_reorder_arr, 0};
        s_balance_the_tree(&l_reorder, 0, q - 1);
        size_t n = l_reorder_arr[0];
        a_cert->metadata = dap_binary_tree_insert(NULL, l_meta_arr[n]->key, (void*)l_meta_arr[n]);
        for (int i = 1; i < q; ++i) {
            n = l_reorder_arr[i];
            dap_binary_tree_insert(a_cert->metadata, l_meta_arr[n]->key, (void *)l_meta_arr[n]);
        }
    }
    DAP_DELETE(l_meta_arr);
}

/**
 * @brief dap_cert_serialize_meta
 * 
 * @param a_cert 
 * @param a_buflen_out 
 * @return uint8_t* 
 */
uint8_t *dap_cert_serialize_meta(dap_cert_t *a_cert, size_t *a_buflen_out)
{
    dap_return_val_if_fail(a_cert, NULL);
    if ( a_cert->enc_key->_inheritor_size)
        dap_cert_add_meta_custom(a_cert, s_key_inheritor, a_cert->enc_key->_inheritor, a_cert->enc_key->_inheritor_size);
    dap_list_t *l_meta_list = dap_binary_tree_inorder_list(a_cert->metadata);
    if ( !l_meta_list ) {
        return NULL;
    }
    dap_list_t *l_meta_list_item = dap_list_first(l_meta_list);
    uint8_t *l_buf = NULL;
    size_t l_mem_shift = 0;
    while (l_meta_list_item) {
        dap_cert_metadata_t *l_meta_item = l_meta_list_item->data;
        size_t l_meta_item_size = sizeof(uint32_t) + 1 + l_meta_item->length + strlen(l_meta_item->key) + 1;
        if (!l_buf) {
            if (!( l_buf = DAP_NEW_Z_SIZE(uint8_t, l_meta_item_size) ))
                return dap_list_free(l_meta_list), log_it(L_CRITICAL, "%s", "Insufficient memory"), NULL;
        } else {
            uint8_t *l_new_buf = DAP_REALLOC(l_buf, l_mem_shift + l_meta_item_size);
            if (!l_new_buf)
                return DAP_DELETE(l_buf), dap_list_free(l_meta_list), log_it(L_CRITICAL, "%s", "Insufficient memory"), NULL;
            l_buf = l_new_buf;
        }
        // Security fix: use safe string copy
        size_t key_len = strlen(l_meta_item->key);
        memcpy((char *)&l_buf[l_mem_shift], l_meta_item->key, key_len + 1);
        l_mem_shift += strlen(l_meta_item->key) + 1;
        *(uint32_t *)&l_buf[l_mem_shift] = htole32(l_meta_item->length);
        l_mem_shift += sizeof(uint32_t);
        l_buf[l_mem_shift++] = l_meta_item->type;
        switch (l_meta_item->type) {
        case DAP_CERT_META_STRING:
        case DAP_CERT_META_SIGN:
        case DAP_CERT_META_CUSTOM:
            memcpy(&l_buf[l_mem_shift], l_meta_item->value, l_meta_item->length);
            l_mem_shift += l_meta_item->length;
            break;
        default:
            switch (l_meta_item->length) {
            case 1:
                l_buf[l_mem_shift++] = l_meta_item->value[0];
                break;
            case 2:
                *(uint16_t *)&l_buf[l_mem_shift] = htole16(*(uint16_t *)&l_meta_item->value[0]);
                l_mem_shift += 2;
                break;
            case 4:
                *(uint32_t *)&l_buf[l_mem_shift] = htole32(*(uint32_t *)&l_meta_item->value[0]);
                l_mem_shift += 4;
                break;
            case 8:
            default:
                *(uint64_t *)&l_buf[l_mem_shift] = htole64(*(uint64_t *)&l_meta_item->value[0]);
                l_mem_shift += 8;
                break;
            }
            break;
        }
        l_meta_list_item = l_meta_list_item->next;
    }
    dap_list_free(l_meta_list);
    if (a_buflen_out) {
        *a_buflen_out = l_mem_shift;
    }
    return l_buf;
}

/**
 * @brief dap_cert_mem_save
 * @param a_cert
 * @param a_cert_size_out
 * @return uint8_t*
 */
uint8_t* dap_cert_mem_save(dap_cert_t * a_cert, uint32_t *a_cert_size_out)
{
    dap_enc_key_t *l_key = a_cert->enc_key;

    uint64_t  l_priv_key_data_size = a_cert->enc_key->priv_key_data_size,
            l_pub_key_data_size = a_cert->enc_key->pub_key_data_size,
            l_metadata_size = l_key->_inheritor_size;
            
    uint8_t *l_pub_key_data = dap_enc_key_serialize_pub_key(l_key, &l_pub_key_data_size),
            *l_priv_key_data = dap_enc_key_serialize_priv_key(l_key, &l_priv_key_data_size),
            *l_metadata = dap_cert_serialize_meta(a_cert, &l_metadata_size);
    if (!l_pub_key_data && !l_priv_key_data)
        return log_it(L_ERROR, "Neither pvt, nor pub key in certificate, nothing to do"), DAP_DELETE(l_metadata), NULL;
    uint64_t l_total_size = sizeof(dap_cert_file_hdr_t) + DAP_CERT_ITEM_NAME_MAX + l_priv_key_data_size + l_pub_key_data_size + l_metadata_size;

    dap_cert_file_hdr_t l_hdr = {
        .sign = dap_cert_FILE_HDR_SIGN, .version = dap_cert_FILE_VERSION,
        .type = l_priv_key_data ? dap_cert_FILE_TYPE_PRIVATE : dap_cert_FILE_TYPE_PUBLIC,
        .sign_type = dap_sign_type_from_key_type( l_key->type ),
        .data_size = l_pub_key_data_size, .data_pvt_size = l_priv_key_data_size, .metadata_size = l_metadata_size,
        .ts_last_used = l_key->last_used_timestamp
    };
    uint8_t *l_data = DAP_VA_SERIALIZE_NEW(l_total_size, &l_hdr, (uint64_t)sizeof(l_hdr), a_cert->name, (uint64_t)sizeof(a_cert->name),
                                           l_pub_key_data, l_pub_key_data_size, l_priv_key_data, l_priv_key_data_size,
                                           l_metadata, l_metadata_size );
    if (a_cert_size_out)
        *a_cert_size_out = l_data ? l_total_size : 0;
    return DAP_DEL_MULTY(l_pub_key_data, l_priv_key_data, l_metadata), l_data;
}

/**
 * @brief dap_cert_file_load
 * @param a_cert_file_path: path to certificate, for example "{PREFIX}/var/lib/ca/node-addr.dcert"
 * @return dap_cert_t
 */
dap_cert_t* dap_cert_file_load(const char * a_cert_file_path)
{
    dap_cert_t * l_ret = NULL;
    int l_err = 0;
    FILE *l_file = fopen(a_cert_file_path, "rb");
    if ( !l_file ) {
#ifdef DAP_OS_WINDOWS
        l_err = GetLastError();
#else
        l_err = errno;
#endif
        return log_it(L_ERROR, "Can't open cert file '%s', error %d: \"%s\"", a_cert_file_path, l_err, dap_strerror(l_err)), NULL;
    }
    fseeko(l_file, 0L, SEEK_END);
    size_t l_file_size = ftello(l_file);
    rewind(l_file);
    byte_t *l_data = DAP_NEW_Z_SIZE(byte_t, l_file_size);
    if ( fread(l_data, 1, l_file_size, l_file) != l_file_size ) {
        l_err = -1;
        log_it(L_ERROR, "Can't read %zu bytes from the disk!", l_file_size);
    } else if (!( l_ret = dap_cert_mem_load(l_data, l_file_size) )) {
        log_it(L_ERROR, "Can't load cert from file");
        l_err = -2;
    } else
        dap_cert_add( l_ret );
    fclose(l_file);
    DAP_DELETE(l_data);
    if (l_err) {
        dap_cert_delete(l_ret);
        l_ret = NULL;
    }
    return l_ret;
}


/**
 * @brief dap_cert_mem_load
 * 
 * @param a_data - pointer to buffer with certificate, early loaded from filesystem
 * @param a_data_size - size of certificate
 * @return dap_cert_t* 
 */
dap_cert_t* dap_cert_mem_load(const void *a_data, size_t a_data_size)
{
    dap_return_val_if_fail_err(!!a_data, NULL, "No data provided to load cert from");
    dap_return_val_if_fail_err(a_data_size > sizeof(dap_cert_file_hdr_t), NULL, "Inconsistent cert data");
    dap_cert_t *l_ret = NULL;
    const uint8_t *l_data = (const uint8_t*)a_data;
    dap_cert_file_hdr_t l_hdr = *(dap_cert_file_hdr_t*)l_data;
    l_data += sizeof(l_hdr);
    if ( l_hdr.sign != dap_cert_FILE_HDR_SIGN )
        return log_it(L_ERROR, "Wrong cert signature, corrupted header!"), NULL;
    else if ( l_hdr.version < 1 )
        return log_it(L_ERROR, "Unrecognizable certificate version, corrupted file or your software is deprecated"), NULL;
    debug_if( dap_enc_debug_more(), L_DEBUG,"sizeof(l_hdr)=%zu "
                                            "l_hdr.data_pvt_size=%"DAP_UINT64_FORMAT_U" "
                                            "l_hdr.data_size=%"DAP_UINT64_FORMAT_U" "
                                            "l_hdr.metadata_size=%"DAP_UINT64_FORMAT_U" "
                                            "a_data_size=%zu ",
                                            sizeof(l_hdr), l_hdr.data_pvt_size, l_hdr.data_size,
                                            l_hdr.metadata_size, a_data_size );
    size_t l_size_req = sizeof(l_hdr) + DAP_CERT_ITEM_NAME_MAX + l_hdr.data_size + l_hdr.data_pvt_size + l_hdr.metadata_size;

    if ( l_size_req > a_data_size )
        return log_it(L_ERROR, "Cert data size exeeds file size, %zu > %zu", l_size_req, a_data_size), NULL;

    char l_name[DAP_CERT_ITEM_NAME_MAX];
    dap_strncpy(l_name, (const char*)l_data, DAP_CERT_ITEM_NAME_MAX - 1);
    l_data += DAP_CERT_ITEM_NAME_MAX;
    if (!( l_ret = dap_cert_new(l_name) ))
        return log_it(L_ERROR, "Can't create cert '%s'", l_name), NULL;
    else if (!( l_ret->enc_key = dap_enc_key_new(dap_sign_type_to_key_type(l_hdr.sign_type)) ))
        return log_it(L_ERROR, "Can't init new key with sign type %s",
                               dap_sign_type_to_str(l_hdr.sign_type)),
            dap_cert_delete(l_ret), NULL;
    l_ret->enc_key->last_used_timestamp = l_hdr.ts_last_used;
    
    if ( l_hdr.data_size ) {
        dap_enc_key_deserialize_pub_key(l_ret->enc_key, l_data, l_hdr.data_size);
        l_data += l_hdr.data_size;
    }
    if ( l_hdr.data_pvt_size ) {
        dap_enc_key_deserialize_priv_key(l_ret->enc_key, l_data, l_hdr.data_pvt_size);
        l_data += l_hdr.data_pvt_size;
    }
    if ( l_hdr.metadata_size )
        dap_cert_deserialize_meta(l_ret, l_data, l_hdr.metadata_size);
    dap_enc_key_update(l_ret->enc_key);
    return l_ret;
}
