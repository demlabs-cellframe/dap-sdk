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
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#include "uthash.h"
#include "utlist.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_file_utils.h"
#include "dap_string.h"
#include "dap_strfuncs.h"
#include "dap_cert.h"
#include "dap_cert_file.h"
#include "utarray.h"
//#include "dap_hash.h"
#define LOG_TAG "dap_cert"


typedef struct dap_sign_item
{
    dap_sign_t * sign;
    struct dap_sign_item * next;
    struct dap_sign_item * prev;
} dap_sign_item_t;

typedef struct dap_cert_item
{
    char name[DAP_CERT_ITEM_NAME_MAX];
    dap_cert_t * cert;
    UT_hash_handle hh;
} dap_cert_item_t;

typedef struct dap_cert_pvt
{
    dap_sign_item_t *signs;
} dap_cert_pvt_t;


#define PVT(a) ( ( dap_cert_pvt_t *)((a)->_pvt) )

static dap_cert_item_t * s_certs = NULL;
static UT_array *s_cert_folders = NULL;
static bool s_debug_more = false;

/**
 * @brief dap_cert_init empty stub for certificate init
 * @return
 */
int dap_cert_init() // TODO deinit too
{
    s_debug_more = dap_config_get_item_bool_default(g_config, "cert", "debug_more", false);
    debug_if(s_debug_more, L_DEBUG, "dap_cert_init: debug_more=%d", s_debug_more);
    uint16_t l_ca_folders_size = 0;
    char **l_ca_folders = dap_config_get_item_str_path_array(g_config, "resources", "ca_folders", &l_ca_folders_size);
    utarray_new(s_cert_folders, &ut_str_icd);
    utarray_reserve(s_cert_folders, l_ca_folders_size);
    for (uint16_t i=0; i < l_ca_folders_size; i++) {
        dap_cert_add_folder(l_ca_folders[i]);
    }
    dap_config_get_item_str_path_array_free(l_ca_folders, l_ca_folders_size);
    return 0;
}

/**
 * @brief
 * parse list of certificate from config file (if it is presented)
 * @param a_certs_str const char * string with certificate names
 * @param a_certs dap_cert_t *** array with certificates
 * @param a_certs_size size of certificate
 * @return size_t
 */
