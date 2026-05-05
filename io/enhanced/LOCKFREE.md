# Lock-Free I/O Engine: Safety Model and Protocols

This document uses the `dap-sdk/io/enhanced` codebase as a concrete example
of how to build a deadlock-free, timer-compensator-free multi-thread I/O
system with lock-free primitives.  For each protocol: both sides are shown
in parallel, the memory ordering guarantee is stated explicitly, and the
reason the system self-recovers without external nudges is explained.

---

## 1. Buffer Anatomy

### 1.0  Cache-line partitioning: the problem and the rule

**The problem — false sharing.**

A cache line on x86-64 and ARM64 is 64 bytes.  The cache coherence protocol
(MESI) operates at cache-line granularity: when any core writes to any byte
in a line, the entire 64-byte line is invalidated in every other core's cache.

```
Core 0 (producer)         Core 1 (consumer)
─────────────────         ──────────────────
writes tail_gen           reads tail_gen
                          writes head_gen

Without partitioning — both fields on the SAME cache line:

  ┌────────────────────────────────────────────────────────────┐
  │  tail_gen (8B)  │  head_gen (8B)  │  ... padding ...       │
  └────────────────────────────────────────────────────────────┘
  ↑ one 64-byte cache line

  Core 0 writes tail_gen → line state: MODIFIED on Core 0, INVALID on Core 1
  Core 1 reads  tail_gen → cache miss: must fetch the line from Core 0
  Core 1 writes head_gen → line state: MODIFIED on Core 1, INVALID on Core 0
  Core 0 reads  tail_gen → cache miss: must fetch the line from Core 1
  ... repeats on every push/drain pair ...

  Cross-core fetch latency: ~100–300 cycles (vs 4 cycles for L1 hit).
  On a hot queue with push+drain running in parallel, every operation
  pays this penalty even though the two fields are never read by the same
  core at the same time.  This is false sharing.
```

**The fix — partition by writer identity.**

```
  ┌───────────────────────────── CL 0 (64 B) ──────────────────────────────┐
  │  tail_gen   written by producer on EVERY push                          │
  │  (padding to 64 B)                                                     │
  └─────────────────────────────────────────────────────────────────────────┘
  ┌───────────────────────────── CL 1 (64 B) ──────────────────────────────┐
  │  head_gen   written by consumer on EVERY drain                         │
  │  (padding to 64 B)                                                     │
  └─────────────────────────────────────────────────────────────────────────┘

  Core 0 writes CL 0 → CL 1 is unaffected on Core 1
  Core 1 writes CL 1 → CL 0 is unaffected on Core 0
  Each core keeps its own line in MODIFIED state continuously.
  Cross-core traffic: only when one side needs to read the other's line
  (producer checking head on buffer-full; consumer reading tail on drain).
```

**Three partitioning classes used in this codebase:**

| Class | Written by | Read by | Placement |
|---|---|---|---|
| **producer-hot** | producer, every operation | consumer (rarely — on drain) | CL 0 |
| **consumer-hot** | consumer, every operation | producer (rarely — on full check) | CL 1 |
| **cold / rendezvous** | both sides, rarely | both sides | CL 2+ |

Cold fields (backpressure flags, capacity, metadata) get their own line
to avoid polluting the hot lines even with infrequent writes.
Fields written by *both* sides (Dekker rendezvous: `ack_pos`,
`producer_waiting`) are isolated on CL 2 for the same reason — a write
from either core must not stale the other core's hot line.

**Enforcement in code** — `_Alignas(DAP_VMQ_CACHELINE)` before each
partition forces the compiler and linker to place the field at a
64-byte-aligned offset, and the `_Static_assert` checks verify the
actual byte offsets match the intended layout:

```c
_Static_assert(offsetof(dap_vmqueue_t, tail_gen) % DAP_VMQ_CACHELINE == 0, …);
_Static_assert(offsetof(dap_vmqueue_t, head_gen) % DAP_VMQ_CACHELINE == 0, …);
_Static_assert(offsetof(dap_vmqueue_t, data)     % DAP_VMQ_CACHELINE == 0, …);
_Static_assert(sizeof(dap_conn_t) == 2 * DAP_VMQ_CACHELINE, …);
```

The asserts are compile-time safety nets: if a field is accidentally
added between two partitions and pushes a boundary past 64 bytes,
the build fails immediately.

