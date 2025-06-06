/*
 * Test for Chipmunk Merkle Tree implementation
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dap_common.h"
#include "chipmunk_tree.h"
#include "chipmunk_hots.h"

#define LOG_TAG "test_chipmunk_tree"

static void print_test_result(const char *test_name, bool result) {
    if (result) {
        log_it(L_INFO, "🧪 %-30s: ✅ PASS", test_name);
    } else {
        log_it(L_ERROR, "🧪 %-30s: ❌ FAIL", test_name);
    }
}

/**
 * @brief Test HVC hasher initialization
 */
static bool test_hvc_hasher_init() {
    chipmunk_hvc_hasher_t l_hasher;
    uint8_t l_seed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                          17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

    int l_ret = chipmunk_hvc_hasher_init(&l_hasher, l_seed);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "   ❌ Failed to initialize HVC hasher: %d", l_ret);
        return false;
    }

    // Check that seed was stored
    if (memcmp(l_hasher.seed, l_seed, 32) != 0) {
        log_it(L_ERROR, "   ❌ Seed not stored correctly");
        return false;
    }

    // Check that matrix was generated (non-zero)
    bool l_has_nonzero = false;
    for (int i = 0; i < CHIPMUNK_HVC_WIDTH && !l_has_nonzero; i++) {
        for (int j = 0; j < CHIPMUNK_N && !l_has_nonzero; j++) {
            if (l_hasher.matrix_a[i].coeffs[j] != 0) {
                l_has_nonzero = true;
            }
        }
    }

    if (!l_has_nonzero) {
        log_it(L_ERROR, "   ❌ Matrix appears to be all zeros");
        return false;
    }

    log_it(L_INFO, "   ✅ HVC hasher initialized with non-zero matrix");
    return true;
}

/**
 * @brief Test HVC hash function
 */
static bool test_hvc_hash() {
    chipmunk_hvc_hasher_t l_hasher;
    uint8_t l_seed[32] = {0};
    
    // Initialize hasher
    int l_ret = chipmunk_hvc_hasher_init(&l_hasher, l_seed);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to initialize hasher\n");
        return false;
    }

    // Create test polynomials
    chipmunk_hvc_poly_t l_left, l_right, l_result;
    memset(&l_left, 0, sizeof(l_left));
    memset(&l_right, 0, sizeof(l_right));
    
    // Set some test values
    l_left.coeffs[0] = 100;
    l_left.coeffs[1] = 200;
    l_right.coeffs[0] = 50;
    l_right.coeffs[1] = 75;

    // Test hash function
    l_ret = chipmunk_hvc_hash_decom_then_hash(&l_hasher, &l_left, &l_right, &l_result);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Hash function failed: %d\n", l_ret);
        return false;
    }

    // Check that result is not all zeros
    bool l_has_nonzero = false;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (l_result.coeffs[i] != 0) {
            l_has_nonzero = true;
            break;
        }
    }

    if (!l_has_nonzero) {
        printf("   ❌ Hash result is all zeros\n");
        return false;
    }

    printf("   ✅ Hash function produces non-zero output\n");
    printf("   📊 First result coeffs: %d, %d, %d, %d\n", 
           l_result.coeffs[0], l_result.coeffs[1], l_result.coeffs[2], l_result.coeffs[3]);
    return true;
}

/**
 * @brief Test tree construction
 */