size_t dap_cert_parse_str_list(const char * a_certs_str, dap_cert_t *** a_certs, size_t * a_certs_size)
{
    char * l_certs_tmp_ptrs = NULL;
    char * l_certs_str_dup = strdup(a_certs_str);
    if (!l_certs_str_dup) {
        log_it(L_ERROR, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        return 0;
    }
    char *l_cert_str = strtok_r(l_certs_str_dup, ",", &l_certs_tmp_ptrs);

    // First we just calc items
    while(l_cert_str) {
        l_cert_str = strtok_r(NULL, ",", &l_certs_tmp_ptrs);
        (*a_certs_size)++;
    }
    // init certs array
    dap_cert_t **l_certs;
    *a_certs = l_certs = DAP_NEW_Z_SIZE(dap_cert_t*, (*a_certs_size) * sizeof(dap_cert_t*) );
    if (!l_certs) {
        log_it(L_ERROR, "Memory allocation error in %s, line %d", __PRETTY_FUNCTION__, __LINE__);
        DAP_DEL_Z(l_certs_str_dup);
        return 0;
    }
    // Second pass we parse them all
    strcpy(l_certs_str_dup, a_certs_str);
    l_cert_str = strtok_r(l_certs_str_dup, ",", &l_certs_tmp_ptrs);

    size_t l_certs_pos = 0;
    size_t l_sign_total_size =0;
    while(l_cert_str) {
        // trim whitespace in certificate's name
        l_cert_str = dap_strstrip(l_cert_str);// removes leading and trailing spaces
        // get certificate by name
        l_certs[l_certs_pos] = dap_cert_find_by_name(l_cert_str);
        // if certificate is found
        if(l_certs[l_certs_pos]) {
            l_sign_total_size += dap_cert_sign_output_size(l_certs[l_certs_pos]);
            l_certs_pos++;
        } else {
            log_it(L_WARNING,"Can't load cert %s",l_cert_str);
            DAP_DELETE(*a_certs);
            *a_certs = NULL;
            *a_certs_size = 0;
            break;
        }
        l_cert_str = strtok_r(NULL, ",", &l_certs_tmp_ptrs);
    }
    free(l_certs_str_dup);
    return  l_sign_total_size;
}



/**
 * @brief
 * simply call dap_sign_create_output_unserialized_calc_size( a_cert->enc_key,a_size_wished)
 * @param a_cert dap_cert_t * certificate object
 * @return size_t
 */
size_t dap_cert_sign_output_size(dap_cert_t * a_cert)
{
    return dap_sign_create_output_unserialized_calc_size( a_cert->enc_key);
}

/**
 * @brief dap_cert_sign_output
 * @param a_cert
 * @param a_data
 * @param a_data_size
 * @param a_output
 * @param a_output_size
 * @return
 */
int dap_cert_sign_output(dap_cert_t * a_cert, const void * a_data, size_t a_data_size,
                                        void * a_output, size_t *a_output_size)
{
    return dap_sign_create_output( a_cert->enc_key, a_data, a_data_size, a_output, a_output_size);
}

/**
 * @brief sign data by encryption key from certificate with choosed hash type
 * @param a_cert dap_cert_t * certificate object
 * @param a_data data for signing
 * @param a_data_size data size
 * @param a_hash_type data and pkey hash type
 * @return dap_sign_t*
 */
dap_sign_t *dap_cert_sign_with_hash_type(dap_cert_t *a_cert, const void *a_data, size_t a_data_size, uint32_t a_hash_type)
{
    dap_return_val_if_fail(a_cert && a_cert->enc_key && a_cert->enc_key->priv_key_data &&
                           a_cert->enc_key->priv_key_data_size && a_data && a_data_size, NULL);
    dap_sign_t *l_ret = dap_sign_create_with_hash_type(a_cert->enc_key, a_data, a_data_size, a_hash_type);

    if (l_ret)
        log_it(L_INFO, "Sign sizes: %d %d", l_ret->header.sign_size, l_ret->header.sign_pkey_size);
    else
        log_it(L_ERROR, "dap_sign_create return NULL");

    return l_ret;
}

/**
 * @brief
 * sign certificate with another certificate (a_cert->signs)
 * @param a_cert dap_cert_t certificate object
 * @param a_cert_signer dap_cert_t certificate object, which signs a_cert
 * @return int
 */
int dap_cert_add_cert_sign(dap_cert_t *a_cert, dap_cert_t *a_cert_signer)
{
    if (a_cert->enc_key->pub_key_data_size && a_cert->enc_key->pub_key_data) {
        dap_sign_item_t * l_sign_item = DAP_NEW_Z(dap_sign_item_t);
        if (!l_sign_item) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return -1;
        }
        l_sign_item->sign = dap_cert_sign(a_cert_signer,a_cert->enc_key->pub_key_data,a_cert->enc_key->pub_key_data_size);
        DL_APPEND ( PVT(a_cert)->signs, l_sign_item );
        return 0;
    } else {
        log_it (L_ERROR, "No public key in cert \"%s\" that we are trying to sign with \"%s\"", a_cert->name,a_cert_signer->name);
        return -1;
    }
}


/**
 * @brief generate certificate in memory with specified seed
 *
 * @param a_cert_name const char * name of certificate
 * @param a_key_type dap_enc_key_type_t key type
 * @param a_seed const void* seed for certificate generation
 * @param a_seed_size size_t size of seed
 * @return dap_cert_t*
 */
dap_cert_t * dap_cert_generate_mem_with_seed(const char * a_cert_name, dap_enc_key_type_t a_key_type,
        const void* a_seed, size_t a_seed_size)
{
    dap_enc_key_t *l_enc_key = dap_enc_key_new_generate(a_key_type, NULL, 0, a_seed, a_seed_size, 0);
    if (l_enc_key) {
        dap_cert_t * l_cert = dap_cert_new(a_cert_name);
        l_cert->enc_key = l_enc_key;
        if (a_seed && a_seed_size)
            log_it(L_DEBUG, "Certificate generated with seed hash %s", dap_get_data_hash_str(a_seed, a_seed_size).s);
        return l_cert;
    } else {
        log_it(L_ERROR,"Can't generate key in memory!");
        return NULL;
    }
}

/**
 * @brief generate certificate in memory
 *
 * @param a_cert_name const char * certificate name
 * @param a_key_type encryption key type
 * @return dap_cert_t*
 */