---

### 1.1  SPSC message queue (`dap_vmqueue_t`)

The entire producer/consumer synchronisation lives in two 8-byte atomic
words, each padded to its own cache line to eliminate false sharing.

```
Byte layout of dap_vmqueue_t (heap allocation via mmap):

  offset  0  ┌─────────────────────────────────────────────── CL 0 (producer-hot) ─┐
             │  tail_gen  _Atomic(uint64_t)                                         │
             │  ┌──────────────────┬──────────────────────────────────────────────┐ │
             │  │   gen  (hi 32)   │           byte offset  (lo 32)               │ │
             │  └──────────────────┴──────────────────────────────────────────────┘ │
  offset 64  ├─────────────────────────────────────────────── CL 1 (consumer-hot) ─┤
             │  head_gen  _Atomic(uint64_t)  (same layout as tail_gen)             │
  offset 128 ├─────────────────────────────────────────────── CL 2 (backpressure) ─┤
             │  producer_waiting  _Atomic(uint32_t)                                 │
             │  shutdown          _Atomic(uint32_t)                                 │
             │  capacity          size_t                                             │
  offset 192 ├──────────────────────────────────────────────── data (flexible) ────┤
             │                                                                       │
             │  [hdr8|payload][pad] [hdr8|payload][pad] ...  [     free      ]      │
             │   ^                                      ^      ^               ^     │
             │ head_off                              tail_off                cap     │
             └───────────────────────────────────────────────────────────────────────┘

Message header (8 bytes, 8-byte aligned):
  ┌─────────┬─────────┬──────────────────────────┐
  │ type:1B │  pri:1B │     total_len:4B          │  then payload[total_len-6], pad to 8B
  └─────────┴─────────┴──────────────────────────┘

Generation wrap: when tail_off + msg_aligned > capacity, producer
  resets tail_off=0 and bumps gen.  Consumer detects gen mismatch
  on next drain_begin and resets head to 0 — buffer appears empty
  until producer re-fills from the start.
```

### 1.2  MPSC queue: lane control blocks

```
Full allocation (one contiguous mmap region):

  ┌──────────────────────────────────────────────────────────────────────┐
  │  dap_vmqueue_mpsc_t header  (n_lanes, offsets, notify_latch, ...)    │
  │  lane_off[n+1]  (cumulative data sizes for lane 0..n-1)              │
  ├───────────────── ctrl_offset ────────────────────────────────────────┤
  │  Lane 0 ctrl  [192 B = 3 cache lines]                                │
  │  ┌─── CL 0 ────────────────────────────────────────────────────────┐ │
  │  │  tail_gen  _Atomic(uint64_t)   ← producer writes every push     │ │
  │  ├─── CL 1 ────────────────────────────────────────────────────────┤ │
  │  │  head_gen  _Atomic(uint64_t)   ← consumer writes every drain    │ │
  │  ├─── CL 2 ────────────────────────────────────────────────────────┤ │
  │  │  producer_waiting  _Atomic(uint32_t)   ← futex backpressure     │ │
  │  └─────────────────────────────────────────────────────────────────┘ │
  │  Lane 1 ctrl  [192 B]  ...  Lane n-1 ctrl  [192 B]                   │
  ├───────────────── data_offset ────────────────────────────────────────┤
  │  Lane 0 data  [lane_off[1] - lane_off[0] bytes]                      │
  │  Lane 1 data  [lane_off[2] - lane_off[1] bytes]                      │
  │  ...                                                                  │
  │  Lane n-1 data                                                        │
  └──────────────────────────────────────────────────────────────────────┘

WFQ standard layout for N workers (3N lanes total):

  Lane index:  0 ... N-1    N ... 2N-1    2N ... 3N-1
               ┌──────────┐ ┌──────────┐  ┌──────────┐
               │  FAST    │ │  NORM    │  │   BG     │
               │ w0..wN-1 │ │ w0..wN-1 │  │ w0..wN-1 │
               └──────────┘ └──────────┘  └──────────┘
  Quotas:        32/lane       16/lane        8/lane    (per drain pass)
```

### 1.3  Receive OLB (`recv_olb`) — worker fills, processor consumes

