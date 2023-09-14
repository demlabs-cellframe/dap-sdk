/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2023
* All rights reserved.

This file is part of DAP SDK the open source project

DAP SDK is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP SDK is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "dap_global_db_cluster.h"
#include "dap_strfuncs.h"

/**
 * @brief Multiples data into a_old_pkt structure from a_new_pkt structure.
 * @param a_old_pkt a pointer to the old object
 * @param a_new_pkt a pointer to the new object
 * @return Returns a pointer to the multiple object
 */
dap_global_db_pkt_pack_t *dap_global_db_pkt_pack(dap_global_db_pkt_pack_t *a_old_pkt, dap_global_db_pkt_t *a_new_pkt)
{
    if (!a_new_pkt)
        return a_old_pkt;
    size_t l_add_size = dap_global_db_pkt_get_size(a_new_pkt);
    dap_global_db_pkt_pack_t *l_old_pkt;
    if (a_old_pkt)
        l_old_pkt = (dap_global_db_pkt_pack_t *)DAP_REALLOC(a_old_pkt, a_old_pkt->data_size + sizeof(dap_global_db_pkt_pack_t) + l_add_size);
    else
        l_old_pkt = DAP_NEW_Z_SIZE(dap_global_db_pkt_pack_t, sizeof(dap_global_db_pkt_pack_t) + l_add_size);
    memcpy(l_old_pkt->data + l_old_pkt->data_size, l_new_pkt, l_add_size);
    l_old_pkt->data_size += l_add_size;
    l_old_pkt->obj_count++;
    return l_old_pkt;
}

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_store_obj_t *a_store_obj)
{
    if (!a_store_obj)
        return NULL;

    size_t l_group_len = dap_strlen(a_store_obj->group);
    size_t l_key_len = dap_strlen(a_store_obj->key);
    size_t l_sign_len = dap_sign_get_size(a_store_obj->sign);
    size_t l_data_size_out = l_group_len + l_key_len + l_sign_len + a_store_obj->value_len;
    dap_global_db_pkt_t *l_pkt = DAP_NEW_SIZE(dap_global_db_pkt_t, l_data_size_out + sizeof(dap_global_db_pkt_t));

    /* Fill packet header */
    l_pkt->timestamp = a_store_obj->timestamp;
    l_pkt->group_len = l_group_len;
    l_pkt->key_len = l_key_len;
    l_pkt->value_len = a_store_obj->value_len;
    l_pkt->crc = a_store_obj->crc;

    /* Put serialized data into the payload part of the packet */
    byte_t *l_data_ptr = l_pkt->data;
    l_data_ptr = dap_stpcpy(l_data_ptr, a_store_obj->group);
    l_data_ptr = dap_stpcpy(l_data_ptr, a_store_obj->key);
    if (a_store_obj->value_len)
        l_data_ptr = mempcpy(l_data_ptr, a_store_obj->value, a_store_obj->value_len);
    l_data_ptr = mempcpy(l_data_ptr, a_store_obj->sign, l_sign_len);

    assert(l_data_ptr - l_pkt->data == l_data_size_out);
    return l_pkt;
}

/**
 * @brief Deserializes some objects from a packed structure into an array of objects.
 * @param pkt a pointer to the serialized packed structure
 * @param store_obj_count[out] a number of deserialized objects in the array
 * @return Returns a pointer to the first object in the array, if successful; otherwise NULL.
 */
