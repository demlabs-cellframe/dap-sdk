/*
 * DAP SDK — JS library for emscripten WASM transport layer
 *
 * Contains all JS functions called from C via extern declarations.
 * Linked via emcc --js-library flag; this avoids EM_JS + LTO + archive
 * issues where __em_js__ metadata gets lost during link-time optimization.
 */

addToLibrary({

    /* ==================================================================
     * Helper: detect Node.js vs browser
     * ================================================================== */
    _dap_is_node: function() {
        return typeof process !== 'undefined' && process.versions && process.versions.node;
    },

    /* ==================================================================
     * HTTP POST — async version (ST mode, main thread safe)
     * Calls C callback with result: _dap_http_async_callback(req_id, ptr, len, status)
     *
     * In Node.js: uses synchronous http (blocks main thread but C code
     *             is already blocking via pthread_cond_timedwait, so
     *             we need synchronous response to unblock it).
     * In browser: uses async XHR (callback fires from event loop).
     * ================================================================== */

    js_http_post_async__deps: ['$UTF8ToString', 'malloc', '_dap_http_async_callback'],
    js_http_post_async: function(a_req_id, a_url_ptr, a_content_type_ptr, a_body, a_body_len,
                                  a_extra_headers_ptr) {
        var url = UTF8ToString(a_url_ptr);
        var contentType = a_content_type_ptr ? UTF8ToString(a_content_type_ptr) : null;
        var extraHeaders = a_extra_headers_ptr ? UTF8ToString(a_extra_headers_ptr) : null;
        var bodySlice = (a_body && a_body_len > 0)
            ? HEAPU8.slice(a_body, a_body + a_body_len)
            : null;

        // Node.js path: synchronous HTTP via child_process (no event loop blocking)
        if (typeof require === 'function' && typeof process !== 'undefined' && process.versions && process.versions.node) {
            try {
                var parsed = new URL(url);
                var httpModule = parsed.protocol === 'https:' ? require('https') : require('http');

                // Use synchronous request via deasync-style approach
                // We need to make this truly synchronous without blocking event loop
                var done = false;
                var respData = null;
                var respStatus = -1;

                var options = {
                    hostname: parsed.hostname,
                    port: parsed.port || (parsed.protocol === 'https:' ? 443 : 80),
                    path: parsed.pathname + parsed.search,
                    method: 'POST',
                    headers: {
                        'Content-Type': contentType || 'application/json',
                        'Content-Length': bodySlice ? bodySlice.length : 0,
                    },
                };

                var req = httpModule.request(options, function(res) {
                    var chunks = [];
                    res.on('data', function(chunk) { chunks.push(chunk); });
                    res.on('end', function() {
                        respData = Buffer.concat(chunks);
                        respStatus = (res.statusCode >= 200 && res.statusCode < 300) ? 0 : (-res.statusCode || -1);
                        done = true;
                    });
                });
                req.on('error', function(e) {
                    respStatus = -1;
                    done = true;
                });
                req.setTimeout(15000, function() {
                    req.destroy();
                    respStatus = -1;
                    done = true;
                });
                if (bodySlice) req.write(Buffer.from(bodySlice));
                req.end();

                // Synchronous wait using Atomics (SharedArrayBuffer approach)
                // Create a shared buffer for synchronization
                var sab = new SharedArrayBuffer(4);
                var i32 = new Int32Array(sab);

                // Set up a timer to check done flag and wake
                var interval = setInterval(function() {
                    if (done) {
                        Atomics.store(i32, 0, 1);
                        Atomics.notify(i32, 0);
                    }
                }, 5);

                // Wait for response (with timeout)
                Atomics.wait(i32, 0, 0, 16000);
                clearInterval(interval);

                if (respStatus === 0 && respData && respData.length > 0) {
                    var ptr = _malloc(respData.length + 1);
                    HEAPU8.set(respData, ptr);
                    HEAPU8[ptr + respData.length] = 0;
                    __dap_http_async_callback(a_req_id, ptr, respData.length, 0);
                } else {
                    __dap_http_async_callback(a_req_id, 0, 0, respStatus || -1);
                }
            } catch (e) {
                __dap_http_async_callback(a_req_id, 0, 0, -1);
            }
            return;
        }

        // Browser path: async XMLHttpRequest.
        //
        // Bounded and negatively cached for the same reason the synchronous one
        // is (see js_http_post_sync below): a node that accepts the connection
        // and never answers otherwise leaves the callback pending until the
        // browser's own network timeout. Measured through the wallet in Chrome
        // against rpc.cellframe.net: 129 717 ms for one `net_list`, with the
        // single-threaded engine suspended on it and every later call — crypto
        // included — queued behind it.
        var DAP_NODE_TIMEOUT_RC = -1002;   /* keep in sync with cf_client_rpc_transport.h */
        var cfg = Module.__dapNodeTransport || (Module.__dapNodeTransport = {});
        if (!cfg.asyncTimeoutMs || cfg.asyncTimeoutMs < 0) cfg.asyncTimeoutMs = 15000;
        if (!cfg.downTtlMs || cfg.downTtlMs < 0) cfg.downTtlMs = 10000;
        if (!cfg.timeoutTtlMs || cfg.timeoutTtlMs < 0) cfg.timeoutTtlMs = 5000;
        var originOfUrl = function(aUrl) {
            try { return new URL(aUrl).origin; } catch (e) { return aUrl; }
        };
        var origin = originOfUrl(url);

        var settled = false;
        var finish = function(a_ptr, a_len, a_status) {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            __dap_http_async_callback(a_req_id, a_ptr, a_len, a_status);
        };

        // An origin that just refused a request is skipped rather than paid for
        // again; the TTL expiring re-arms it, so recovery needs no user action.
        if (Module.__dapNodeDown && Module.__dapNodeDown.origin === origin) {
            if (Date.now() >= Module.__dapNodeDown.until) {
                Module.__dapNodeDown = null;
            } else {
                // Replay the remembered condition (see the synchronous path).
                __dap_http_async_callback(a_req_id, 0, 0, Module.__dapNodeDown.rc || -1);
                return;
            }
        }

        var xhr = new XMLHttpRequest();
        xhr.open("POST", url, true);
        xhr.responseType = "arraybuffer";
        if (contentType) xhr.setRequestHeader("Content-Type", contentType);
        if (extraHeaders) {
            var lines = extraHeaders.split("\r\n");
            for (var i = 0; i < lines.length; i++) {
                var sep = lines[i].indexOf(":");
                if (sep > 0)
                    xhr.setRequestHeader(lines[i].substring(0, sep).trim(),
                                         lines[i].substring(sep + 1).trim());
            }
        }
        var timer = setTimeout(function() {
            try { xhr.abort(); } catch (e) { /* already finished */ }
            Module.__dapNodeDown = {
                origin: origin,
                until: Date.now() + cfg.timeoutTtlMs,
                rc: DAP_NODE_TIMEOUT_RC,
            };
            finish(0, 0, DAP_NODE_TIMEOUT_RC);
        }, cfg.asyncTimeoutMs);
        xhr.onload = function() {
            // Any HTTP answer — even an error status — proves the node is up.
            Module.__dapNodeDown = null;
            if (xhr.status >= 200 && xhr.status < 300 && xhr.response) {
                var resp = new Uint8Array(xhr.response);
                if (resp.length > 0) {
                    var ptr = _malloc(resp.length + 1);
                    HEAPU8.set(resp, ptr);
                    HEAPU8[ptr + resp.length] = 0;
                    finish(ptr, resp.length, 0);
                } else {
                    finish(0, 0, 0);
                }
            } else {
                finish(0, 0, -xhr.status || -1);
            }
        };
        xhr.onerror = function() {
            Module.__dapNodeDown = { origin: origin, until: Date.now() + cfg.downTtlMs };
            finish(0, 0, -1);
        };
        if (bodySlice) xhr.send(bodySlice);
        else xhr.send();
    },

    /* ==================================================================
     * HTTP POST (synchronous XHR, runs on calling pthread's Web Worker)
     *
     * The browser path is BOUNDED and NEGATIVELY CACHED.
     *
     * Why: C calls this from the thread that runs the engine, and in the
     * single-threaded (Firefox) build that is the *only* thread. An
     * unbounded synchronous XHR to an unreachable node blocks it forever —
     * no JS timer can fire while the thread is blocked, so a timer-based
     * timeout can never fire, and the thread stays wedged afterwards.
     *
     * The bound is `xhr.timeout` on the synchronous request. The XHR spec
     * only forbids `timeout` for a *Window* global (hence the try/catch
     * below); this code runs inside a Worker, where the browser does abort
     * the request when the timer fires. Measured (worker scope, sync XHR):
     *   Firefox 140.15.0esr — connect phase holding (SYN dropped, the
     *     rpc.cellframe.net failure mode) 2001 ms; DNS failure 2002 ms;
     *     connected-but-silent 2001 ms — all TimeoutError from send().
     *   Chromium (headless) — same three cases: 3004 ms / 2 ms / 3003 ms.
     * No timer of ours is needed: the browser aborts, and the blocking
     * `send()` throws instead of never returning.
     *
     * Negative cache: a connection-level failure (timeout, network error)
     * marks the origin unreachable for `downTtlMs`, so the retry storm every
     * caller does against a dead node fails immediately instead of paying
     * the timeout again. The TTL expiring is what makes recovery automatic;
     * any HTTP answer (even an error status) clears the mark at once.
     *
     * Rejected alternatives (measured/derived, not guessed):
     *  - Asyncify (the wasm is already instrumented and `ccall(..., {async:
     *    true})` already used for the rpc path): suspending inside this
     *    import only works when the *caller* passed `async: true`. Many
     *    exports that reach the network (cf_token_info, cf_tx_find,
     *    cf_mempool_add, cf_compose_*, cf_srv_dex_*, cf_shared_*) are called
     *    synchronously, and emscripten's ccall aborts the runtime for those
     *    ("The call to X is running asynchronously... add the async option").
     *  - A separate async pre-flight probe: it can fail for reasons that are
     *    not reachability (a Firefox MV3 host-permission denial turns every
     *    manifest origin into an opt-in, so a probe is blocked although the
     *    engine's own request is not). A cache a probe like that can poison
     *    would make the transport *skip* a request that would have worked.
     *    A failure observed by the request itself cannot have that problem.
     * ================================================================== */

    js_http_post_sync__deps: ['$UTF8ToString', '$setValue', 'malloc'],
    js_http_post_sync: function(a_url_ptr, a_content_type_ptr, a_body, a_body_len,
                                a_extra_headers_ptr, a_out_ptr_addr, a_out_len_addr,
                                a_timeout_ms) {
        /* Keep in sync with cf_client/include/cf_client_rpc_transport.h. */
        var DAP_NODE_UNREACHABLE_RC = -1001;
        var DAP_NODE_TIMEOUT_RC = -1002;
        var originOf = function(a_url) {
            try { return new URL(a_url).origin; } catch (e) { return a_url; }
        };
        var url = UTF8ToString(a_url_ptr);
        var contentType = a_content_type_ptr ? UTF8ToString(a_content_type_ptr) : null;
        var extraHeaders = a_extra_headers_ptr ? UTF8ToString(a_extra_headers_ptr) : null;

        // Node.js path: synchronous HTTP via execSync subprocess
        // Note: Emscripten pthread workers define process.versions.node,
        // so we also check typeof require === 'function' to detect actual Node.js.
        if (typeof require === 'function' && typeof process !== 'undefined' && process.versions && process.versions.node) {
            var parsed = new URL(url);
            var execSync = require('child_process').execSync;
            var b64 = (a_body && a_body_len > 0)
                ? Buffer.from(HEAPU8.slice(a_body, a_body + a_body_len)).toString('base64')
                : '';
            var proto = parsed.protocol === 'https:' ? 'https' : 'http';
            var port = parsed.port || (proto === 'https' ? 443 : 80);
            var script = [
                'const h=require(' + JSON.stringify(proto) + ');',
                'const b=Buffer.from(' + JSON.stringify(b64) + ',"base64");',
                'const r=h.request({hostname:' + JSON.stringify(parsed.hostname) + ',port:' + port + ',path:"/",method:"POST",',
                'headers:{"Content-Type":' + JSON.stringify(contentType || 'application/json') + ',"Content-Length":b.length}},',
                'function(res){const d=[];res.on("data",c=>d.push(c));',
                'res.on("end",()=>{process.stdout.write(Buffer.concat(d).toString("base64"));process.exit(0)});});',
                'r.on("error",()=>process.exit(1));r.setTimeout(15000,()=>{r.destroy();process.exit(1)});',
                'if(b.length>0)r.write(b);r.end();',
            ].join('');
            var responseData = null;
            var result = -1;
            try {
                var result_b64 = execSync('node -e ' + JSON.stringify(script), {
                    encoding: 'utf-8',
                    timeout: 20000,
                });
                if (result_b64) {
                    responseData = Buffer.from(result_b64, 'base64');
                    result = 0;
                }
            } catch (e) {
                result = -1;
            }
            if (result === 0 && responseData && responseData.length > 0) {
                var ptr = _malloc(responseData.length + 1);
                HEAPU8.set(responseData, ptr);
                HEAPU8[ptr + responseData.length] = 0;
                setValue(a_out_ptr_addr, ptr, '*');
                setValue(a_out_len_addr, responseData.length, 'i32');
            } else {
                setValue(a_out_ptr_addr, 0, '*');
                setValue(a_out_len_addr, 0, 'i32');
            }
            return result;
        }

        // Browser path: synchronous XHR, bounded (see the header comment).
        var cfg = Module.__dapNodeTransport || (Module.__dapNodeTransport = {});
        if (!cfg.timeoutMs || cfg.timeoutMs < 0) cfg.timeoutMs = 10000;
        if (!cfg.downTtlMs || cfg.downTtlMs < 0) cfg.downTtlMs = 10000;
        // A spent budget is remembered far more briefly than a refusal: the node
        // may be slow rather than absent, and the next call is allowed to find
        // out. Short is enough to stop a *burst* of queued calls (the wallet
        // issues several at startup) from each paying the whole budget, which on
        // the single-threaded engine starves everything behind them.
        if (!cfg.timeoutTtlMs || cfg.timeoutTtlMs < 0) cfg.timeoutTtlMs = 5000;
        // The CALLER's budget wins when it has one: the engine's own commands
        // legitimately take longer than any fixed default (measured: 20 s for
        // wallet;outputs on a cold node), and a bound shorter than the caller's
        // turns "slow but alive" into a bogus "unreachable".
        var callTimeoutMs = (typeof a_timeout_ms === 'number' && a_timeout_ms > 0)
            ? a_timeout_ms
            : cfg.timeoutMs;
        var origin = originOf(url);

        // Fast fail: this origin refused a request a moment ago. Returning here
        // costs nothing and keeps a dead node from occupying the engine thread
        // once per call; the TTL expiring re-arms the real request. The code the
        // entry was created with is replayed, so a remembered timeout is still
        // reported as a timeout and a remembered refusal as a refusal.
        if (Module.__dapNodeDown && Module.__dapNodeDown.origin === origin) {
            if (Date.now() >= Module.__dapNodeDown.until) {
                Module.__dapNodeDown = null;
            } else {
                setValue(a_out_ptr_addr, 0, '*');
                setValue(a_out_len_addr, 0, 'i32');
                return Module.__dapNodeDown.rc || DAP_NODE_UNREACHABLE_RC;
            }
        }

        var xhr = new XMLHttpRequest();
        xhr.open("POST", url, false);
        // Request a binary response. Sync XHR with responseType is supported on
        // Web Workers (where this MT path runs). Guard with try/catch for the
        // legacy main-thread restriction, falling back to binary string decode.
        var binaryAsString = false;
        try {
            xhr.responseType = "arraybuffer";
        } catch (e) {
            binaryAsString = true;
            xhr.overrideMimeType("text/plain; charset=x-user-defined");
        }
        // Abort the *synchronous* request once the budget is spent. Throws on a
        // Window global (spec restriction); there no bound is available, so keep
        // the historical unbounded behaviour rather than fail the call.
        try { xhr.timeout = callTimeoutMs; } catch (e) { /* Window scope */ }
        if (contentType) xhr.setRequestHeader("Content-Type", contentType);

        if (extraHeaders) {
            var lines = extraHeaders.split("\r\n");
            for (var i = 0; i < lines.length; i++) {
                var sep = lines[i].indexOf(":");
                if (sep > 0) {
                    xhr.setRequestHeader(lines[i].substring(0, sep).trim(),
                                         lines[i].substring(sep + 1).trim());
                }
            }
        }

        var sendError = null;
        try {
            if (a_body && a_body_len > 0) {
                xhr.send(HEAPU8.slice(a_body, a_body + a_body_len));
            } else {
                xhr.send();
            }
        } catch (e) {
            // A sync XHR reports timeout/network failure by throwing from
            // send() instead of firing onerror (measured in both browsers).
            sendError = e;
        }

        if (sendError || xhr.status === 0) {
            setValue(a_out_ptr_addr, 0, '*');
            setValue(a_out_len_addr, 0, 'i32');
            // A request that ran out of its budget is NOT proof the node is gone
            // — it may simply be slow (measured: wallet;outputs 20 s on a cold
            // node while the node answered other commands in <1 s). Reporting
            // that as "unreachable" made the wallet claim a live node was down.
            var l_timedOut = !!sendError && (sendError.name === 'TimeoutError' ||
                                             /timeout/i.test(String(sendError.message || '')));
            if (l_timedOut) {
                // Remembered as a TIMEOUT for a short TTL, not as a refusal: the
                // code is replayed by the fast-fail path above, so the wallet
                // still says "did not answer in time" and never claims the node
                // is down. Without this, a burst of queued calls each paid the
                // full budget and pure crypto behind them did not run at all
                // (bench pureCryptoWorksWhileNodeUnreachable, single-threaded
                // engine).
                Module.__dapNodeDown = {
                    origin: origin,
                    until: Date.now() + cfg.timeoutTtlMs,
                    rc: DAP_NODE_TIMEOUT_RC,
                };
                return DAP_NODE_TIMEOUT_RC;
            }
            Module.__dapNodeDown = { origin: origin, until: Date.now() + cfg.downTtlMs };
            return DAP_NODE_UNREACHABLE_RC;
        }

        // The node answered (any HTTP status) — it is up, clear the mark.
        Module.__dapNodeDown = null;

        if (xhr.status >= 200 && xhr.status < 300) {
            var responseBytes = null;
            if (binaryAsString) {
                // x-user-defined: each char is a raw byte 0x00-0xFF
                var text = xhr.responseText || "";
                responseBytes = new Uint8Array(text.length);
                for (var j = 0; j < text.length; j++)
                    responseBytes[j] = text.charCodeAt(j) & 0xff;
            } else if (xhr.response && xhr.response.byteLength > 0) {
                responseBytes = new Uint8Array(xhr.response);
            }
            if (responseBytes && responseBytes.length > 0) {
                var ptr = _malloc(responseBytes.length + 1);
                HEAPU8.set(responseBytes, ptr);
                HEAPU8[ptr + responseBytes.length] = 0;
                setValue(a_out_ptr_addr, ptr, '*');
                setValue(a_out_len_addr, responseBytes.length, 'i32');
            } else {
                setValue(a_out_ptr_addr, 0, '*');
                setValue(a_out_len_addr, 0, 'i32');
            }
            return 0;
        }
        return -xhr.status || -1;
    },

    /* ==================================================================
     * WebRTC
     * ================================================================== */

    js_rtc_create_peer__deps: ['$UTF8ToString'],
    js_rtc_create_peer: function(a_stun_ptr) {
        var stun = a_stun_ptr ? UTF8ToString(a_stun_ptr) : "stun:stun.l.google.com:19302";
        if (!Module._rtc_pool) {
            Module._rtc_pool = {};
            Module._rtc_next_id = 1;
        }
        var id = Module._rtc_next_id++;
        var config = { iceServers: [{ urls: stun }] };

        var pc;
        try { pc = new RTCPeerConnection(config); }
        catch (e) { return -1; }

        var entry = { pc: pc, dc: null, state: 0, ice_candidates: [], ice_done: false };
        Module._rtc_pool[id] = entry;

        pc.onicecandidate = function(ev) {
            if (ev.candidate) {
                entry.ice_candidates.push(JSON.stringify(ev.candidate));
            } else {
                entry.ice_done = true;
            }
        };

        pc.onconnectionstatechange = function() {
            if (pc.connectionState === "connected") {
                entry.state = 2;
                if (Module.__rtc_on_connected) Module.__rtc_on_connected(id);
            } else if (pc.connectionState === "failed" || pc.connectionState === "closed") {
                entry.state = 5;
                if (Module.__rtc_on_closed) Module.__rtc_on_closed(id);
            }
        };

        return id;
    },

    js_rtc_create_dc__deps: ['$UTF8ToString', 'malloc', 'free'],
    js_rtc_create_dc: function(a_peer_id, a_label_ptr) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) return -1;
        var label = a_label_ptr ? UTF8ToString(a_label_ptr) : "dap-stream";
        var dc;
        try { dc = entry.pc.createDataChannel(label, { ordered: true }); }
        catch (e) { return -1; }
        dc.binaryType = "arraybuffer";
        entry.dc = dc;

        dc.onopen = function() {
            entry.state = 2;
            if (Module.__rtc_on_dc_open) Module.__rtc_on_dc_open(a_peer_id);
        };
        dc.onclose = function() {
            if (Module.__rtc_on_dc_close) Module.__rtc_on_dc_close(a_peer_id);
        };
        dc.onmessage = function(ev) {
            var arr = new Uint8Array(ev.data);
            var buf = _malloc(arr.length);
            HEAPU8.set(arr, buf);
            if (Module.__rtc_on_dc_message) Module.__rtc_on_dc_message(a_peer_id, buf, arr.length);
            _free(buf);
        };
        return 0;
    },

    js_rtc_create_offer__deps: ['$setValue', '$lengthBytesUTF8', '$stringToUTF8', 'malloc'],
    js_rtc_create_offer: function(a_peer_id, a_out_ptr) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) return -1;
        var pc = entry.pc;

        var done = false;
        var result = -1;
        pc.createOffer().then(function(offer) {
            return pc.setLocalDescription(offer);
        }).then(function() {
            var sdp = pc.localDescription.sdp;
            var len = lengthBytesUTF8(sdp) + 1;
            var ptr = _malloc(len);
            stringToUTF8(sdp, ptr, len);
            setValue(a_out_ptr, ptr, '*');
            result = 0;
            done = true;
        }).catch(function(e) {
            result = -1;
            done = true;
        });

        while (!done) {}
        return result;
    },

    js_rtc_set_remote_answer__deps: ['$UTF8ToString'],
    js_rtc_set_remote_answer: function(a_peer_id, a_sdp_ptr) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) return -1;
        var sdp = UTF8ToString(a_sdp_ptr);

        var done = false;
        var result = -1;
        entry.pc.setRemoteDescription({ type: "answer", sdp: sdp }).then(function() {
            result = 0;
            done = true;
        }).catch(function(e) {
            result = -1;
            done = true;
        });

        while (!done) {}
        return result;
    },

    /* ==================================================================
     * RTC async variants (ST mode — no busy-wait, callback into C)
     * ================================================================== */

    js_rtc_create_offer_async__deps: ['$lengthBytesUTF8', '$stringToUTF8', 'malloc', '_rtc_offer_async_callback'],
    js_rtc_create_offer_async: function(a_peer_id) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) { __rtc_offer_async_callback(a_peer_id, 0, -1); return; }
        var pc = entry.pc;
        pc.createOffer().then(function(offer) {
            return pc.setLocalDescription(offer);
        }).then(function() {
            var sdp = pc.localDescription.sdp;
            var len = lengthBytesUTF8(sdp) + 1;
            var ptr = _malloc(len);
            stringToUTF8(sdp, ptr, len);
            __rtc_offer_async_callback(a_peer_id, ptr, 0);
        }).catch(function(e) {
            __rtc_offer_async_callback(a_peer_id, 0, -1);
        });
    },

    js_rtc_set_answer_async__deps: ['$UTF8ToString', '_rtc_answer_async_callback'],
    js_rtc_set_answer_async: function(a_peer_id, a_sdp_ptr) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) { __rtc_answer_async_callback(a_peer_id, -1); return; }
        var sdp = UTF8ToString(a_sdp_ptr);
        entry.pc.setRemoteDescription({ type: "answer", sdp: sdp }).then(function() {
            __rtc_answer_async_callback(a_peer_id, 0);
        }).catch(function() {
            __rtc_answer_async_callback(a_peer_id, -1);
        });
    },

    js_rtc_add_ice__deps: ['$UTF8ToString'],
    js_rtc_add_ice: function(a_peer_id, a_candidate_ptr) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) return -1;
        var cand = JSON.parse(UTF8ToString(a_candidate_ptr));

        var done = false;
        var result = -1;
        entry.pc.addIceCandidate(cand).then(function() {
            result = 0;
            done = true;
        }).catch(function(e) {
            result = -1;
            done = true;
        });

        while (!done) {}
        return result;
    },

    js_rtc_get_ice_candidates__deps: ['$setValue', '$lengthBytesUTF8', '$stringToUTF8', 'malloc'],
    js_rtc_get_ice_candidates: function(a_peer_id, a_out_ptr) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) return -1;
        var json = "[" + entry.ice_candidates.join(",") + "]";
        var len = lengthBytesUTF8(json) + 1;
        var ptr = _malloc(len);
        stringToUTF8(json, ptr, len);
        setValue(a_out_ptr, ptr, '*');
        return entry.ice_candidates.length;
    },

    js_rtc_dc_send: function(a_peer_id, a_data, a_len) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry || !entry.dc || entry.dc.readyState !== "open") return -1;
        try {
            entry.dc.send(HEAPU8.slice(a_data, a_data + a_len).buffer);
            return a_len;
        } catch (e) { return -1; }
    },

    js_rtc_close: function(a_peer_id) {
        var entry = Module._rtc_pool ? Module._rtc_pool[a_peer_id] : null;
        if (!entry) return;
        if (entry.dc) try { entry.dc.close(); } catch(e) {}
        try { entry.pc.close(); } catch(e) {}
        delete Module._rtc_pool[a_peer_id];
    },

    js_rtc_init_callbacks: function() {
        var e = wasmExports || Module.asm;
        Module.__rtc_on_connected  = e['_rtc_on_connected']  || Module['__rtc_on_connected'];
        Module.__rtc_on_closed     = e['_rtc_on_closed']     || Module['__rtc_on_closed'];
        Module.__rtc_on_dc_open    = e['_rtc_on_dc_open']    || Module['__rtc_on_dc_open'];
        Module.__rtc_on_dc_close   = e['_rtc_on_dc_close']   || Module['__rtc_on_dc_close'];
        Module.__rtc_on_dc_message = e['_rtc_on_dc_message'] || Module['__rtc_on_dc_message'];
    },

    /* ==================================================================
     * WebSocket (native browser API)
     * ================================================================== */

    js_ws_create__deps: ['$UTF8ToString', 'malloc', 'free'],
    js_ws_create: function(a_url_ptr) {
        var url = UTF8ToString(a_url_ptr);
        if (!Module._ws_pool) {
            Module._ws_pool = {};
            Module._ws_next_id = 1;
        }
        var id = Module._ws_next_id++;
        var ws;
        try { ws = new WebSocket(url, "dap-stream"); }
        catch (e) { return -1; }
        ws.binaryType = "arraybuffer";
        var entry = { ws: ws, state: 0 };
        Module._ws_pool[id] = entry;

        ws.onopen = function() {
            var e = Module._ws_pool[id];
            if (e) e.state = 1;
            if (Module.__ws_on_open) Module.__ws_on_open(id);
        };
        ws.onclose = function(ev) {
            var e = Module._ws_pool[id];
            if (e) e.state = 3;
            if (Module.__ws_on_close) Module.__ws_on_close(id, ev.code);
        };
        ws.onerror = function() {
            if (Module.__ws_on_error) Module.__ws_on_error(id);
        };
        ws.onmessage = function(ev) {
            var data = ev.data;
            var arr = (typeof data === "string")
                ? new TextEncoder().encode(data)
                : new Uint8Array(data);
            var buf = _malloc(arr.length);
            HEAPU8.set(arr, buf);
            if (Module.__ws_on_message) Module.__ws_on_message(id, buf, arr.length);
            _free(buf);
        };
        return id;
    },

    js_ws_send: function(a_handle, a_data, a_len) {
        var entry = Module._ws_pool ? Module._ws_pool[a_handle] : null;
        if (!entry || entry.state !== 1) return -1;
        try {
            entry.ws.send(HEAPU8.slice(a_data, a_data + a_len).buffer);
            return a_len;
        } catch (e) { return -1; }
    },

    js_ws_close: function(a_handle, a_code) {
        var entry = Module._ws_pool ? Module._ws_pool[a_handle] : null;
        if (entry) {
            try { entry.ws.close(a_code); } catch(e) {}
            entry.state = 2;
        }
    },

    js_ws_destroy: function(a_handle) {
        if (!Module._ws_pool) return;
        var entry = Module._ws_pool[a_handle];
        if (entry) {
            try { entry.ws.close(); } catch(e) {}
            delete Module._ws_pool[a_handle];
        }
    },

    js_ws_init_callbacks: function() {
        var e = wasmExports || Module.asm;
        Module.__ws_on_open    = e['_ws_on_open']    || Module['__ws_on_open'];
        Module.__ws_on_close   = e['_ws_on_close']   || Module['__ws_on_close'];
        Module.__ws_on_error   = e['_ws_on_error']   || Module['__ws_on_error'];
        Module.__ws_on_message = e['_ws_on_message'] || Module['__ws_on_message'];
    },
});
