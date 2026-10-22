//go:build integration

package main

import (
    "bytes"
    "context"
    "crypto/ecdh"
    "crypto/rand"
    "crypto/tls"
    "encoding/base64"
    "fmt"
    "io"
    "net"
    "net/http"
    "net/http/httptest"
    "testing"
    "time"

    "github.com/xtls/xray-core/core"
    xnet "github.com/xtls/xray-core/common/net"
    _ "github.com/xtls/xray-core/proxy/vless/inbound"
    _ "github.com/xtls/xray-core/proxy/freedom"
)

func TestRealityGRPCNetwork(t *testing.T) {
    // Local TLS 1.3/H2 target for REALITY's authenticated handshake/fallback.
    target:=httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter,r *http.Request){ w.Write([]byte("test target")) }))
    target.EnableHTTP2=true
    target.TLS=&tls.Config{MinVersion:tls.VersionTLS13}
    target.StartTLS()
    defer target.Close()
    echo,err:=net.Listen("tcp","127.0.0.1:0")
    if err!=nil { t.Fatal(err) }
    defer echo.Close()
    go func(){ for { c,e:=echo.Accept(); if e!=nil{return}; go func(){defer c.Close(); io.Copy(c,c)}() } }()
    reserve,err:=net.Listen("tcp","127.0.0.1:0")
    if err!=nil {t.Fatal(err)}
    port:=reserve.Addr().(*net.TCPAddr).Port
    reserve.Close() // Xray owns the listener; bind failure fails the test.
    key,err:=ecdh.X25519().GenerateKey(rand.Reader)
    if err!=nil {t.Fatal(err)}
    private:=base64.RawURLEncoding.EncodeToString(key.Bytes())
    public:=base64.RawURLEncoding.EncodeToString(key.PublicKey().Bytes())
    serverJSON:=[]byte(fmt.Sprintf(`{"log":{"loglevel":"warning"},"inbounds":[{
      "listen":"127.0.0.1","port":%d,"protocol":"vless",
      "settings":{"clients":[{"id":"00000000-0000-4000-8000-000000000001"}],"decryption":"none"},
      "streamSettings":{"network":"grpc","grpcSettings":{"serviceName":"test","multiMode":true},
      "security":"reality","realitySettings":{"target":"%s","serverNames":["example.com"],"privateKey":"%s","shortIds":[""]}}}],
      "outbounds":[{"protocol":"freedom"}]}`,port,target.Listener.Addr().String(),private))
    server,err:=core.StartInstance("json",serverJSON)
    if err!=nil {t.Fatal(err)}
    defer server.Close()
    for _,fingerprint:=range []string{"firefox","chrome"} {
      t.Run(fingerprint,func(t *testing.T){
        config:=[]byte(fmt.Sprintf(`{"log":{"loglevel":"warning"},"outbounds":[{"protocol":"vless",
          "settings":{"vnext":[{"address":"127.0.0.1","port":%d,"users":[{"id":"00000000-0000-4000-8000-000000000001","encryption":"none"}]}]},
          "streamSettings":{"network":"grpc","security":"reality","grpcSettings":{"serviceName":"test","multiMode":true},
          "realitySettings":{"serverName":"example.com","fingerprint":"%s","publicKey":"%s","shortId":""}}}]}`,port,fingerprint,public))
        if !validConfig(config) {t.Fatal("client config rejected")}
        client,err:=core.StartInstance("json",config)
        if err!=nil {t.Fatal(err)}
        defer client.Close()
        ctx,cancel:=context.WithTimeout(context.Background(),15*time.Second)
        defer cancel()
        dest,err:=xnet.ParseDestination("tcp:"+echo.Addr().String())
        if err!=nil {t.Fatal(err)}
        conn,err:=core.Dial(ctx,client,dest)
        if err!=nil {t.Fatal(err)}
        defer conn.Close()
        done:=make(chan struct{})
        defer close(done)
        go func(){select{case <-ctx.Done(): conn.Close(); case <-done:}}()
        payload:=bytes.Repeat([]byte("opaque DAP payload\x00"),4096)
        result:=make(chan error,1)
        go func(){_,e:=io.Copy(conn,bytes.NewReader(payload)); result<-e}()
        received:=make([]byte,len(payload))
        if _,err=io.ReadFull(conn,received);err!=nil {t.Fatal(err)}
        if err=<-result;err!=nil {t.Fatal(err)}
        if !bytes.Equal(payload,received){t.Fatal("payload mismatch")}
      })
    }
}
