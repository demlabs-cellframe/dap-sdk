package main

import (
    "crypto/ecdh"
    "crypto/rand"
    "encoding/base64"
    "fmt"
    "testing"

    "github.com/xtls/xray-core/core"
)

// Creates an instance without dialing the test endpoint. This verifies runtime
// registrations, not the remote handshake or actual browser fingerprint.
func TestEmbeddedModulesCreateInstance(t *testing.T) {
    key,err:=ecdh.X25519().GenerateKey(rand.Reader)
    if err!=nil { t.Fatal(err) }
    public:=base64.RawURLEncoding.EncodeToString(key.PublicKey().Bytes())
    config:=[]byte(fmt.Sprintf(`{
      "log":{"loglevel":"none"},
      "outbounds":[{"protocol":"vless","settings":{"vnext":[{
        "address":"127.0.0.1","port":1,"users":[{
          "id":"00000000-0000-4000-8000-000000000001","encryption":"none"}]}]},
        "streamSettings":{"network":"grpc","security":"reality",
          "grpcSettings":{"serviceName":"test","multiMode":true},
          "realitySettings":{"serverName":"example.com","fingerprint":"firefox",
            "publicKey":"%s","shortId":""}}}]
    }`,public))
    if !validConfig(config) { t.Fatal("embedded config rejected") }
    instance,err:=core.StartInstance("json",config)
    if err!=nil { t.Fatal(err) }
    if err=instance.Close(); err!=nil { t.Fatal(err) }
}