```
Linear buffer (no ring), compacted on demand:

  ┌──────────────────────────────────────────────────────────────┐
  │ freed  │  acked  │   committed frames   │ partial │   free   │
  └──────────────────────────────────────────────────────────────┘
  ^        ^         ^                      ^         ^
  0   head_pos    ack_pos               tail_pos  write_end     capacity

  Cursor ownership:
    write_end  — worker local (non-atomic); includes any partial frame
    tail_pos   — worker publishes (release) after parsing complete frames
    ack_pos    — processor publishes (seq_cst) after consuming [ack_pos, tail_pos)
    head_pos   — worker updates (relaxed) after applying ack; base for free-space calc

  Compaction (memmove to 0):
    Triggered when write_end nears capacity AND head_pos >= tail_pos
    (all committed data consumed by processor).
    Partial frame [tail_pos, write_end) is memmoved to offset 0.
    All cursors reset: head=ack=tail=0, write_end=partial_len.

  Watermark early-compact:
    When remaining space drops below compact_threshold, worker attempts compact
    proactively — avoids blocking the recv path entirely.

  Cache lines:
    CL 0: tail_pos (producer/worker-hot)
    CL 1: head_pos, capacity, compact_threshold, write_end (consumer/worker-hot)
    CL 2: ack_pos, watermark_pending (cross-thread rendezvous: processor writes ack_pos,
          worker raises watermark_pending hint)
```

### 1.4  Send OLB (`send_olb`) — processor writes, worker flushes

```
Linear buffer with wrap-around via compacted flag:

  Normal state:
  ┌──────────────────────────────────────────────────────────────┐
  │  flushed (freed)  │        pending to send         │  free   │
  └──────────────────────────────────────────────────────────────┘
  ^                   ^                                ^          ^
  0               head_pos                         tail_pos   write_end=capacity

  After processor wrap-around (write_end reset to 0):
    compacted=1 (release) signals worker to reset head_pos to 0
    Worker sees compacted → head=0 → reads from offset 0 again

  Cache lines:
    CL 0: tail_pos, write_end, compacted (processor-hot)
    CL 1: head_pos, capacity              (worker-hot)
    (no CL 2 — no ack_pos; backpressure via SEND_BUSY flag in dap_conn_t)
```

### 1.5  Connection state bits (`dap_conn_t.state`)

```
  Bit  Flag            Set by       Cleared by   Meaning
  ───  ─────────────   ──────────   ──────────   ─────────────────────────────────
   0   RECV_DONE       worker       –            EOF from peer; stop reading
   1   SUSPENDED       worker       worker        recv OLB full; pause EPOLLIN
   2   CLOSED          worker       –            transport/fatal close; worker stops I/O
   3   SEND_BUSY       processor    worker        send_olb has data; kick flush
   4   PURGE           worker       –            fatal protocol condition; drop all pending tasks
   5   RESCAN          worker       worker        WFQ push failed; needs retry

  dap_conn_t layout (128 bytes = 2 cache lines):

  CL 0 [0..63]   — cross-thread atomics (processor + worker both touch)
    _Atomic(uint32_t)  generation   // bumped on alloc; stale-task guard
    _Atomic(uint8_t)   state        // flag byte above
    dap_conn_ext_dtor_t ext_dtor    // cold: quarantine drain only
    void               *_owner      // owning worker ptr, write-once

  CL 1 [64..127] — worker hot path (read-only after setup)
    dap_fd_t            fd
    read_cb / write_cb / error_cb
    dap_vmqueue_olb_t  *olb          // recv buffer
    dap_vmqueue_olb_t  *send_olb     // send buffer
    void               *ext          // protocol context
```

---

## 2. Handshake Protocols

Each protocol section shows the two sides in adjacent columns.
**Invariant** states what property must never be violated.
**Guarantee** explains why a lost wakeup or deadlock is impossible.

---

### 2.1  SPSC non-blocking push → drain

