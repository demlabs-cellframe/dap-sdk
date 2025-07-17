#include "../include/python_cellframe_common.h"
int dap_crypto_init(void) { return 0; }
void dap_crypto_deinit(void) {}
void* dap_crypto_key_create(const char* type) { return (void*)1; }
void dap_crypto_key_destroy(void* key) {}
int dap_crypto_key_sign(void* key, const void* data, size_t data_size, void* signature, size_t* signature_size) { if(signature_size) *signature_size=64; return 0; }
bool dap_crypto_key_verify(void* key, const void* data, size_t data_size, const void* signature, size_t signature_size) { return true; }
void* dap_hash_fast(const void* data, size_t size) { static char hash[32]={0}; return hash; }
void* dap_hash_slow(const void* data, size_t size) { static char hash[32]={0}; return hash; }
