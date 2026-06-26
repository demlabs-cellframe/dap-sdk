/*
 * chipmunk_mixnet.h — Mixnet for metadata protection in consensus.
 *
 * Prevents timing and network-level deanonymization by batching and
 * shuffling signatures before publication.
 *
 * Two modes:
 * 1. Simple batching: collect → shuffle → publish
 * 2. Hierarchical DC-net: O(N√N) messages for full anonymity
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Parameters
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MIXNET_MAX_PARTICIPANTS 256
#define CHIPMUNK_MIXNET_BATCH_TIMEOUT_MS 5000  /* Max wait for batch */
#define CHIPMUNK_MIXNET_MIN_BATCH_SIZE   4     /* Minimum signatures for anonymity */

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/* Mixnet batch: collected signatures waiting to be shuffled */
typedef struct chipmunk_mixnet_batch {
    uint8_t *signatures[CHIPMUNK_MIXNET_MAX_PARTICIPANTS];
    size_t   sig_sizes[CHIPMUNK_MIXNET_MAX_PARTICIPANTS];
    uint32_t count;
    uint32_t capacity;
    bool     finalized;
} chipmunk_mixnet_batch_t;

/* DC-net round: dining cryptographers for full anonymity */
typedef struct chipmunk_dcnet_round {
    uint32_t round_id;                                                          /* Unique round identifier for deterministic pad derivation */
    uint8_t *shares[CHIPMUNK_MIXNET_MAX_PARTICIPANTS][CHIPMUNK_MIXNET_MAX_PARTICIPANTS];
    size_t   share_sizes[CHIPMUNK_MIXNET_MAX_PARTICIPANTS][CHIPMUNK_MIXNET_MAX_PARTICIPANTS];
    uint32_t participant_count;
    uint8_t *output;
    size_t   output_size;
} chipmunk_dcnet_round_t;

/* -------------------------------------------------------------------------
 * Simple Batching API
 * ---------------------------------------------------------------------- */

/**
 * Initialize a mixnet batch.
 * @param batch Output batch.
 * @param capacity Maximum number of signatures.
 * @return 0 on success.
 */
int chipmunk_mixnet_batch_init(chipmunk_mixnet_batch_t *batch, uint32_t capacity);

/**
 * Add a signature to the batch.
 * @param batch The batch.
 * @param sig Signature bytes.
 * @param sig_size Signature size.
 * @return 0 on success, -EAGAIN if batch is full.
 */
int chipmunk_mixnet_batch_add(chipmunk_mixnet_batch_t *batch,
                               const uint8_t *sig, size_t sig_size);

/**
 * Finalize and shuffle the batch.
 * Fisher-Yates shuffle with CSPRNG for unbiased permutation.
 * @param batch The batch to shuffle.
 * @return 0 on success.
 */
int chipmunk_mixnet_batch_shuffle(chipmunk_mixnet_batch_t *batch);

/**
 * Get shuffled signature by index.
 * @param batch The finalized batch.
 * @param index Index in shuffled order.
 * @param sig Output signature pointer (points into batch, do not free).
 * @param sig_size Output signature size.
 * @return 0 on success.
 */
int chipmunk_mixnet_batch_get(const chipmunk_mixnet_batch_t *batch,
                               uint32_t index,
                               const uint8_t **sig, size_t *sig_size);

/**
 * Free batch resources.
 */
void chipmunk_mixnet_batch_free(chipmunk_mixnet_batch_t *batch);

/* -------------------------------------------------------------------------
 * Hierarchical DC-net API (O(N√N) messages)
 * ---------------------------------------------------------------------- */

/**
 * Initialize a DC-net round.
 * @param round Output round.
 * @param participant_count Number of participants.
 * @return 0 on success.
 */
int chipmunk_dcnet_init(chipmunk_dcnet_round_t *round, uint32_t participant_count);

/**
 * Generate shares for a participant.
 * Each participant generates N-1 random shares and sends them to others.
 * @param round The round.
 * @param participant_index This participant's index.
 * @param own_message Participant's own message (signature to anonymize).
 * @param own_message_size Message size.
 * @return 0 on success.
 */
int chipmunk_dcnet_generate_shares(chipmunk_dcnet_round_t *round,
                                    uint32_t participant_index,
                                    const uint8_t *own_message,
                                    size_t own_message_size);

/**
 * Combine all shares to produce anonymized output.
 * output = XOR of all messages (untraceable to any participant).
 * @param round The round.
 * @return 0 on success.
 */
int chipmunk_dcnet_combine(chipmunk_dcnet_round_t *round);

/**
 * Get the anonymized output.
 * @param round The round.
 * @param output Output pointer (points into round, do not free).
 * @param output_size Output size.
 * @return 0 on success.
 */
int chipmunk_dcnet_get_output(const chipmunk_dcnet_round_t *round,
                               const uint8_t **output, size_t *output_size);

/**
 * Free DC-net round resources.
 */
void chipmunk_dcnet_free(chipmunk_dcnet_round_t *round);

#ifdef __cplusplus
}
#endif