```
Invariant: consumer sees every message pushed before commit,
           producer never overwrites unconsumed data.

PRODUCER (worker thread)               CONSUMER (processor thread)
──────────────────────────────────     ──────────────────────────────────────

l_tg = load(tail_gen, relaxed)         s_vmq_drain_begin():
l_off = VMQ_OFF(l_tg)                    l_tg = load(tail_gen, ACQUIRE)  ← [A]
l_gen = VMQ_GEN(l_tg)                    l_hg = load(head_gen, relaxed)
                                          if GEN(l_tg) != GEN(l_hg):
if l_off + aligned > capacity:              head = 0
  l_hg = load(head_gen, ACQUIRE)  ← [B]    commit_head(head=0) ← wake pw
  if l_hg != l_tg: return false            (generation wrap reset)
  // wrap: reset offset, bump gen
  store(tail_gen, PACK(gen+1, 0),   while (peek available):
        RELEASE)              ← [C]    dispatch(hdr)
                                        head += step
write hdr + payload to data[]         commit_head(head_gen=PACK(gen,head),
store(tail_gen, PACK(gen,              RELEASE)    ← [D]
      l_off+aligned), RELEASE) ← [C]
                                 OR (empty lane):
                                   s_vmq_ack_waiter(pw)  // see §2.2

Memory order pairs:
  [C] producer release  →  [A] consumer acquire:
      guarantees consumer sees payload bytes written before tail_gen commit.
  [D] consumer release  →  [B] producer acquire (on next full-buffer check):
      guarantees producer sees freed space after consumer advances head.
```

---

### 2.2  SPSC blocking push: Dekker backpressure

```
Invariant: producer never spins; exactly one futex_wake per block.
           No wakeup is lost even if consumer drains between pw=1 and futex_wait.

PRODUCER                               CONSUMER (drain side)
──────────────────────────────────     ──────────────────────────────────────

// fast path failed (buffer full)      // after each drain pass:
store(pw, 1, SEQ_CST)      ← [1]      commit_head(...):
if a_wake: a_wake(wfq)                   store(head_gen, ..., RELEASE)
                                          if load(pw, SEQ_CST) != 0: ← [2]
retry push:                                 CAS(pw, 1→0, SEQ_CST, ACQUIRE)
  if success:                               futex_wake(pw, 1)
    store(pw, 0, relaxed)
    a_wake(wfq)            ← notify    // OR on empty lane:
    return true                        s_vmq_ack_waiter(pw):
                                         if !load(pw, SEQ_CST): return  ← [3]
if shutdown: return false                CAS(pw, 1→0, SEQ_CST, ACQUIRE)
                                         futex_wake(pw, 1)
// still full after retry:
futex_wait(pw, 1)          ← [4]
  // wakes when pw != 1
store(pw, 0, relaxed)
l_wait_armed = false
goto retry push

Why no lost wakeup (Dekker race closure):

  Race window: consumer drains between [1] and [4].
  Without SEQ_CST, producer's store(pw=1) might not be visible at [2].

  With SEQ_CST on both [1] and [2]:
    Case A — consumer sees pw=1 at [2]:
      Consumer does CAS(pw→0) + futex_wake.
      Producer retries push (now succeeds or finds pw=0 and skips futex_wait).

    Case B — consumer completes drain before [1]:
      Producer retries push at "retry push" above; buffer has space now → success.
      pw stays 0; futex_wait never called.

    Case C — producer's SEQ_CST store [1] completes before consumer's load [2]:
      [2] sees pw=1 → wake path executes → producer unblocks.

  The double-retry ("retry push" before futex_wait) closes the window
  where consumer drained after store(pw=1) but before the retry.
  If retry succeeds, futex_wait is never called.
```

---

### 2.3  MPSC notify-latch: worker → processor wakeup

```
Invariant: no push is invisible to the processor; processor never
           sleeps while there are unconsumed messages.

PRODUCER (worker, any thread)          CONSUMER (processor loop)
──────────────────────────────────     ──────────────────────────────────────

// after mpsc_push():                  // main loop idle path:
dap_vmqueue_mpsc_notify(wfq):
  old = exchange(notify_latch,         drain all lanes → 0 messages found
                 1, RELEASE)  ← [1]
  if old == 0:                         store(notify_latch, 0,
    futex_wake(notify_latch, 1)            RELEASE)             ← [2]

                                       re-drain all lanes       ← [3]

                                       if found > 0: continue loop

                                       load(notify_latch,
                                            ACQUIRE)            ← [4]
                                       if latch != 0: continue loop

                                       futex_wait(notify_latch, 0) ← [5]

Why no lost wakeup:

  Race: worker pushes between [2] and [5].
  Without the re-drain [3] and load [4], processor could sleep with data present.

  With the sequence [2]→[3]→[4]→[5]:
    If worker's exchange [1] happens before [2]:
      latch is 1, processor sees it at [4] and does not sleep.
    If worker's exchange [1] happens after [2] but before [4]:
      Exchange returns old=0 → futex_wake fired.
      Processor sees latch=1 at [4], skips futex_wait.
    If worker's exchange [1] happens between [4] and [5]:
      futex_wait sees latch=1 (val != expected=0) → returns immediately.
    If worker's exchange [1] happens after [5]:
      futex_wake wakes the sleeping processor.

  The 0→1 transition on exchange guarantees exactly one futex_wake per
  "new data epoch".  Burst amortisation: subsequent pushes in the same
  epoch see exchange returning 1 → no syscall.
```