dap_cert_t * dap_cert_generate_mem(const char * a_cert_name, dap_enc_key_type_t a_key_type)
{
    return dap_cert_generate_mem_with_seed(a_cert_name, a_key_type, NULL, 0);
}

/**
 * @brief generate certificate and save it to file
 *
 * @param a_cert_name const char * certificate name
 * @param a_file_path const char * path to certificate file
 * @param a_key_type dap_enc_key_type_t key_type
 * @return dap_cert_t*
 */
dap_cert_t * dap_cert_generate(const char * a_cert_name
                                           , const char * a_file_path,dap_enc_key_type_t a_key_type )
{
    dap_cert_t * l_cert = dap_cert_generate_mem(a_cert_name,a_key_type);
    dap_cert_add(l_cert);
    if ( l_cert){
        if ( dap_cert_file_save(l_cert, a_file_path) == 0 ){
            return l_cert;
        } else{
            dap_cert_delete(l_cert);
            log_it(L_ERROR, "Can't save certificate to the file!");
            return NULL;
        }
    } else {
        log_it(L_ERROR,"Can't generate certificat in memory!");
    }
    return NULL;
}

/**
 * @brief dap_cert_delete_by_name
 * delete certificate object, finding by name
 * @param a_cert_name const char * certificate name
 */
void dap_cert_delete_by_name(const char * a_cert_name)
{
    dap_cert_t * l_cert = dap_cert_find_by_name(a_cert_name);
    if ( l_cert )
        dap_cert_delete( l_cert );
    else
        log_it(L_WARNING,"Can't find \"%s\" certificate to delete it",a_cert_name);
}

/**
 * @brief
 * find certificate by name in path, which is configured ca_folders parameter in chain config
 * @param a_cert_name const char *
 * @return
 */
dap_cert_t *dap_cert_find_by_name(const char *a_cert_name)
{
    if (!a_cert_name)
        return NULL;
    
    debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: CALLED with cert_name='%s'", a_cert_name);
    
    dap_cert_item_t *l_cert_item = NULL;
    dap_cert_t *l_ret = NULL;

    char* l_cert_name = (char*)a_cert_name;
    size_t l_cert_name_len = strlen(a_cert_name);
    for (unsigned int i = 0; i < l_cert_name_len; i++){
        if (l_cert_name[i]=='\\')
            l_cert_name[i]='/';
    }

    if(strstr(l_cert_name, "/")){
        // find external certificate
        char *l_cert_path = NULL;
        if (!strstr(l_cert_name, ".dcert"))
            l_cert_path = dap_strjoin("", l_cert_name, ".dcert", (char *)NULL);
        else
            l_cert_path = dap_strjoin("", l_cert_name, (char *)NULL);
        debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: loading external cert from path '%s'", l_cert_path);
        l_ret = dap_cert_file_load(l_cert_path);
        DAP_DELETE(l_cert_path);
    } else {
        HASH_FIND_STR(s_certs, a_cert_name, l_cert_item);
        if (l_cert_item ) {
            l_ret = l_cert_item->cert ;
            debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: '%s' FOUND in memory (enc_key=%p)", a_cert_name, l_ret->enc_key);
        } else {
            debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: '%s' NOT in memory, loading from ca_folders", a_cert_name);
            uint16_t l_ca_folders_size = 0;
            char *l_cert_path = NULL;
            char **l_ca_folders = dap_config_get_item_str_path_array(g_config, "resources", "ca_folders", &l_ca_folders_size);
            debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: ca_folders_size=%u", l_ca_folders_size);
            for (uint16_t i = 0; i < l_ca_folders_size; ++i) {
                l_cert_path = dap_strjoin("", l_ca_folders[i], "/", a_cert_name, ".dcert", (char *)NULL);
                debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: trying path '%s'", l_cert_path);
                l_ret = dap_cert_file_load(l_cert_path);
                DAP_DELETE(l_cert_path);
                if (l_ret) {
                    debug_if(s_debug_more, L_DEBUG, "dap_cert_find_by_name: '%s' loaded from file (enc_key=%p)", a_cert_name, l_ret->enc_key);
                    break;
                }
            }
            dap_config_get_item_str_path_array_free(l_ca_folders, l_ca_folders_size);
        }
    }
    if (!l_ret)
        log_it(L_DEBUG, "Can't load cert '%s'", a_cert_name);
    return l_ret;
}

