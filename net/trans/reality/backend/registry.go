package main

import (
    "context"
    "io"
    "net"
    "sync"
    "time"
)

// The registry owns lifetime, not transport configuration or C buffers.
// Taking a reference never holds the registry lock during blocking I/O.
type connection struct {
    mu sync.Mutex
    instance io.Closer
    stream net.Conn
    cancel context.CancelFunc
    state int // 0 opening, 1 stream available (not remote DAP ready), -1 failed, -2 closed
    openTimer *time.Timer
    failure int // 0 none, 1 backend/open failure, 2 opening deadline
    handshakeTimer *time.Timer
    handshakeConfirmed bool
}

// Independent from setup: core.Dial can return before any network handshake.
// The SDK must confirm only after receiving a valid DAP handshake response.
func (c *connection) armHandshakeDeadline(delay time.Duration) {
    c.mu.Lock()
    defer c.mu.Unlock()
    if c.state < 0 || c.handshakeConfirmed || c.handshakeTimer != nil { return }
    c.handshakeTimer = time.AfterFunc(delay, func() {
        c.mu.Lock()
        if c.state < 0 || c.handshakeConfirmed { c.mu.Unlock(); return }
        c.state, c.failure = -1, 3
        stream, instance := c.stream, c.instance
        c.stream, c.instance = nil, nil
        c.mu.Unlock()
        c.cancel()
        if stream != nil { stream.Close() }
        if instance != nil { instance.Close() }
    })
}

func (c *connection) confirmHandshake() bool {
    c.mu.Lock()
    defer c.mu.Unlock()
    if c.state != 1 { return false }
    c.handshakeConfirmed = true
    if c.handshakeTimer != nil { c.handshakeTimer.Stop(); c.handshakeTimer = nil }
    return true
}

func (c *connection) armOpenDeadline(delay time.Duration) {
    c.mu.Lock()
    defer c.mu.Unlock()
    if c.state != 0 || c.openTimer != nil { return }
    c.openTimer = time.AfterFunc(delay, func() {
        c.mu.Lock()
        if c.state != 0 { c.mu.Unlock(); return }
        c.state, c.failure = -1, 2
        c.mu.Unlock()
        c.cancel()
    })
}

func (c *connection) errorCode() int {
    c.mu.Lock()
    defer c.mu.Unlock()
    return c.failure
}

func (c *connection) finish(instance io.Closer, stream net.Conn, err error) {
    c.mu.Lock()
    if c.openTimer != nil { c.openTimer.Stop(); c.openTimer = nil }
    if c.state != 0 || err != nil {
        if c.handshakeTimer != nil { c.handshakeTimer.Stop(); c.handshakeTimer = nil }
        if c.state == 0 { c.state, c.failure = -1, 1 }
        c.mu.Unlock()
        if stream != nil { stream.Close() }
        if instance != nil { instance.Close() }
        c.cancel()
        return
    }
    c.instance, c.stream, c.state = instance, stream, 1
    c.mu.Unlock()
}

func (c *connection) snapshot() (net.Conn, int) {
    c.mu.Lock()
    defer c.mu.Unlock()
    return c.stream, c.state
}

type connectionRegistry struct {
    sync.Mutex
    next uint64
    exhausted bool
    entries map[uint64]*connection
}

var registry = connectionRegistry{entries: make(map[uint64]*connection)}

func (r *connectionRegistry) add(c *connection) uint64 {
    r.Lock()
    defer r.Unlock()
    // Never reuse IDs, including after uint64 wraparound (stale C handles).
    if r.exhausted { return 0 }
    r.next++
    if r.next == 0 { r.exhausted = true; return 0 }
    r.entries[r.next] = c
    return r.next
}

func (r *connectionRegistry) get(id uint64) *connection {
    r.Lock()
    defer r.Unlock()
    return r.entries[id]
}

func (r *connectionRegistry) close(id uint64) {
    r.Lock()
    c := r.entries[id]
    delete(r.entries, id)
    r.Unlock()
    if c != nil {
        c.mu.Lock()
        if c.openTimer != nil { c.openTimer.Stop(); c.openTimer = nil }
        if c.handshakeTimer != nil { c.handshakeTimer.Stop(); c.handshakeTimer = nil }
        c.state = -2
        stream, instance := c.stream, c.instance
        c.stream, c.instance = nil, nil
        c.mu.Unlock()
        c.cancel()
        if stream != nil { stream.Close() }
        if instance != nil { instance.Close() }
    }
}