---

### 2.4  Recv OLB compaction: Dekker CONN_SUSPENDED

```
Invariant: worker never loses recv buffer space permanently.
           Processor never misses a SUSPENDED connection.

WORKER (recv path)                     PROCESSOR (batch ack path)
──────────────────────────────────     ──────────────────────────────────────

try_space() → DAP_OLB_FULL:           // after consuming batch data:
  // no space in recv OLB             dap_vmqolb_ack(olb, bytes):
                                         l_ack = load(ack_pos, relaxed)
fetch_or(state,                          store(ack_pos,
         SUSPENDED, SEQ_CST) ← [1]           l_ack + bytes,
                                             SEQ_CST)           ← [3]
// Dekker re-check:                    // check if worker is suspended:
load(ack_pos, SEQ_CST)      ← [2]     load(state, SEQ_CST)     ← [4]
apply_ack() → head advanced?           if state & SUSPENDED:
  compact → space available               set pending_bits[conn_idx]
  fetch_and(~SUSPENDED, release)           kick worker eventfd
  return OK                           // (also: watermark_pending path)

if still no space:
  skip recv (EPOLLIN stays armed      // worker wakes via eventfd:
  but we return without reading)      // resume_conn read_cb calls
                                      // drain_pending → apply_ack,
                                      // clear SUSPENDED, call read_cb

Why no permanent suspension:

  Race: processor writes ack_pos between worker's fetch_or [1] and load [2].

  With SEQ_CST on both [1] and [3]:
    Case A — processor's store [3] happens before worker's load [2]:
      Worker sees new ack at [2], compacts, clears SUSPENDED itself.
      No kick needed.
    Case B — worker's fetch_or [1] happens before processor's load [4]:
      Processor sees SUSPENDED at [4], kicks worker unconditionally.
    Case C — total order of SEQ_CST guarantees one of A or B always holds.

  The Dekker re-check [2] after setting SUSPENDED [1] closes the window
  where processor acked but didn't see SUSPENDED yet (then would miss the kick).
```

Implementation detail:
  In code, the processor-side "is worker suspended?" check is performed inside
  `dap_worker_conn_notify_send()` (called from `dap_io_tx_send*()`) via
  `fetch_or(SEND_BUSY, seq_cst)` and `old & SUSPENDED`.  If suspended, it sets
  `pending_bits` and kicks the worker.

---

### 2.5  Send path: SEND_BUSY backpressure

```
Invariant: data written to send_olb by the processor is always
           eventually flushed; no data silently lost.

PROCESSOR                              WORKER (event loop)
──────────────────────────────────     ──────────────────────────────────────

dap_io_tx_send(handle, ...):            // epoll fires EPOLLOUT or resume kick:

  write → send_olb                     dap_worker_tx_flush(conn):
  old = fetch_or(state,                  // flush loop:
                 SEND_BUSY,              while (flush(send_olb, fd) > 0)
                 SEQ_CST)    ← [1]           flushed++;
  if (old & SUSPENDED)                  // send_olb drained to empty:
     || !(old & SEND_BUSY):             old = fetch_and(~SEND_BUSY,
    set pending_bits[conn_idx]                          SEQ_CST) ← [2]
    kick worker eventfd                 // Dekker re-flush:
                                        while (flush(send_olb, fd) > 0)
                                            flushed++;
                                        if EAGAIN:
                                          fetch_or(SEND_BUSY, release)
                                          // EPOLLOUT will re-trigger

                                        if (old & SEND_BUSY):
                                          dap_wfq_wake(wfq)  // wake processor
                                          // deferred batches can now retry

Why no stalled data:

  Race: processor writes more data to send_olb between worker's
        "drained to empty" check and clearing SEND_BUSY [2].

  With SEQ_CST on [1] and [2]:
    The Dekker re-flush after [2] catches any data written between
    the last empty-check and [2].
    If SEND_BUSY was already 0 before [1], processor's fetch_or [1]
    returns 0 → kick is always sent on first write.
    If worker cleared SEND_BUSY and processor wrote again:
      fetch_or [1] returns 0 (bit was clear) → kick sent.

  After clearing SEND_BUSY, wfq_wake signals the processor that
  send_olb space is available — deferred batches waiting on
  SEND_OLB_FULL can now retry.
```