dap_list_t *dap_cert_get_all_mem()
{
    dap_list_t *l_ret = NULL;
    dap_cert_item_t *l_cert_item = NULL, *l_cert_tmp;
    HASH_ITER(hh, s_certs, l_cert_item, l_cert_tmp) {
        l_ret = dap_list_append(l_ret, l_cert_item->cert);
    }
    return l_ret;
}

/**
 * @brief dap_cert_new
 * create certificate object with specified name
 * @param a_name const char *
 * @return
 */
dap_cert_t * dap_cert_new(const char * a_name)
{
    dap_cert_t *l_ret = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_cert_t, NULL);
    l_ret->_pvt = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_cert_pvt_t, NULL, l_ret);
    strncpy(l_ret->name, a_name, sizeof(l_ret->name) - 1);
    return l_ret;
}

int dap_cert_add(dap_cert_t *a_cert)
{
    dap_return_val_if_fail(a_cert, -1);
    dap_cert_item_t *l_cert_item = NULL;
    HASH_FIND_STR(s_certs, a_cert->name, l_cert_item);
    if (l_cert_item)
        return log_it(L_WARNING, "Certificate with name %s already present in memory", a_cert->name), -2;
    l_cert_item = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_cert_item_t, -3);
    dap_strncpy(l_cert_item->name, a_cert->name, sizeof(l_cert_item->name) - 1);
    l_cert_item->cert = a_cert;
    HASH_ADD_STR(s_certs, name, l_cert_item);
    return 0;
}

/**
 * @brief s_cert_delete
 * delete certificate object
 * @param a_cert dap_cert_t *
 */
void dap_cert_delete(dap_cert_t * a_cert)
{
    if (!a_cert)
        return;
    dap_cert_item_t * l_cert_item = NULL;
    HASH_FIND_STR(s_certs, a_cert->name, l_cert_item);
    if ( l_cert_item ){
         HASH_DEL(s_certs,l_cert_item);
         DAP_DELETE (l_cert_item);
    }

    if( a_cert->enc_key )
        dap_enc_key_delete (a_cert->enc_key );
    if( a_cert->metadata )
        dap_binary_tree_clear(a_cert->metadata);
    DAP_DEL_MULTY(a_cert->_pvt, a_cert);
}

static int s_make_cert_path(const char *a_cert_name, const char *a_folder_path, bool a_check_access, char *a_cert_path)
{
    int l_ret = snprintf(a_cert_path, MAX_PATH, "%s/%s.dcert", a_folder_path, a_cert_name);
    if (l_ret < 0) {
        *a_cert_path = '\0';
        return -1;
    } else
        return a_check_access && access(a_cert_path, F_OK) == -1
            ? ( log_it (L_ERROR, "File %s does not exist", a_cert_path), -2 )
            : 0;
}

/**
 * @brief dap_cert_add_file
 * load certificate file from folder (specified in chain config)
 * @param a_cert_name const char * certificate name
 * @param a_folder_path const char * certificate path
 * @return dap_cert_t
 */
dap_cert_t *dap_cert_add_file(const char *a_cert_name, const char *a_folder_path)
{
    char l_cert_path[MAX_PATH + 1];
    return s_make_cert_path(a_cert_name, a_folder_path, true, l_cert_path) ? NULL : dap_cert_file_load(l_cert_path);
}

int dap_cert_delete_file(const char *a_cert_name, const char *a_folder_path)
{
    char l_cert_path[MAX_PATH + 1];
    int ret = s_make_cert_path(a_cert_name, a_folder_path, true, l_cert_path);
    return ret ? ret : remove(l_cert_path);
}


/**
 * @brief save certitificate to folder
 *
 * @param a_cert dap_cert_t * certiticate object
 * @param a_file_dir_path const char * path to directory with certificate
 * @return int
 */
int dap_cert_save_to_folder(dap_cert_t *a_cert, const char *a_file_dir_path)
{
    char l_cert_path[MAX_PATH + 1];
    int ret = s_make_cert_path(a_cert->name, a_file_dir_path, false, l_cert_path);
    return ret ? ret : dap_cert_file_save(a_cert, l_cert_path);
}