static bool test_tree_construction() {
    chipmunk_hvc_hasher_t l_hasher;
    uint8_t l_seed[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                          17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

    // Initialize hasher
    int l_ret = chipmunk_hvc_hasher_init(&l_hasher, l_seed);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to initialize hasher\n");
        return false;
    }

    // Create test leaf nodes
    chipmunk_hvc_poly_t l_leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
    for (int i = 0; i < CHIPMUNK_TREE_LEAF_COUNT_DEFAULT; i++) {
        memset(&l_leaf_nodes[i], 0, sizeof(chipmunk_hvc_poly_t));
        // Set unique values for each leaf
        l_leaf_nodes[i].coeffs[0] = i + 1;
        l_leaf_nodes[i].coeffs[1] = (i + 1) * 10;
        l_leaf_nodes[i].coeffs[2] = (i + 1) * 100;
    }

    // Create tree
    chipmunk_tree_t l_tree;
    memset(&l_tree, 0, sizeof(l_tree)); // Initialize structure
    l_ret = chipmunk_tree_new_with_leaf_nodes(&l_tree, l_leaf_nodes, CHIPMUNK_TREE_LEAF_COUNT_DEFAULT, &l_hasher);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to create tree: %d\n", l_ret);
        return false;
    }

    // Get root
    const chipmunk_hvc_poly_t* l_root = chipmunk_tree_root(&l_tree);
    if (!l_root) {
        printf("   ❌ Failed to get tree root\n");
        return false;
    }

    // Check that root is not all zeros
    bool l_has_nonzero = false;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (l_root->coeffs[i] != 0) {
            l_has_nonzero = true;
            break;
        }
    }

    if (!l_has_nonzero) {
        printf("   ❌ Tree root is all zeros\n");
        return false;
    }

    printf("   ✅ Tree constructed with %d leaves\n", CHIPMUNK_TREE_LEAF_COUNT_DEFAULT);
    printf("   📊 Root coeffs: %d, %d, %d, %d\n", 
           l_root->coeffs[0], l_root->coeffs[1], l_root->coeffs[2], l_root->coeffs[3]);
    return true;
}

/**
 * @brief Test proof generation and verification
 */
static bool test_proof_generation() {
    chipmunk_hvc_hasher_t l_hasher;
    uint8_t l_seed[32] = {42}; // Simple seed
    
    // Initialize hasher
    int l_ret = chipmunk_hvc_hasher_init(&l_hasher, l_seed);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to initialize hasher\n");
        return false;
    }

    // Create diverse leaf nodes
    chipmunk_hvc_poly_t l_leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
    for (int i = 0; i < CHIPMUNK_TREE_LEAF_COUNT_DEFAULT; i++) {
        memset(&l_leaf_nodes[i], 0, sizeof(chipmunk_hvc_poly_t));
        // Create more diverse values
        for (int j = 0; j < 4; j++) {
            l_leaf_nodes[i].coeffs[j] = (i + 1) * (j + 1) * 123;
        }
    }

    // Create tree
    chipmunk_tree_t l_tree;
    memset(&l_tree, 0, sizeof(l_tree)); // Initialize structure
    l_ret = chipmunk_tree_new_with_leaf_nodes(&l_tree, l_leaf_nodes, CHIPMUNK_TREE_LEAF_COUNT_DEFAULT, &l_hasher);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to create tree\n");
        return false;
    }

    const chipmunk_hvc_poly_t* l_root = chipmunk_tree_root(&l_tree);

    // Test proof generation for multiple indices
    for (size_t test_idx = 0; test_idx < 4; test_idx++) {
        chipmunk_path_t l_path;
        l_ret = chipmunk_tree_gen_proof(&l_tree, test_idx, &l_path);
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            printf("   ❌ Failed to generate proof for index %zu: %d\n", test_idx, l_ret);
            return false;
        }

        // Verify proof
        bool l_verify_result = chipmunk_path_verify(&l_path, l_root, &l_hasher);
        if (!l_verify_result) {
            printf("   ❌ Proof verification failed for index %zu\n", test_idx);
            return false;
        }

        printf("   ✅ Proof for index %zu verified successfully\n", test_idx);
    }

    return true;
}

/**
 * @brief Test HOTS public key to HVC conversion
 */