---

### 2.6  WFQ push backpressure: rescan_mask

```
Invariant: a worker whose non-blocking push failed is never forgotten;
           the processor re-signals it after draining creates space.

WORKER (push path)                     PROCESSOR (post-drain path)
──────────────────────────────────     ──────────────────────────────────────

dap_worker_push_batch() → false:      // after any successful drain:
  // WFQ NORM lane full               if load(rescan_mask, relaxed) != 0:
  fetch_or(state, RESCAN, release)      mask = exchange(rescan_mask,
  fetch_or(rescan_mask,                              0, ACQUIRE) ← [2]
           1<<worker_id,               for each bit in mask:
           RELEASE)          ← [1]       kick worker_kick_fds[bit]
  mpsc_notify(wfq)    // wake proc
  set pending_bits[conn_idx]   // worker will retry in drain_pending

// worker woken by kick:
dap_worker_drain_pending():
  if state & RESCAN:
    push_batch() → success?
      fetch_and(~RESCAN, release)
      if state & SUSPENDED:
        fetch_and(~SUSPENDED, release)
        call read_cb()         // resume recv
    → still full?
      set pending_bits again,
      fetch_or(rescan_mask, 1<<id)  ← back to top

Why no permanent RESCAN:

  After drain [2] consumed messages and freed lane space,
  the kick reaches the worker's eventfd.
  Worker retries push — succeeds because space was freed.
  If it fails again (multiple producers, fast refill),
  the cycle repeats: worker re-arms rescan_mask, processor
  will see it again after the next drain.

  The mpsc_notify at [1] guarantees the processor wakes if
  it is sleeping — so it will drain and subsequently kick.
```

---

### 2.7  Connection slot safety: quarantine + `wfq_seq`

```
Invariant: a slab slot is never reused while the processor may still
           hold a reference to the old connection (via in-flight batch).

WORKER (conn_del path)                 PROCESSOR (drain cycle)
──────────────────────────────────     ──────────────────────────────────────

epoll_ctl(DEL, fd)                     // after drain finds no work:
swap-remove from conns[]               epoch = fetch_add(slab->wfq_epoch, 1) + 1
                                       slab_drain(slab, epoch):
epoch = load(slab->wfq_epoch, relaxed)   for each quarantine entry:
slab_free(idx, epoch + 1)                 if epoch >= entry.wfq_seq:
  → slot enters quarantine                   slot_cleanup(ext_dtor, OLBs)
                                             entry → ready zone

Error path (conn never used in WFQ):   dap_conn_slab_get(slab, idx, gen):
slab_return(slab, conn)                  l_c = slot(slab, idx)
  wfq_seq = 0 → immediately drainable   return (l_c->generation == gen)
                                                ? l_c : NULL
                                       // if slab_get returns NULL:
                                       //   batch is STALE, dropped safely

Why it is safe to reuse:

  wfq_epoch is incremented by the processor when it finds no work
  (all lanes drained).  The worker stores epoch+1 at close time.
  This means the processor must complete at least one full idle
  transition after the connection was removed — by which point all
  WFQ tasks referencing the connection have been consumed.

  If the batch arrives after close: generation mismatch → STALE.
  If the batch arrives before close: processed before recycle.
  Either way: no use-after-free.
```

---

### 2.8  Synchronous processing mode (`DAP_CONN_SYNC`)