/**
 * @brief dap_cert_to_pkey
 * get public key from certificate
 *  dap_pkey_from_enc_key( a_cert->enc_key )
 * @param a_cert dap_cert_t certificate object
 * @return dap_pkey_t
 */
dap_pkey_t *dap_cert_to_pkey(dap_cert_t *a_cert)
{
    return a_cert && a_cert->enc_key ? dap_pkey_from_enc_key(a_cert->enc_key) : NULL;
}

int dap_cert_get_pkey_hash(dap_cert_t *a_cert, dap_hash_fast_t *a_out_hash)
{
    dap_return_val_if_fail(a_cert && a_cert->enc_key && a_cert->enc_key->pub_key_data &&
                           a_cert->enc_key->pub_key_data_size && a_out_hash , -1);
    return dap_enc_key_get_pkey_hash(a_cert->enc_key, a_out_hash);
}

/**
 * @brief
 * compare certificate encryption key with key, which was used for event or block signing
 * @param a_cert dap_cert_t * certificate object
 * @param a_sign dap_sign_t * dap_sign_t object (signed block or event)
 * @return int
 */
int dap_cert_compare_with_sign (dap_cert_t *a_cert,const dap_sign_t *a_sign)
{
    dap_return_val_if_pass(!a_cert || !a_cert->enc_key || !a_sign, -4);
    if ( dap_sign_type_from_key_type( a_cert->enc_key->type ).type == a_sign->header.type.type ){
        size_t l_pub_key_size = 0;
        // serialize public key
        uint8_t *l_pub_key = dap_enc_key_serialize_pub_key(a_cert->enc_key, &l_pub_key_size);
        int l_ret = l_pub_key_size == a_sign->header.sign_pkey_size
            ? memcmp(l_pub_key, a_sign->pkey_n_sign, a_sign->header.sign_pkey_size)
            : -2;
        DAP_DELETE(l_pub_key);
        return l_ret;
    } else
        return -3; // Wrong sign type
}



/**
 * @brief Certificates signatures chain size
 * @param a_cert dap_cert_t certificate object
 * @return
 */
size_t dap_cert_count_cert_sign(dap_cert_t * a_cert)
{
    size_t ret;
    dap_sign_item_t * l_cert_item = NULL;
    DL_COUNT(  PVT(a_cert)->signs,l_cert_item,ret);
    return ret > 0 ? ret : 0 ;
}


/**
 * @brief show certificate information
 * @param a_cert dap_cert_t certificate object
 */
char *dap_cert_dump(dap_cert_t *a_cert)
{
    dap_string_t *l_ret = dap_string_new("");
    dap_string_append_printf(l_ret, "Certificate name: %s\n", a_cert->name);
    dap_string_append_printf(l_ret, "Signature type: %s\n",
                             dap_sign_type_to_str(dap_sign_type_from_key_type(a_cert->enc_key->type)));
    dap_string_append_printf(l_ret, "Private key size: %zu\n", a_cert->enc_key->priv_key_data_size);
    dap_string_append_printf(l_ret, "Public key size: %zu\n", a_cert->enc_key->pub_key_data_size);
    size_t l_meta_items_cnt = dap_binary_tree_count(a_cert->metadata);
    dap_string_append_printf(l_ret, "Metadata section count: %zu\n", l_meta_items_cnt);
    dap_string_append_printf(l_ret, "Certificates signatures chain size: %zu\n", dap_cert_count_cert_sign (a_cert));
    if (l_meta_items_cnt) {
        dap_string_append(l_ret, "Metadata sections\n");
        dap_list_t *l_meta_list = dap_binary_tree_inorder_list(a_cert->metadata);
        dap_list_t *l_meta_list_item = dap_list_first(l_meta_list);
        while (l_meta_list_item) {
            dap_cert_metadata_t *l_meta_item = (dap_cert_metadata_t *)l_meta_list_item->data;
            char *l_str;
            switch (l_meta_item->type) {
            case DAP_CERT_META_STRING:
                l_str = strndup((char *)l_meta_item->value, l_meta_item->length);
                dap_string_append_printf(l_ret, "%s\t%u\t%u\t%s\n", l_meta_item->key, l_meta_item->type, l_meta_item->length, l_str);
                free(l_str);
                break;
            case DAP_CERT_META_INT:
            case DAP_CERT_META_BOOL:
                dap_string_append_printf(l_ret, "%s\t%u\t%u\t%u\n", l_meta_item->key, l_meta_item->type, l_meta_item->length, *(uint32_t *)l_meta_item->value);
                break;
            default:
                l_str = l_meta_item->length ? DAP_NEW_Z_SIZE(char, l_meta_item->length * 2 + 1) : NULL;
                if (l_meta_item->length && !l_str) {
                    log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                    break;
                }
                dap_bin2hex(l_str, l_meta_item->value, l_meta_item->length);
                dap_string_append_printf(l_ret, "%s\t%u\t%u\t%s\n", l_meta_item->key, l_meta_item->type, l_meta_item->length, l_str);
                DAP_DELETE(l_str);
                break;
            }
            l_meta_list_item = l_meta_list_item->next;
        }
        dap_list_free(l_meta_list);
    }
    return dap_string_free(l_ret, false);
}