static bool test_hots_pk_conversion() {
    // Create a test HOTS public key
    chipmunk_public_key_t l_hots_pk;
    memset(&l_hots_pk, 0, sizeof(l_hots_pk));
    
    // Set some test values
    for (int i = 0; i < 10; i++) {
        l_hots_pk.v0.coeffs[i] = i * 1000;
        l_hots_pk.v1.coeffs[i] = i * 2000;
    }

    // Convert to HVC polynomial
    chipmunk_hvc_poly_t l_hvc_poly;
    int l_ret = chipmunk_hots_pk_to_hvc_poly(&l_hots_pk, &l_hvc_poly);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to convert HOTS PK to HVC: %d\n", l_ret);
        return false;
    }

    // Check conversion results
    bool l_has_expected_values = true;
    for (int i = 0; i < 10; i++) {
        int32_t l_expected = l_hots_pk.v0.coeffs[i] % CHIPMUNK_HVC_Q;
        if (l_expected < 0) l_expected += CHIPMUNK_HVC_Q;
        
        if (l_hvc_poly.coeffs[i] != l_expected) {
            printf("   ❌ Conversion mismatch at index %d: got %d, expected %d\n", 
                   i, l_hvc_poly.coeffs[i], l_expected);
            l_has_expected_values = false;
            break;
        }
    }

    if (!l_has_expected_values) {
        return false;
    }

    printf("   ✅ HOTS PK to HVC conversion successful\n");
    printf("   📊 First converted coeffs: %d, %d, %d, %d\n", 
           l_hvc_poly.coeffs[0], l_hvc_poly.coeffs[1], l_hvc_poly.coeffs[2], l_hvc_poly.coeffs[3]);
    return true;
}

/**
 * @brief Integration test with real HOTS keys
 */
static bool test_integration_with_hots() {
    printf("   🔧 Generating HOTS keys for tree integration test...\n");
    
    // Initialize Chipmunk
    int l_ret = chipmunk_init();
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to initialize Chipmunk\n");
        return false;
    }

    // Initialize hasher
    chipmunk_hvc_hasher_t l_hasher;
    uint8_t l_hasher_seed[32] = {100, 101, 102, 103, 104, 105, 106, 107, 
                                 108, 109, 110, 111, 112, 113, 114, 115,
                                 116, 117, 118, 119, 120, 121, 122, 123,
                                 124, 125, 126, 127, 128, 129, 130, 131};
    
    l_ret = chipmunk_hvc_hasher_init(&l_hasher, l_hasher_seed);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to initialize HVC hasher\n");
        return false;
    }

    // Generate HOTS keys and convert to HVC polynomials
    chipmunk_hvc_poly_t l_leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
    
    for (int i = 0; i < CHIPMUNK_TREE_LEAF_COUNT_DEFAULT; i++) {
        uint8_t l_hots_pk_bytes[CHIPMUNK_PUBLIC_KEY_SIZE];
        uint8_t l_hots_sk_bytes[CHIPMUNK_PRIVATE_KEY_SIZE];
        
        // Generate HOTS keypair
        l_ret = chipmunk_keypair(l_hots_pk_bytes, sizeof(l_hots_pk_bytes),
                                 l_hots_sk_bytes, sizeof(l_hots_sk_bytes));
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            printf("   ❌ Failed to generate HOTS keypair %d: %d\n", i, l_ret);
            return false;
        }

        // Deserialize public key
        chipmunk_public_key_t l_hots_pk;
        l_ret = chipmunk_public_key_from_bytes(&l_hots_pk, l_hots_pk_bytes);
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            printf("   ❌ Failed to deserialize HOTS public key %d: %d\n", i, l_ret);
            return false;
        }

        // Convert to HVC polynomial
        l_ret = chipmunk_hots_pk_to_hvc_poly(&l_hots_pk, &l_leaf_nodes[i]);
        if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
            printf("   ❌ Failed to convert HOTS PK %d to HVC: %d\n", i, l_ret);
            return false;
        }
    }

    printf("   ✅ Generated %d HOTS keys and converted to HVC polynomials\n", CHIPMUNK_TREE_LEAF_COUNT_DEFAULT);

    // Create Merkle tree with real HOTS public keys
    chipmunk_tree_t l_tree;
    memset(&l_tree, 0, sizeof(l_tree)); // Initialize structure
    l_ret = chipmunk_tree_new_with_leaf_nodes(&l_tree, l_leaf_nodes, CHIPMUNK_TREE_LEAF_COUNT_DEFAULT, &l_hasher);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to create tree with HOTS keys: %d\n", l_ret);
        return false;
    }

    const chipmunk_hvc_poly_t* l_root = chipmunk_tree_root(&l_tree);
    printf("   ✅ Created Merkle tree with HOTS public keys\n");
    printf("   📊 Tree root coeffs: %d, %d, %d, %d\n", 
           l_root->coeffs[0], l_root->coeffs[1], l_root->coeffs[2], l_root->coeffs[3]);

    // Test proof generation for middle index
    size_t l_test_index = CHIPMUNK_TREE_LEAF_COUNT_DEFAULT / 2;
    chipmunk_path_t l_path;
    l_ret = chipmunk_tree_gen_proof(&l_tree, l_test_index, &l_path);
    if (l_ret != CHIPMUNK_ERROR_SUCCESS) {
        printf("   ❌ Failed to generate proof for HOTS key at index %zu\n", l_test_index);
        return false;
    }

    // Verify proof
    bool l_verify_result = chipmunk_path_verify(&l_path, l_root, &l_hasher);
    if (!l_verify_result) {
        printf("   ❌ Failed to verify proof for HOTS key at index %zu\n", l_test_index);
        return false;
    }

    printf("   ✅ Successfully verified membership proof for HOTS key at index %zu\n", l_test_index);
    return true;
}

