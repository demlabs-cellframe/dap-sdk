package main

import (
    "context"
    "net"
    "sync"
    "sync/atomic"
    "testing"
    "time"
)

type countedCloser struct { calls atomic.Int32 }
func (c *countedCloser) Close() error { c.calls.Add(1); return nil }

func TestCloseUnblocksReadAndIsIdempotent(t *testing.T) {
    r := connectionRegistry{entries: make(map[uint64]*connection)}
    left, right := net.Pipe()
    defer right.Close()
    ctx, cancel := context.WithCancel(context.Background())
    owner := &countedCloser{}
    id := r.add(&connection{instance: owner, stream: left, cancel: cancel, state: 1})
    c := r.get(id)
    done := make(chan error, 1)
    stream, _ := c.snapshot()
    go func() { var b [1]byte; _, err := stream.Read(b[:]); done <- err }()
    var wg sync.WaitGroup
    for i:=0; i<16; i++ { wg.Add(1); go func(){ defer wg.Done(); r.close(id) }() }
    wg.Wait()
    select {
    case err := <-done: if err == nil { t.Fatal("closed read succeeded") }
    case <-time.After(time.Second): t.Fatal("close did not unblock read")
    }
    if r.get(id)!=nil || owner.calls.Load()!=1 || ctx.Err()==nil { t.Fatal("invalid lifetime") }
}

func TestHandleExhaustionDoesNotReuseIDs(t *testing.T) {
    r := connectionRegistry{next: ^uint64(0), entries: make(map[uint64]*connection)}
    if r.add(&connection{})!=0 || r.add(&connection{})!=0 { t.Fatal("handle reused after overflow") }
}

func TestOpenDeadlineRejectsLateSuccess(t *testing.T) {
    ctx,cancel := context.WithCancel(context.Background())
    c := &connection{cancel:cancel}
    c.armOpenDeadline(time.Millisecond)
    select {
    case <-ctx.Done():
    case <-time.After(time.Second): t.Fatal("deadline did not cancel")
    }
    left,right := net.Pipe(); defer right.Close()
    owner := &countedCloser{}
    c.finish(owner,left,nil)
    stream,state := c.snapshot()
    if stream!=nil || state!=-1 || c.errorCode()!=2 || owner.calls.Load()!=1 {
        t.Fatal("late success overwrote deadline")
    }
}

func TestSuccessfulOpenStopsDeadline(t *testing.T) {
    ctx,cancel := context.WithCancel(context.Background())
    c := &connection{cancel:cancel}
    left,right := net.Pipe(); defer right.Close(); defer left.Close(); defer cancel()
    c.armOpenDeadline(time.Hour)
    c.finish(&countedCloser{},left,nil)
    c.mu.Lock(); stopped := c.openTimer==nil; c.mu.Unlock()
    if !stopped || ctx.Err()!=nil { t.Fatal("successful connection retains setup timer") }
}

func TestHandshakeDeadlineClosesLocalStream(t *testing.T) {
    ctx,cancel := context.WithCancel(context.Background())
    c := &connection{cancel:cancel}
    left,right := net.Pipe(); defer right.Close()
    owner := &countedCloser{}
    c.finish(owner,left,nil)
    c.armHandshakeDeadline(time.Millisecond)
    done := make(chan error,1)
    go func(){ var b [1]byte; _,err:=left.Read(b[:]); done<-err }()
    select {
    case err:=<-done: if err==nil { t.Fatal("read not cancelled") }
    case <-time.After(time.Second): t.Fatal("handshake deadline stuck")
    }
    // Wait for cleanup after stream close before checking its owner.
    deadline:=time.Now().Add(time.Second)
    for owner.calls.Load()!=1 && time.Now().Before(deadline) { time.Sleep(time.Millisecond) }
    if c.errorCode()!=3 || ctx.Err()==nil || c.confirmHandshake() || owner.calls.Load()!=1 {
        t.Fatal("invalid handshake timeout state")
    }
}

func TestHandshakeConfirmationAndCloseRace(t *testing.T) {
    for i:=0;i<100;i++ {
        r:=connectionRegistry{entries:make(map[uint64]*connection)}
        _,cancel:=context.WithCancel(context.Background())
        c:=&connection{cancel:cancel}
        left,right:=net.Pipe()
        owner:=&countedCloser{}
        c.finish(owner,left,nil)
        id:=r.add(c)
        c.armHandshakeDeadline(time.Millisecond)
        var wg sync.WaitGroup; wg.Add(2)
        go func(){ defer wg.Done(); c.confirmHandshake() }()
        go func(){ defer wg.Done(); r.close(id) }()
        wg.Wait(); right.Close()
        deadline:=time.Now().Add(time.Second)
        for owner.calls.Load()!=1 && time.Now().Before(deadline) { time.Sleep(time.Millisecond) }
        if owner.calls.Load()!=1 { t.Fatal("owner cleanup must happen once") }
    }
}

func TestCancelledOpenDisposesLateResult(t *testing.T) {
    for i:=0;i<100;i++ {
        r := connectionRegistry{entries: make(map[uint64]*connection)}
        ctx,cancel := context.WithCancel(context.Background())
        c := &connection{cancel:cancel}
        id := r.add(c)
        left,right := net.Pipe()
        owner := &countedCloser{}
        var wg sync.WaitGroup
        wg.Add(2)
        go func(){ defer wg.Done(); r.close(id) }()
        go func(){ defer wg.Done(); c.finish(owner,left,nil) }()
        wg.Wait()
        _,state := c.snapshot()
        if state!=-2 || ctx.Err()==nil || owner.calls.Load()!=1 { t.Fatal("late result retained") }
        right.Close()
    }
}
