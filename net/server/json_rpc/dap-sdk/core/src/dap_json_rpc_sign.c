#include <string.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_strfuncs.h"
#include "dap_hash.h"
#include "dap_sign.h"
#include "dap_enc_base58.h"
#include "dap_enc_bliss.h"
#include "dap_enc_tesla.h"
#include "dap_enc_picnic.h"
#include "dap_enc_dilithium.h"
#include "dap_enc_falcon.h"

#ifdef DAP_PQLR
#include "dap_pqrl_dilithium.h"
#include "dap_pqrl_falcon.h"
#include "dap_pqrl_sphincs.h"
#endif

#include "dap_list.h"

#include "dap_json_rpc_sign.h"
#include "dap_json_rpc_errors.h"
#include "json.h"


#define LOG_TAG "dap_json_rpc_sign"

json_object* dap_sign_to_json(const dap_sign_t *a_sign) {
    if (!a_sign)
        return NULL;
    json_object *l_object = json_object_new_object();
    if (!l_object) {
        dap_json_rpc_allocated_error;
        return NULL;
    }
    json_object *l_obj_type_sign = json_object_new_string(dap_sign_type_to_str(a_sign->header.type));
    if (!l_obj_type_sign) {
        json_object_put(l_object);
        dap_json_rpc_allocated_error;
        return NULL;
    }
    json_object_object_add(l_object, "type", l_obj_type_sign);
    dap_chain_hash_fast_t l_hash_pkey = {};
    if (dap_sign_get_pkey_hash((dap_sign_t *) a_sign, &l_hash_pkey)) {
        char l_hash[DAP_CHAIN_HASH_FAST_STR_SIZE];
        dap_chain_hash_fast_to_str(&l_hash_pkey, l_hash, sizeof(l_hash));
        json_object *l_obj_pkey_hash = json_object_new_string(l_hash);
        if (!l_obj_pkey_hash) {
            json_object_put(l_object);
            dap_json_rpc_allocated_error;
            return NULL;
        }
        json_object_object_add(l_object, "pkeyHash", l_obj_pkey_hash);
    }
    json_object *l_obj_pkey_size = json_object_new_uint64(a_sign->header.sign_pkey_size);
    if (!l_obj_pkey_size) {
        json_object_put(l_object);
        dap_json_rpc_allocated_error;
        return NULL;
    }
    json_object *l_obj_sign_size = json_object_new_uint64(a_sign->header.sign_size);
    if (!l_obj_sign_size) {
        json_object_put(l_object);
        json_object_put(l_obj_pkey_size);
        dap_json_rpc_allocated_error;
        return NULL;
    }
    json_object_object_add(l_object, "signPkeySize", l_obj_pkey_size);
    json_object_object_add(l_object, "signSize", l_obj_sign_size);
    return l_object;
}