int main() {
    printf("\n🌳 === Chipmunk Merkle Tree Tests ===\n\n");
    
    clock_t l_start_time = clock();
    
    bool l_all_passed = true;
    
    // Run all tests
    bool l_test_result;
    l_test_result = test_hvc_hasher_init();
    print_test_result("HVC Hasher Init", l_test_result);
    l_all_passed &= l_test_result;
    
    l_test_result = test_hvc_hash();
    print_test_result("HVC Hash Function", l_test_result);
    l_all_passed &= l_test_result;
    
    l_test_result = test_tree_construction();
    print_test_result("Tree Construction", l_test_result);
    l_all_passed &= l_test_result;
    
    l_test_result = test_proof_generation();
    print_test_result("Proof Generation", l_test_result);
    l_all_passed &= l_test_result;
    
    l_test_result = test_hots_pk_conversion();
    print_test_result("HOTS PK Conversion", l_test_result);
    l_all_passed &= l_test_result;
    
    l_test_result = test_integration_with_hots();
    print_test_result("HOTS Integration", l_test_result);
    l_all_passed &= l_test_result;
    
    clock_t l_end_time = clock();
    double l_elapsed = ((double)(l_end_time - l_start_time)) / CLOCKS_PER_SEC;
    
    printf("\n📊 === Test Summary ===\n");
    printf("⏱️  Total time: %.3f seconds\n", l_elapsed);
    printf("🌳 Tree height: %d levels\n", CHIPMUNK_TREE_HEIGHT_DEFAULT);
    printf("🍃 Leaf count: %d nodes\n", CHIPMUNK_TREE_LEAF_COUNT_DEFAULT);
    printf("🔗 HVC modulus: %d\n", CHIPMUNK_HVC_Q);
    printf("📏 HVC width: %d\n", CHIPMUNK_HVC_WIDTH);
    
    if (l_all_passed) {
        printf("\n🎉 ALL TESTS PASSED! Merkle Tree implementation is working correctly.\n");
        return 0;
    } else {
        printf("\n💥 SOME TESTS FAILED! Please check the implementation.\n");
        return 1;
    }
} 