// Embedded Xray stream bridge. No local SOCKS/HTTP listener and no child process.
// Reads/writes may block: callers must use dedicated I/O workers, not the reactor.
package main

/*
#include <stdint.h>
#include <stddef.h>
*/
import "C"

import (
    "context"
    "unsafe"
    "time"

    "github.com/xtls/xray-core/core"
    xnet "github.com/xtls/xray-core/common/net"
)

// CFRealityOpen starts a caller-supplied Xray configuration and opens an opaque
// TCP stream to the DAP endpoint. A handle is not proof of remote DAP readiness.
// The configuration must contain no inbounds and a VLESS/gRPC/REALITY outbound.
//export CFRealityOpen
func CFRealityOpen(config *C.char, size C.size_t, destination *C.char) C.uint64_t {
    if config == nil || destination == nil || size == 0 || size > 65536 { return 0 }
    raw := C.GoBytes(unsafe.Pointer(config), C.int(size))
    if !validConfig(raw) { return 0 }
    dest, err := xnet.ParseDestination(C.GoString(destination))
    if err != nil || dest.Network != xnet.Network_TCP { return 0 }
    ctx, cancel := context.WithCancel(context.Background())
    c := &connection{cancel: cancel}
    id := registry.add(c)
    if id == 0 { cancel(); return 0 }
    c.armOpenDeadline(30 * time.Second)
    c.armHandshakeDeadline(60 * time.Second)
    go func() {
        if err := ctx.Err(); err != nil { c.finish(nil,nil,err); return }
        instance, err := core.StartInstance("json", raw)
        if err != nil { c.finish(nil,nil,err); return }
        if err := ctx.Err(); err != nil { c.finish(instance,nil,err); return }
        stream, err := core.Dial(ctx, instance, dest)
        c.finish(instance,stream,err)
    }()
    return C.uint64_t(id)
}

// CFRealityStatus: 0 opening, 1 local stream available, -1 failed, -2 unknown/closed.
// A local stream is not proof of a successful remote REALITY or DAP handshake.
//export CFRealityStatus
func CFRealityStatus(id C.uint64_t) C.int {
    c := registry.get(uint64(id))
    if c == nil { return -2 }
    _, state := c.snapshot()
    return C.int(state)
}

// CFRealityError returns sanitized setup diagnostics, never credentials.
// 0 none, 1 backend/open failure, 2 opening deadline, 3 DAP handshake deadline,
// -2 unknown/closed handle.
//export CFRealityError
func CFRealityError(id C.uint64_t) C.int {
    c := registry.get(uint64(id))
    if c == nil { return -2 }
    return C.int(c.errorCode())
}

// Only the SDK protocol layer may confirm after validating the remote DAP reply.
//export CFRealityConfirmHandshake
func CFRealityConfirmHandshake(id C.uint64_t) C.int {
    c := registry.get(uint64(id))
    if c == nil || !c.confirmHandshake() { return -1 }
    return 0
}

//export CFRealityRead
func CFRealityRead(id C.uint64_t, data unsafe.Pointer, size C.size_t) C.int {
    c := registry.get(uint64(id))
    if c == nil || data == nil || size == 0 || size > 65536 { return -1 }
    stream, state := c.snapshot()
    if state != 1 || stream == nil { return -1 }
    n, err := stream.Read(unsafe.Slice((*byte)(data), int(size)))
    if n > 0 { return C.int(n) }
    if err != nil { return -1 }
    return 0
}

//export CFRealityWrite
func CFRealityWrite(id C.uint64_t, data unsafe.Pointer, size C.size_t) C.int {
    c := registry.get(uint64(id))
    if c == nil || data == nil || size == 0 || size > 65536 { return -1 }
    stream, state := c.snapshot()
    if state != 1 || stream == nil { return -1 }
    n, err := stream.Write(unsafe.Slice((*byte)(data), int(size)))
    if n > 0 { return C.int(n) }
    if err != nil { return -1 }
    return 0
}

//export CFRealityClose
func CFRealityClose(id C.uint64_t) {
    registry.close(uint64(id))
}

func main() {}