dap_store_obj_t *dap_global_db_pkt_deserialize(const dap_global_db_pkt_t *a_pkt, size_t *a_store_obj_count)
{
uint32_t l_count, l_cur_count;
uint64_t l_size;
unsigned char *pdata, *pdata_end;
dap_store_obj_t *l_store_obj_arr, *l_obj;

    if(!a_pkt || a_pkt->data_size < sizeof(dap_global_db_pkt_t))
        return NULL;

    l_count = a_pkt->obj_count;
    l_size = l_count <= UINT16_MAX ? l_count * sizeof(struct dap_store_obj) : 0;

    l_store_obj_arr = DAP_NEW_Z_SIZE(dap_store_obj_t, l_size);

    if (!l_store_obj_arr || !l_size)
    {
        log_it(L_ERROR, "Invalid size: can't allocate %"DAP_UINT64_FORMAT_U" bytes, errno=%d", l_size, errno);
        DAP_DEL_Z(l_store_obj_arr);
        return NULL;
    }

    pdata = (unsigned char *) a_pkt->data;                                  /* Set <pdata> to begin of payload */
    pdata_end = pdata + a_pkt->data_size;                                   /* Set <pdata_end> to end of payload area
                                                                              will be used to prevent out-of-buffer case */
    l_obj = l_store_obj_arr;

    for ( l_cur_count = l_count ; l_cur_count; l_cur_count--, l_obj++ )
    {
        if ( (pdata  + sizeof (uint32_t)) > pdata_end )                     /* Check for buffer boundaries */
            {log_it(L_ERROR, "Broken GDB element: can't read 'type' field"); break;}
        l_obj->type = *((uint32_t *) pdata);
        pdata += sizeof(uint32_t);


        if ( (pdata  + sizeof (uint16_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'group_length' field"); break;}
        l_obj->group_len = *((uint16_t *) pdata);
        pdata += sizeof(uint16_t);

        if ( !l_obj->group_len )
            {log_it(L_ERROR, "Broken GDB element: 'group_len' field is zero"); break;}


        if ( (pdata + l_obj->group_len) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'group' field"); break;}
        l_obj->group = DAP_NEW_Z_SIZE(char, l_obj->group_len + 1);
        if (!l_obj->group) {
        log_it(L_CRITICAL, "Memory allocation error");
            DAP_DEL_Z(l_store_obj_arr);
            return NULL;
        }
        memcpy(l_obj->group, pdata, l_obj->group_len);
        pdata += l_obj->group_len;



        if ( (pdata + sizeof (uint64_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'id' field"); break;}
        l_obj->id = *((uint64_t *) pdata);
        pdata += sizeof(uint64_t);



        if ( (pdata + sizeof (uint64_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'timestamp' field");  break;}
        l_obj->timestamp = *((uint64_t *) pdata);
        pdata += sizeof(uint64_t);


        if ( (pdata + sizeof (uint16_t)) > pdata_end)
            {log_it(L_ERROR, "Broken GDB element: can't read 'key_length' field"); break;}
        l_obj->key_len = *((uint16_t *) pdata);
        pdata += sizeof(uint16_t);

        if ( !l_obj->key_len )
            {log_it(L_ERROR, "Broken GDB element: 'key_length' field is zero"); break;}

        if ((pdata + l_obj->key_len) > pdata_end)
            {log_it(L_ERROR, "Broken GDB element: 'key_length' field is out from allocated memory"); break;}

        l_obj->key_byte = DAP_NEW_SIZE(byte_t, l_obj->key_len + 1);
        if (!l_obj->key_byte) {
            log_it(L_CRITICAL, "Memory allocation error");
            DAP_DEL_Z(l_obj->group);
            DAP_DEL_Z(l_store_obj_arr);
            return NULL;
        }
        memcpy( l_obj->key_byte, pdata, l_obj->key_len);
        l_obj->key_byte[l_obj->key_len] = '\0';
        pdata += l_obj->key_len;


        if ( (pdata + sizeof (uint64_t)) > pdata_end )
            {log_it(L_ERROR, "Broken GDB element: can't read 'value_length' field"); break;}
        l_obj->value_len = *((uint64_t *) pdata);
        pdata += sizeof(uint64_t);

        if (l_obj->value_len) {
            if ( (pdata + l_obj->value_len) > pdata_end )
                {log_it(L_ERROR, "Broken GDB element: can't read 'value' field"); break;}
            l_obj->value = DAP_NEW_SIZE(uint8_t, l_obj->value_len);
            if (!l_obj->value) {
                log_it(L_CRITICAL, "Memory allocation error");
                DAP_DEL_Z(l_obj->key_byte);
                DAP_DEL_Z(l_obj->group);
                DAP_DEL_Z(l_store_obj_arr);
                return NULL;
            }
            memcpy(l_obj->value, pdata, l_obj->value_len);
            pdata += l_obj->value_len;
        }
    }

    assert(pdata <= pdata_end);

    // Return the number of completely filled dap_store_obj_t structures
    // because l_cur_count may be less than l_count due to too little memory
    if (a_store_obj_count)
        *a_store_obj_count = l_count;

    return l_store_obj_arr;
}
