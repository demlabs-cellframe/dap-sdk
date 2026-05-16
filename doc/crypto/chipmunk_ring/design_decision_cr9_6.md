---
doc: design_decision_cr9_6
phase: CR-9.6 — Cellframe integration (SDK slice)
status: ACTIVE
predecessors:
  - doc/crypto/chipmunk_ring/design_decision_cr9.md
  - doc/crypto/chipmunk_ring/design_decision_cr9_4.md
  - doc/crypto/chipmunk_ring/design_decision_cr9_5.md
---

# CR-9.6 — Cellframe Integration (SDK Slice)

> **Scope split.**  Full on-chain Cellframe integration (governance
> certificate registry, mempool policy, block-validator dispatch for
> threshold artefacts) lives in the **Cellframe node repository** and
> requires Cellframe-team buy-in for wire-type registration.  **This
> slice** delivers everything the Cellframe team needs *from DAP SDK*
> without waiting on node changes: a stable `dap_enc_*` governance
> surface, PoP-enforced ring admission, and an updated integration
> guide with wire-format tables.

---

## 1. What Cellframe needs from DAP SDK

| Need | SDK deliverable (this slice) | Cellframe node (out of repo) |
|------|------------------------------|------------------------------|
| Governance t-of-n master key | `dap_enc_chipmunk_ring_governance_deal/combine_to_key` | Store shares in operator vault; policy engine |
| Rogue-key defence at ring join | `dap_enc_chipmunk_ring_member_pop_*` + `container_create_with_pop` | Validator rejects ring txs without valid PoP registry entries |
| Wire constants for storage | Tables in `cellframe_integration_guide.md` | `dap_global_db` cert records |
| Signing after combine | Existing `dap_enc_chipmunk_ring_sign` / `dap_sign` | Unchanged dispatch |

---

## 2. Public API (`dap_enc_chipmunk_ring_governance.h`)

Thin `dap_enc_key` wrappers over CR-9.4.A + CR-9.5:

```c
size_t dap_enc_chipmunk_ring_pop_wire_size(void);

int dap_enc_chipmunk_ring_governance_deal(...);
int dap_enc_chipmunk_ring_governance_combine_to_key(...);
int dap_enc_chipmunk_ring_member_pop_create(struct dap_enc_key *sk, ...);
int dap_enc_chipmunk_ring_member_pop_verify(struct dap_enc_key *pk, ...);
int dap_enc_chipmunk_ring_container_create_with_pop(
    struct dap_enc_key **pub_keys,
    const uint8_t *const *pops,
    size_t ring_size,
    chipmunk_ring_container_t *out_ring);
```

**Contract highlights:**

* `combine_to_key` allocates a fresh `dap_enc_key` with
  `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING`; caller owns it and must call
  `dap_enc_chipmunk_ring_key_delete`.
* `member_pop_create` requires `leaf_index == 0` on the underlying
  hypertree sk (same as CR-9.5); call **before** any production sign.
* `container_create_with_pop` is **fail-closed**: any missing PoP,
  wrong size, or verify failure aborts before ring hash allocation.

---

## 3. Cellframe governance flow (GOV-MULTI)

```
Dealer (trusted)                Participants (1..n)              Combiner
     |                                |                              |
     |-- governance_deal(seed,t,n) -->|  (OOB: share blob 72 B)      |
     |                                |                              |
     |                                |-- pop_create(sk_i) ---------->|  (registry)
     |                                |                              |
     |                                |         t shares OOB ------->|
     |                                |                              | combine_to_key
     |                                |                              | -> dap_enc_key
     |                                |                              | ring_sign / dap_sign
```

PoP is collected **per participant** at registry time, not at combine
time.  The combiner never needs participant private keys — only `t`
share blobs.

---

## 4. Wire-format reference (for Cellframe cert storage)

| Artefact | Magic (LE) | Total size | Version |
|----------|--------------|------------|---------|
| Threshold share | `'CRHS'` `0x53485243` | 72 B | 1 |
| Proof of Possession | `'CRRP'` `0x50525243` | 8 + `CHIPMUNK_HT_SIGNATURE_SIZE` | 1 |
| Partial sig (CR-9.4.B) | `'CRHP'` `0x50485243` | TBD | reserved |

Ring public key size: `CHIPMUNK_RING_PUBLIC_KEY_SIZE` (hypertree pk
bytes, currently ~2112 B).

---

## 5. Test acceptance (CR-9.6)

| Test | Contract |
|------|----------|
| `test_governance_deal_combine_roundtrip` | deal → combine_to_key → pub/priv sizes match ring constants |
| `test_governance_combine_signs` | combined key signs and verifies via `dap_enc_chipmunk_ring_sign` |
| `test_member_pop_dap_enc_roundtrip` | pop_create/verify on `dap_enc_key` |
| `test_container_with_pop_accepts_valid` | all valid PoPs → container_create succeeds |
| `test_container_with_pop_rejects_rogue` | PoP for pk_A presented with pk_B → rejected |

---

## 6. Decisions log

| # | Decision | Rationale |
|---|----------|-----------|
| D-1 | SDK-only slice; no `dap_sign_type_t` change in this phase | Avoids Cellframe validator break; threshold signing uses reconstructed master key + existing ring sig type |
| D-2 | PoP enforcement via opt-in `container_create_with_pop` | Keeps legacy callers working; governance path is explicitly strict |
| D-3 | `combine_to_key` returns heap `dap_enc_key*` | Matches existing Cellframe key lifecycle (`dap_enc_key_new` / delete callbacks) |
| D-4 | Integration guide is the hand-off artefact to Cellframe team | Single doc they can link from node MR |

---

## 7. Follow-up (Cellframe repo, not this slice)

- [ ] Governance cert record in `dap_global_db` (pk + pop + metadata)
- [ ] Mempool: reject anonymous ring tx if ring member lacks registry PoP
- [ ] Optional: `SIG_TYPE_CHIPMUNK_RING_THRESHOLD` when CR-9.4.B lands

---

*CR-9.6 SDK slice, 2026-05-16.*
