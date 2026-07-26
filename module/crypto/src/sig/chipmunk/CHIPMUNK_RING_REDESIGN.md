# Chipmunk Ring Redesign: Algebraic Aggregation + Halving Fold

## Problem

Ring V2 uses O(N) per-member structure: N×T, N×c, N×z. At N=128, signatures are ~272KB.
MRNG uses O(log N) halving fold: ~37KB at N=128.

## Solution

Use MRNG with threshold=1 for single-signer anonymous ring signatures.
MRNG already implements:
- Algebraic aggregation: X = Σ b_i * x_i
- Halving fold on augmented vector b̃ = (b, b∘(b-1))
- Logarithmic proof size: fold_depth = 1 + ceil(log2(N))
- Link tag: T = A_T * X
- Vector commitment: C_b = VCom(b; r_b)
- Bind block: z_x + c*

## Architecture

### Single-Signer Ring (threshold=1)

```
Statement: "I know sk_j for pk_j in {pk_0, ..., pk_{N-1}}"
Witness: b ∈ {0,1}^N (indicator, one 1), x = sk_j
Commitment: C_b = VCom(b; r_b)
Aggregate: X = Σ b_i * x_i = x_j (single key)
Link tag: T = A_T * X
Relation: Σ b_i * pk_i = A_pk * X = pk_j
Fold: halving fold on b̃ = (b, b∘(b-1)), depth = ceil(log2(N)) + 1
```

### Threshold Ring (threshold=t)

```
Statement: "I know sk_j for t of N keys in {pk_0, ..., pk_{N-1}}"
Witness: b ∈ {0,1}^N (indicator, t ones), X = Σ b_i * x_i
Aggregate: X = Σ_{i∈S} x_i (t keys)
Relation: Σ b_i * pk_i = A_pk * X
```

### Size Comparison

| N | Ring V2 O(N) | MRNG threshold=1 O(log N) | Savings |
|---|---|---|---|
| 8 | ~69 KB | ~23 KB | 3x |
| 16 | ~137 KB | ~28 KB | 5x |
| 32 | ~272 KB | ~31 KB | 9x |
| 64 | ~542 KB | ~34 KB | 16x |
| 128 | ~1084 KB | ~37 KB | 29x |
| 256 | ~2168 KB | ~39 KB | 55x |

## Status

This replacement proposal is not part of the Chipmunk Ring implementation.
Chipmunk Ring remains the independent CRIN signature exposed by
`chipmunk_ring.h`. MRNG is exposed only through `chipmunk_mring.h`; no MRNG
wrappers or LRS key bridge belong in the Chipmunk Ring API.

## Key Files

- `chipmunk_mring.c` — MRNG sign/verify (threshold t-of-N)
- `chipmunk_mring_fold.h/c` — halving fold prove/verify
- `chipmunk_mring_statement.h/c` — vector commitment, bind block
- `chipmunk_lrs.h/c` — LRS key types, witness derivation
- `chipmunk_ring.c` — Ring V2 (to be replaced)

## Security

- MSIS/MLWE ≥ 128 bits (quantum)
- Fold soundness: ~125 bits (subtractive set over R_q^{(e)})
- Link tag: T = A_T * X (deterministic, MSIS-hard to forge)
- QROM Fiat-Shamir: G4 open obligation (125-bit margin likely sufficient)