```
By default, connections use the async pipeline:
  worker recv → OLB → push_batch → WFQ → processor → frame_cb → ack

With DAP_CONN_SYNC, the worker processes data inline:
  worker recv → OLB → inline ack (no WFQ, no processor)

The user's read_cb handles processing and writes to send_olb directly.

SYNC → ASYNC:
  clear DAP_CONN_SYNC — immediate, no in-flight state.

ASYNC → SYNC (two-phase):
  1. set DAP_CONN_SYNC
  2. new recv blocked (event loop: SYNC && ack_pos < tail_pos → skip)
  3. processor drains remaining in-flight batches via dap_vmqolb_ack
  4. ack_pos >= tail_pos → sync_ready, recv resumes
  5. first try_space does apply_ack (head = ack), then worker acks inline via
     dap_vmqolb_sync_ack which keeps head == ack == tail at all times

Orthogonal to SUSPENDED: both block recv independently.
  SUSPENDED resolves via OLB space freed.
  SYNC resolves via ack_pos >= tail_pos (processor finished).

SYNC + OLB_FULL is split into two cases:
  - True oversize frame (`bytes_needed > capacity`):
      CLOSED | PURGE, error_cb(conn, EMSGSIZE), worker closes fd.
  - Transient low-space condition (including parser hint `bytes_needed == 0`):
      no fatal close; worker sets pending_bits + rescan_mask, notifies processor,
      and retries via pending/rescan path.

In SYNC mode the Dekker SUSPENDED path is intentionally skipped.
```

---

### 2.9  Watermark kick coalescing (`pending_bits` 0→1)

```
Invariant: watermark pressure does not spam worker wakeups.

WORKER (try_space)                     PROCESSOR (after batch ack)
──────────────────────────────────     ──────────────────────────────────────

free < threshold, free >= threshold/2:
  store(watermark_pending, true, release)

                                      if load(watermark_pending, acquire):
                                        if set_if_new(pending_bits[conn_idx]):
                                          kick worker eventfd

WORKER (resume path):
  drain_pending() grabs pending_bits words via exchange(word, 0),
  which clears the bit and allows the next watermark epoch to kick again.

Effect:
  Multiple watermark observations for the same connection collapse into
  one kick while its pending bit remains set.

Note:
  watermark_pending is a hint, not a stop condition; recv stays enabled.

Implementation notes:
  1) `set_if_new(pending_bits[idx])` coalesces kicks by pending-bit lifecycle,
     not by "watermark epochs".  A new kick becomes eligible only after
     worker-side `drain_pending()` clears the bit via `exchange(word, 0)`.
  2) In `dap_worker_rx_olb()`, publish/ack progress is gated by actual tail
     movement (`tail_after > tail_before`), not by parser return count alone.
     This keeps progress correct even when a parser consumes bytes but reports
     zero logical items.
```

---

## 3. Self-Resolving Deadlock Freedom

### 3.1  The ACK-before-sleep pattern

Every sleeping path in the system follows the same three-step sequence:

```
  Step 1: ACK — publish "I am done draining / I have freed space"
  Step 2: re-drain — consume anything the other side sent between last
          drain and ACK (close the race window)
  Step 3: check condition — only sleep if nothing arrived in step 2

This is the Dekker mutual exclusion applied to sleep/wake:

  Thread A (going to sleep)        Thread B (sending work)
  ─────────────────────────        ─────────────────────────
  store(latch, 0, release)  [1]    push data
  re-drain                  [2]    exchange(latch, 1, release) [3]
  load(latch, acquire)      [4]    if old == 0: futex_wake
  if latch==0: futex_wait

  If [3] happens before [1]: latch is 1 after [1]; [4] sees 1 → no sleep.
  If [3] happens between [1] and [4]: [4] sees 1 → no sleep.
  If [3] happens after [4]: futex value != expected → futex_wait returns.
  If [3] happens after futex_wait entry: futex_wake fires.

  The re-drain [2] handles the case where B sent work before [1]
  but the push completed after the previous drain pass ended.
```

Instances of this pattern in the codebase:

| Sleep point              | ACK operation                           | Re-drain         |
|--------------------------|----------------------------------------|------------------|
| `futex_wait(notify_latch)` | `store(notify_latch, 0, release)`    | `proc_drain()`   |
| `futex_wait(pw, 1)`       | implicit: drain commits head           | retry push        |
| worker skip recv (SUSPENDED) | `fetch_or(SUSPENDED, seq_cst)`    | `load(ack_pos, seq_cst)` |
| worker EPOLLOUT wait      | `fetch_and(~SEND_BUSY, seq_cst)`       | re-flush loop     |