/**
 * @brief get certificate folder path
 * usage example: dap_cert_get_folder(0)
 * @param a_n_folder_path
 * @return const char*
 */
const char *dap_cert_get_folder(int a_n_folder_path)
{
    char **l_p = utarray_eltptr(s_cert_folders, (u_int)a_n_folder_path);
    return l_p ? *l_p : ( log_it(L_ERROR, "No default cert path, check \"ca_folders\" in cellframe-node.cfg"), NULL );
}


/**
 * @brief load certificates from specified folder
 *
 * @param a_folder_path const char *
 */
void dap_cert_add_folder(const char *a_folder_path)
{
    utarray_push_back(s_cert_folders, &a_folder_path);
    dap_mkdir_with_parents(a_folder_path);
    DIR *l_dir = opendir(a_folder_path);
    if( l_dir ) {
        struct dirent *l_dir_entry;
        while((l_dir_entry=readdir(l_dir))!=NULL){
            const char * l_filename = l_dir_entry->d_name;
            size_t l_filename_len = strlen (l_filename);
            // Check if its not special dir entries . or ..
            if( strcmp(l_filename,".") && strcmp(l_filename,"..") ){
                // If not check the file's suffix
                const char l_suffix[]=".dcert";
                size_t l_suffix_len = strlen(l_suffix);
                if (strncmp(l_filename+ l_filename_len-l_suffix_len,l_suffix,l_suffix_len) == 0 ){
                    char *l_cert_name = dap_strdup(l_filename);
                    l_cert_name[l_filename_len-l_suffix_len] = '\0'; // Remove suffix
                    // Load the cert file
                    if (!dap_cert_add_file(l_cert_name,a_folder_path))
                        log_it(L_ERROR,"Cert %s not loaded", l_filename);
                    else
                        log_it(L_DEBUG,"Cert %s loaded", l_filename);
                    DAP_DELETE(l_cert_name);
                }
            }
        }
        closedir(l_dir);

        log_it(L_NOTICE, "Added folder %s",a_folder_path);
    } else {
        log_it(L_WARNING, "Can't add folder %s to cert manager",a_folder_path);
    }
}

/**
 * @brief
 *
 * @param a_key const char *
 * @param a_type dap_cert_metadata_type_t
 * @param a_value void *
 * @param a_value_size size_t
 * @return dap_cert_metadata_t*
 */
dap_cert_metadata_t *dap_cert_new_meta(const char *a_key, dap_cert_metadata_type_t a_type, void *a_value, size_t a_value_size)
{
    dap_return_val_if_pass(!a_key || a_type > DAP_CERT_META_CUSTOM || (!a_value && a_value_size), NULL);
    size_t l_meta_item_size = sizeof(dap_cert_metadata_t) + a_value_size + strlen(a_key) + 1;
    dap_cert_metadata_t *l_new_meta = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_cert_metadata_t, l_meta_item_size, NULL);
    l_new_meta->length = a_value_size;
    l_new_meta->type = a_type;
    l_new_meta->key = dap_strdup(a_key);
    dap_mempcpy(l_new_meta->value, a_value, a_value_size);
    return l_new_meta;
}

/**
 * @brief Add metadata to certificate
 * action for command "cellframe-node cert add_metadata <cert name> <key:type:length:value>"
 * @param a_cert dap_cert_t * certificate object
 * @param a_key const char * key
 * @param a_type dap_cert_metadata_type_t type
 * @param a_value void * value
 * @param a_value_size size_t length
 */
