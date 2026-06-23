/*
 * DAP SDK — JS library for emscripten WASM transport layer
 *
 * Contains all JS functions called from C via extern declarations.
 * Linked via emcc --js-library flag; this avoids EM_JS + LTO + archive
 * issues where __em_js__ metadata gets lost during link-time optimization.
 */

addToLibrary({

    /* ==================================================================
     * HTTP POST — async version (ST mode, main thread safe)
     * Calls C callback with result: _dap_http_async_callback(req_id, ptr, len, status)
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
        xhr.onload = function() {
            if (xhr.status >= 200 && xhr.status < 300 && xhr.response) {
                var resp = new Uint8Array(xhr.response);
                if (resp.length > 0) {
                    var ptr = _malloc(resp.length + 1);
                    HEAPU8.set(resp, ptr);
                    HEAPU8[ptr + resp.length] = 0;
                    __dap_http_async_callback(a_req_id, ptr, resp.length, 0);
                } else {
                    __dap_http_async_callback(a_req_id, 0, 0, 0);
                }
            } else {
                __dap_http_async_callback(a_req_id, 0, 0, -xhr.status || -1);
            }
        };
        xhr.onerror = function() {
            __dap_http_async_callback(a_req_id, 0, 0, -1);
        };
        if (bodySlice) xhr.send(bodySlice);
        else xhr.send();
    },

    /* ==================================================================
     * HTTP POST (synchronous XHR, runs on calling pthread's Web Worker)
     * ================================================================== */

    js_http_post_sync__deps: ['$UTF8ToString', '$setValue', 'malloc'],
    js_http_post_sync: function(a_url_ptr, a_content_type_ptr, a_body, a_body_len,
                                a_extra_headers_ptr, a_out_ptr_addr, a_out_len_addr) {
        var url = UTF8ToString(a_url_ptr);
        var contentType = a_content_type_ptr ? UTF8ToString(a_content_type_ptr) : null;
        var extraHeaders = a_extra_headers_ptr ? UTF8ToString(a_extra_headers_ptr) : null;

        var xhr = new XMLHttpRequest();
        xhr.open("POST", url, false);
        xhr.responseType = "arraybuffer";
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

        if (a_body && a_body_len > 0) {
            xhr.send(HEAPU8.slice(a_body, a_body + a_body_len));
        } else {
            xhr.send();
        }

        if (xhr.status >= 200 && xhr.status < 300) {
            var response = new Uint8Array(xhr.response);
            if (response.length > 0) {
                var ptr = _malloc(response.length + 1);
                HEAPU8.set(response, ptr);
                HEAPU8[ptr + response.length] = 0;
                setValue(a_out_ptr_addr, ptr, '*');
                setValue(a_out_len_addr, response.length, 'i32');
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
