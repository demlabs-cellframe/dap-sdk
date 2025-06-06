#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "dap_common.h"
#include "dap_time.h"
#include "dap_enc_chipmunk.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"

#define LOG_TAG "profile_keygen"

static inline double get_time_ms(void) {
    return dap_nanotime_now() / 1000000.0;
}

int main(int argc, char *argv[]) {
    int num_keys = 50;
    if (argc > 1) {
        num_keys = atoi(argv[1]);
    }
    
    printf("🔬 PROFILING KEY GENERATION for %d keys\n", num_keys);
    
    // Initialize chipmunk
    dap_common_init(NULL, NULL);
    dap_enc_chipmunk_init();
    
    // Allocate arrays
    chipmunk_private_key_t *private_keys = malloc(num_keys * sizeof(chipmunk_private_key_t));
    chipmunk_public_key_t *public_keys = malloc(num_keys * sizeof(chipmunk_public_key_t));
    chipmunk_hots_pk_t *hots_public_keys = malloc(num_keys * sizeof(chipmunk_hots_pk_t));
    chipmunk_hots_sk_t *hots_secret_keys = malloc(num_keys * sizeof(chipmunk_hots_sk_t));
    
    if (!private_keys || !public_keys || !hots_public_keys || !hots_secret_keys) {
        printf("❌ Memory allocation failed\n");
        return -1;
    }
    
    // Initialize cached HOTS params
    chipmunk_hots_params_t hots_params;
    if (chipmunk_hots_get_cached_params(&hots_params) != 0) {
        printf("❌ Failed to get cached HOTS params\n");
        return -1;
    }
    
    double total_start = get_time_ms();
    
    // Profile individual steps
    double chipmunk_keypair_time = 0.0;
    double hots_keygen_time = 0.0;
    
    for (int i = 0; i < num_keys; i++) {
        // Profile chipmunk_keypair
        double step_start = get_time_ms();
        int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                   (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
        chipmunk_keypair_time += (get_time_ms() - step_start);
        
        if (ret != 0) {
            printf("❌ chipmunk_keypair failed for key %d\n", i);
            return -1;
        }
        
        // Profile HOTS keygen
        hots_public_keys[i].v0 = private_keys[i].pk.v0;
        hots_public_keys[i].v1 = private_keys[i].pk.v1;
        
        uint8_t hots_seed[32];
        memcpy(hots_seed, private_keys[i].key_seed, 32);
        uint32_t counter = (uint32_t)i;
        
        step_start = get_time_ms();
        ret = chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                  &hots_public_keys[i], &hots_secret_keys[i]);
        hots_keygen_time += (get_time_ms() - step_start);
        
        if (ret != 0) {
            printf("❌ chipmunk_hots_keygen failed for key %d\n", i);
            return -1;
        }
        
        // Progress indicator
        if (i > 0 && i % 10 == 0) {
            printf("   Progress: %d/%d keys (%.1f%%)\n", i, num_keys, (float)i * 100.0 / num_keys);
        }
    }
    
    double total_time = get_time_ms() - total_start;
    
    printf("\n📊 PROFILING RESULTS:\n");
    printf("   🔑 Total keys generated: %d\n", num_keys);
    printf("   ⏱️ Total time: %.3f seconds\n", total_time);
    printf("   ⏱️ Average per key: %.3f ms\n", total_time * 1000.0 / num_keys);
    printf("\n📈 BREAKDOWN:\n");
    printf("   🔧 chipmunk_keypair(): %.3f seconds (%.3f ms/key, %.1f%%)\n", 
           chipmunk_keypair_time, chipmunk_keypair_time * 1000.0 / num_keys,
           chipmunk_keypair_time / total_time * 100.0);
    printf("   🏠 chipmunk_hots_keygen(): %.3f seconds (%.3f ms/key, %.1f%%)\n", 
           hots_keygen_time, hots_keygen_time * 1000.0 / num_keys,
           hots_keygen_time / total_time * 100.0);
    
    // Cleanup
    free(private_keys);
    free(public_keys);
    free(hots_public_keys);
    free(hots_secret_keys);
    
    printf("\n✅ Key generation profiling completed!\n");
    return 0;
} 