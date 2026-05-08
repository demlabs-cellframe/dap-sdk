# Minimal Linux protocol integration (documentation shape)

This file is documentation only: it sketches how a small length-prefixed or
line-oriented protocol fits the hardened public I/O API. It is not built or
tested as part of the tree.

**Prerequisites (normal + expert public surface only):** include `dap_io.h` and
`dap_io_ops.h` for topology, `dap_io_proc_set_*`, and `dap_io_conn_open` /
`dap_io_conn_open_cfg`; `dap_proc_frame.h` for the `dap_proc_batch_cb_t` contract;
`dap_conn.h` and `dap_conn_handle.h` for connection types and handle rules;
`dap_io_send.h` for `dap_io_tx_send` / `dap_io_tx_send_direct` and liveness
documentation.
Do **not** treat `dap_io_advanced.h` as required for this walkthrough. The socket
`fd` is already non-blocking and ready for the worker `epoll` loop; application
logic sends responses explicitly (no stock echo path).

## 1. `ext` layout and OLB wiring

Put `dap_io_olb_ext_t` first, then protocol state. Use the layout guard so a
bad refactor fails at compile time:

```c
DAP_IO_SPAN_EXT_TYPE(my_proto_ext_t,
    size_t max_frame;
);
```

Call `DAP_IO_SPAN_EXT_INIT(e, my_parse, my_parse_arg, my_compact)` (or
`my_compact` NULL if unused). That macro sets `e->olb.parser` to `&e->parser` in span mode
(`dap_io_olb_parse_span` driving your `dap_io_parse_cb_t`). For a fully custom raw parse path, allocate a
`dap_io_olb_parser_t`, call `dap_io_olb_ext_setup(&e->olb, &my_parser, my_olb_parse, my_compact)`,
and implement `my_olb_parse` with the `dap_io_olb_parse_fn` contract (see
`dap_io_ops.h`).

## 2. Parse callback (worker thread)

`dap_io_parse_cb_t` has the shape
`dap_io_parse_result_t (*fn)(const char *a_data, size_t a_size, void *a_arg)`.
It runs on the owning worker after the pull filled the recv OLB. Return the
complete prefix length in `consumed`; return `bytes_needed` as a hint for a
partial tail, or `0` if unknown. Do not retain `a_data` after the callback.

**Length-prefixed example parser:**

```c
static dap_io_parse_result_t my_parse(const char *data, size_t n, void *arg) {
    (void)arg;
    const uint8_t *d = (const uint8_t *)data;
    size_t consumed = 0;
    while (n >= 4) {
        uint32_t L = ((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];
        if (n < 4u + L)
            return (dap_io_parse_result_t){ consumed, 4u + L - n };
        consumed += 4 + L;
        d += 4 + L;
        n -= 4 + L;
    }
    return (dap_io_parse_result_t){ consumed, n ? 4u - n : 0u };
}
```

## 3. Open the connection

After `fcntl` / `accept` has set non-blocking mode:

```c
dap_io_conn_cfg_t cfg = DAP_IO_CONN_CFG_INIT;
cfg.io = io;
cfg.worker_id = worker_id;
cfg.kind = DAP_IO_SOCK;
cfg.fd = fd;
cfg.olb_cap = olb_cap;
cfg.read_cb = dap_io_rx_bridge;
cfg.ext = e;
cfg.max_frame = max_frame;
cfg.ext_dtor = my_ext_free; /* NULL if no heap ext or manual dtor bind */
dap_conn_t *conn = dap_io_conn_open_cfg(&cfg);
```

Equivalent positional form: `dap_io_conn_open(io, worker_id, DAP_IO_SOCK, fd,
olb_cap, NULL, dap_io_rx_bridge, e, max_frame)`. `ext` remains caller-owned unless
`ext_dtor` is non-NULL in the cfg (success-only registration), matching
`dap_io_conn_open_with_ext_dtor`. The destructor runs on the documented
slab/quarantine path, not as a generic close hook.

## 4. Processor: frames and explicit send

Register a batch handler on the desired processor index, typically during setup:

```c
dap_io_proc_set_frame_cb(io, proc_idx, my_frame_cb, app_ctx);
```

`my_frame_cb` matches `dap_proc_batch_cb_t`: it receives a pointer into
`recv_olb` valid only for that call (see `dap_proc_frame.h`). Production code
does not get an implicit echo: build the response in your logic and send with
`dap_io_tx_send_direct(conn, buf, len)` while the callback's `dap_conn_t *` is
known-live, or with `dap_io_tx_send(handle, buf, len)` when sending later or
from code that only carries a generation-checked handle. See the public liveness
rules in `dap_io_send.h`.

Public send results (`dap_send_rc_t`):

- `DAP_SEND_OVERFLOW`: transient; send OLB has no space, or ctrl-message
  allocation failed on the slow `dap_io_tx_send` path. Retry after the worker
  drains or memory pressure eases.
- `DAP_SEND_TOO_LARGE`: permanent on the owner direct path; payload is larger
  than `send_olb` capacity, so chunk or drop. It is not returned synchronously
  from the slow path after a ctrl message is enqueued.

`DAP_SEND_OK` and `DAP_SEND_CLOSED` keep their meanings from `dap_conn.h`.

If you need return codes / defer without exposing batch internals, use
`dap_io_proc_set_frame_rc_cb` and map send results through
`dap_io_send_rc_to_msg_rc`. Use `dap_io_proc_set_batch_cb` only for custom batch
drain logic that really needs `dap_batch_task_t`.

## 5. What this example intentionally omits

No CMake target and no sample `main`. Error handling, backpressure, and protocol
specifics are the integrator's responsibility; this document only fixes the
shape against the public API surface.