void dap_cert_add_meta(dap_cert_t *a_cert, const char *a_key, dap_cert_metadata_type_t a_type, void *a_value, size_t a_value_size)
{
    dap_return_if_fail(a_cert);
    dap_cert_metadata_t *l_new_meta = dap_cert_new_meta(a_key, a_type, a_value, a_value_size);
    dap_return_if_fail_err(l_new_meta, "Can't create metadata item");
    dap_binary_tree_t *l_new_root = dap_binary_tree_insert(a_cert->metadata, l_new_meta->key, l_new_meta);
    if (!a_cert->metadata) {
        a_cert->metadata = l_new_root;
    }
}

/**
 * @brief Add metadata to certificate with additional value modification
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_key const char * key
 * @param a_type dap_cert_metadata_type_t type
 * @param a_value void * value
 * @param a_value_size size_t length
 */
void dap_cert_add_meta_scalar(dap_cert_t *a_cert, const char *a_key, dap_cert_metadata_type_t a_type, uint64_t a_value, size_t a_value_size)
{
    void *l_value = NULL;
    union { byte_t l_tmp8; uint16_t l_tmp16; uint32_t l_tmp32; uint64_t l_tmp64; } uval = { };
    switch (a_type) {
    case DAP_CERT_META_STRING:
    case DAP_CERT_META_SIGN:
    case DAP_CERT_META_CUSTOM:
        log_it(L_WARNING, "Incorrect metadata type for dap_cert_add_meta_scalar()");
        return;
    default:
        switch (a_value_size) {
        case 1:
            uval.l_tmp8 = a_value;
            break;
        case 2:
            uval.l_tmp16 = a_value;
            break;
        case 4:
            uval.l_tmp32 = a_value;
            break;
        case 8:
        default:
            uval.l_tmp64 = a_value;
            break;
        }
        break;
    }
    dap_cert_add_meta(a_cert, a_key, a_type, &uval, a_value_size);
}

/**
 * @brief get specified metadata from certificate
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return dap_cert_metadata_t*
 */
dap_cert_metadata_t *dap_cert_get_meta(dap_cert_t *a_cert, const char *a_field)
{
    return dap_binary_tree_search(a_cert->metadata, a_field);
}

/**
 * @brief get specified metadata from certificate in string (DAP_CERT_META_STRING) format
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return char*
 */
char *dap_cert_get_meta_string(dap_cert_t *a_cert, const char *a_field)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return NULL;
    }
    if (l_meta->type != DAP_CERT_META_STRING) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return NULL;
    }
    return strndup((char *)&l_meta->value[0], l_meta->length);
}

/**
 * @brief get metadata from certificate with boolean (DAP_CERT_META_BOOL) type
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return true
 * @return false
 */
bool dap_cert_get_meta_bool(dap_cert_t *a_cert, const char *a_field)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return -1;
    }
    if (l_meta->type != DAP_CERT_META_BOOL) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return -1;
    }
    if (l_meta->length != sizeof(bool)) {
        log_it(L_DEBUG, "Metadata field corrupted");
    }
    return *(bool *)&l_meta->value[0];
}

/**
 * @brief get metadata from certificate with int (DAP_CERT_META_INT) type
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return int
 */
int dap_cert_get_meta_int(dap_cert_t *a_cert, const char *a_field)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return -1;
    }
    if (l_meta->type != DAP_CERT_META_INT) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return -1;
    }
    if (l_meta->length != sizeof(int)) {
        log_it(L_DEBUG, "Metadata field corrupted");
    }
    return *(int *)&l_meta->value[0];
}

/**
 * @brief get metadata from certificate with datetime (DAP_CERT_META_DATETIME) type
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return time_t
 */
time_t dap_cert_get_meta_time(dap_cert_t *a_cert, const char *a_field)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return -1;
    }
    if (l_meta->type != DAP_CERT_META_DATETIME) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return -1;
    }
    if (l_meta->length != sizeof(time_t)) {
        log_it(L_DEBUG, "Metadata field corrupted");
    }
    return *(time_t *)&l_meta->value[0];
}

/**
 * @brief get metadata from certificate with datetime (DAP_CERT_META_DATETIME_PERIOD) type
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return time_t
 */