### 3.2  Why there are no timer-based compensators

A level-triggered design periodically pokes all connections or queues
to check whether they are stuck.  This has several problems:

- **Latency**: a stuck connection waits up to one timer interval.
- **Scalability**: O(N) scan cost per tick regardless of actual activity.
- **Correctness debt**: the timer is a load-bearing correctness mechanism,
  not just a diagnostic — removing it causes deadlocks.

This engine has no such timers.  Instead, every "unstuck" action is
triggered by the event that creates the precondition:

| Stuck condition              | Trigger that resolves it                     |
|------------------------------|----------------------------------------------|
| Processor sleeping, new data | Worker's `mpsc_notify` after push            |
| Worker SUSPENDED, ack ready  | Processor-side `dap_io_tx_send*()` / kick     |
| Worker RESCAN, lane drained  | Processor's `dap_proc_kick_rescan_workers`   |
| Producer blocked on pw       | Consumer's `commit_head` → `ack_waiter`      |
| Defer retry needed           | Worker's flush clears SEND_BUSY → `wfq_wake` |

Each trigger fires **at the moment the condition changes**, not periodically.
The only timers in the system (`dap_timers_t`) are application-level
(keepalive, protocol deadlines) — they carry no deadlock-safety role.

### 3.3  Shutdown correctness

Shutdown follows the same edge-triggered principle:

```
dap_proc_shutdown():
  store(shutdown, true, release)         // signal
  exchange(notify_latch, 1, release)     // wake processor if sleeping
  futex_wake(notify_latch, 1)

Processor loop on shutdown:
  force_complete = true
  drain defer queue (force-execute all deferred batches)
  drain WFQ lanes until empty ((size_t)-1 quota — no starvation)
  drain ext-stack pending list
  // exits loop only when all queues are empty
```

`force_complete=true` bypasses SEND_OLB_FULL checks, ensuring every
deferred BATCH task reaches completion and its OLB reference is released
before the buffers are destroyed.

---

## 4. Memory Ordering Reference

| Atomic operation                    | Ordering     | Paired with                         | Guarantee                                      |
|-------------------------------------|--------------|-------------------------------------|------------------------------------------------|
| `store(tail_gen)` — push commit     | release      | `load(tail_gen)` — drain begin      | payload bytes visible before tail advances     |
| `store(head_gen)` — drain commit    | release      | `load(head_gen)` — producer full chk| freed space visible to producer                |
| `store(pw, 1)` — producer wait      | seq_cst      | `load(pw)` — consumer drain exit    | no lost wakeup (total order)                   |
| `CAS(pw, 1→0)` — consumer wake      | seq_cst/acq  | producer's `futex_wait`             | futex_wake after CAS                           |
| `exchange(notify_latch, 1)` — push  | release      | `store(notify_latch, 0)` — proc ACK | 0→1 transition fires exactly one wake          |
| `store(ack_pos)` — proc ack         | seq_cst      | `load(ack_pos)` — worker Dekker     | ack visible before SUSPENDED check on other side|
| `fetch_or(SUSPENDED)` — worker      | seq_cst      | `load(state)` — proc notify_send    | suspension visible to processor                |
| `fetch_or(SEND_BUSY)` — processor   | seq_cst      | `fetch_and(~SEND_BUSY)` — worker    | re-flush catches data written before clear     |
| `fetch_or(rescan_mask)` — worker    | release      | `exchange(rescan_mask, 0)` — proc   | proc sees bit after worker sets it             |
| `store(tail_pos)` — OLB commit      | release      | `load(tail_pos)` — processor read   | data bytes precede tail_pos visibility         |
| `fetch_add(generation)` — slab alloc| release      | `load(generation)` — proc validate  | old slot data not visible through new gen ptr  |

**Rule of thumb used throughout the codebase:**

> A thread that *publishes* data uses `release`.
> A thread that *consumes* that data uses `acquire`.
> When both sides must agree on ordering relative to each other
> (Dekker rendezvous), both use `seq_cst`.
> Cold reads with no ordering consequence use `relaxed`.

`seq_cst` is used sparingly and only where two threads must each
observe the other's store before deciding to sleep — the suspension
and backpressure rendezvous points.  Everything else is
release/acquire or relaxed.
