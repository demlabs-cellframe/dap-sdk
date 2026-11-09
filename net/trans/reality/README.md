# REALITY transport (VLESS + Reality, pure C)

Wire-compatible client for Xray-core **v26.10.31** (tag pinned, commit
`a4d3dff5b20c2faae567b742c172c7b95e75c083`). No Go, no new runtime deps:
crypto via OpenSSL/BoringSSL EVP, ML-KEM-768 via vendored PQClean (CC0).

Status: **core proven against live server** (handshake 92 ms, HTTP through
tunnel, 16 KB+ multi-record transfer with vision padding→raw transition).
Interop test: `tests/test_interop.c` vs 95.179.231.80:443.

## Layout (SOLID isolation)

```
include/reality/reality.h   public API + DI ports (io/crypto/time/log)
core/                       protocol core — ZERO dap-sdk includes
  reality_fp_chrome.c       wire-captured ClientHello template (generated,
                            tools/gen_fp.py; source: real xray client capture)
  reality_fp_build.c        template patcher (random/session_id/keyshares/SNI)
  reality_auth.c            session_id seal + server cert evidence (HMAC-SHA512)
  reality_tls13.c           TLS 1.3 mini-stack (handshake, key schedule, records)
  vless_codec.c             VLESS request header
  vless_vision.c            xtls-rprx-vision padding codec
  rate_limiter.c            token bucket (DPI: cap 5-6 MB/s on cellular)
  reality_session.c         public session API
port/                       injected dependencies
  reality_crypto_openssl.c  EVP: X25519/HKDF/AEAD/HMAC/SHA/Ed25519
  mlkem/                    vendored PQClean ML-KEM-768 + randombytes shim
  reality_io_socket.c       blocking TCP (tests/CLI; production uses SDK loop)
  reality_time_std.c
  reality_bridge.c/.h       thread+socketpair bridge: blocking session exposed
                            as a byte pipe with ctrl events ('R'/'E'/'C') —
                            интеграция с событийными циклами без переделки ядра;
                            есть ext-fd режим (насос прямо в данный fd)
  reality_forwarder.c/.h    локальный TCP-форвардер 127.0.0.1:<port> → туннель:
                            каждое соединение = отдельный мост. v0-интеграция
                            в продукт БЕЗ изменений SDK: uplink клиента
                            направляется на порт форвардера, существующая
                            HTTP-DAP машина едет внутри туннеля как есть
adapter/                    (next step) dap_net_trans_reality.c — native
                            transport registration (тип 0x0A) для policy/fallback
tests/                      standalone: unit (RFC vectors, loopback), interop,
                            bridge, forwarder — все против live-ноды
```

## Build (standalone, without the SDK)

```
cmake -S . -B /tmp/reality-build -DOPENSSL_ROOT=<prefix>
cmake --build /tmp/reality-build
/tmp/reality-build/test_reality_unit          # offline: vectors, codecs, loopback
```

Live tests (env-configured; see CMakeLists for the full variable list):

```
REALITY_NODE=... REALITY_PORT=443 REALITY_SNI=www.samsung.com \
REALITY_PUBKEY=<b64url> REALITY_SHORTID=<hex> REALITY_UUID=<uuid> \
[REALITY_IFACE=en0] [REALITY_DEST_HOST=... REALITY_DEST_PORT=...] \
  ./test_reality_interop     # one session: handshake + HTTP GET
  ./test_reality_bridge      # thread+socketpair pipe, same session
  ./test_reality_forwarder   # local TCP listener, 3 sequential sessions
```

Debug-сборка с wire-дампами: `-DCMAKE_C_FLAGS=-DREALITY_DEBUG`.

## Protocol notes (learned from the wire, not from docs)

- Server v26.10.31 **requires** X25519MLKEM768 (0x11EC) key share; plain
  X25519-only hellos are silently fallen back to the real target.
- Auth: `session_id[0:16] = {26,10,31,0,unix32,shortId}` sealed with
  AES-256-GCM, key = HKDF-SHA256(X25519(plain-share-priv, server_static_pub),
  salt=hello.random[:20], info="REALITY"), nonce=random[20:32],
  AAD = ClientHello with zeroed session_id. Auth uses the **plain** X25519
  share; the session ECDH uses the **hybrid** share (two different keypairs).
- Server evidence: its ephemeral ed25519 cert carries
  `HMAC-SHA512(authKey, ed25519_pub)` in the signature field; absence ⇒
  real certificate ⇒ MITM/redirect.
- Server Finished key derives from **s_hs_traffic** (not the raw handshake
  secret); `Derive-Secret(., "derived", "")` context is **Hash("")**, not empty.
- Server flushes the VLESS response header only after first client body bytes
  reach the target (`SetFlushNext`) ⇒ response header must be read lazily,
  never blocking before the first write.
- Server sends **empty app-data records** post-handshake (mimicry of the
  fallback target's record lengths) ⇒ an empty record is NOT EOF.
- VLESS request/response headers travel **raw**; only the body is
  vision-framed (first frame UUID-prefixed, padded ~8 packets, then raw).

## Regenerating the ClientHello template

Capture from a working xray client (tcpdump on the node), extract the
ClientHello handshake message, then:

```
python3 tools/gen_fp.py clienthello.bin > core/reality_fp_chrome.c
```

## Known limitations (deliberate)

- One handshake shape: no HelloRetryRequest (hard error), no resumption,
  no 0-RTT; KeyUpdate supported; NewSessionTicket ignored.
- Client never emits vision DIRECT (0x02); handles it from the server.
- Ciphers: 1301/1302/1303. Server observed picking 0x1302.