time_t dap_cert_get_meta_period(dap_cert_t *a_cert, const char *a_field)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return -1;
    }
    if (l_meta->type != DAP_CERT_META_DATETIME_PERIOD) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return -1;
    }
    if (l_meta->length != sizeof(time_t)) {
        log_it(L_DEBUG, "Metadata field corrupted");
    }
    return *(time_t *)&l_meta->value[0];
}

/**
 * @brief get metadata from certificate with dap_sign_t (DAP_CERT_META_SIGN) type
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @return dap_sign_t*
 */
dap_sign_t *dap_cert_get_meta_sign(dap_cert_t *a_cert, const char *a_field)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return NULL;
    }
    if (l_meta->type != DAP_CERT_META_SIGN) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return NULL;
    }
    dap_sign_t *l_ret = (dap_sign_t *)&l_meta->value[0];
    if (l_meta->length != dap_sign_get_size(l_ret)) {
        log_it(L_DEBUG, "Metadata field corrupted");
    }
    return l_ret;
}

/**
 * @brief get metadata from certificate with custom (DAP_CERT_META_CUSTOM) type
 *
 * @param a_cert dap_cert_t * certificate object
 * @param a_field const char * field, which will be gotten from metadata
 * @param a_meta_size_out size_t size of recieved data
 * @return void*
 */
void *dap_cert_get_meta_custom(dap_cert_t *a_cert, const char *a_field, size_t *a_meta_size_out)
{
    dap_cert_metadata_t *l_meta = dap_cert_get_meta(a_cert, a_field);
    if (!l_meta) {
        return NULL;
    }
    if (l_meta->type != DAP_CERT_META_CUSTOM) {
        log_it(L_DEBUG, "Requested and actual metadata types are not equal");
        return NULL;
    }
    if (a_meta_size_out) {
        *a_meta_size_out = l_meta->length;
    }
    return (void *)&l_meta->value[0];
}

/**
 * @brief dap_cert_deinit
 * empty function
 */
void dap_cert_deinit()
{
    dap_cert_item_t *l_cert_item = NULL, *l_cert_tmp;
    HASH_ITER(hh, s_certs, l_cert_item, l_cert_tmp) {
         HASH_DEL(s_certs, l_cert_item);
         dap_cert_delete(l_cert_item->cert);
         DAP_DELETE (l_cert_item);
    }
}

/**
 * @brief create new key with merged all keys from certs
 * @param a_cert pointer to certificate objects
 * @param a_count certificate count, if > 1 forming MULTISIGN key
 * @param a_key_start_index index to start getting keys
 * @return pointer to key, NULL if error
 */
dap_enc_key_t *dap_cert_get_keys_from_certs(dap_cert_t **a_certs, size_t a_count, size_t a_key_start_index)
{
// sanity check
    dap_return_val_if_pass(!a_certs || !a_count || !a_certs[0] || a_key_start_index >= a_count, NULL);
    if (a_count == 1)
        return dap_enc_key_dup(a_certs[0]->enc_key);
// memory alloc
    size_t l_keys_count = 0;
    dap_enc_key_t *l_keys[a_count];
// func work
    // Fill l_keys array starting from index 0, not from a_key_start_index
    // to avoid uninitialized memory at the beginning of the array
    for(size_t i = a_key_start_index; i < a_count; ++i) {
        if (a_certs[i]) {
            l_keys[l_keys_count] = dap_enc_key_dup(a_certs[i]->enc_key);
            l_keys_count++;
        } else {
            log_it(L_WARNING, "Certs with NULL value");
        }
    }
    return dap_enc_merge_keys_to_multisign_key(l_keys, l_keys_count);
}

DAP_INLINE const char *dap_cert_get_str_recommended_sign(){
    return "sig_dil\nsig_falcon\nsig_sphincs\n"
#ifdef DAP_SHIPOVNIK
    "sig_shipovnik\n"
#endif
    ;
}

/**
 * @brief get pkey_full str from cert
 * @param a_cert pointer to certificate
 * @param a_str_type str type, hex or base58
 * @return pointer to pkey_full str
 */
char *dap_cert_get_pkey_str(dap_cert_t *a_cert, const char *a_str_type)
{
    dap_pkey_t *l_pkey = dap_cert_to_pkey(a_cert);
    char *l_ret = dap_pkey_to_str(l_pkey, a_str_type);
    DAP_DELETE(l_pkey);
    return l_ret;
}
