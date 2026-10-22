package main

// Explicit registrations for the embedded client, not the standalone Xray
// distribution. Config parsing may still pull transitive dependencies; this
// list is not a claim that the resulting binary contains only these packages.
import (
    _ "github.com/xtls/xray-core/app/dispatcher"
    _ "github.com/xtls/xray-core/app/proxyman/inbound"
    _ "github.com/xtls/xray-core/app/proxyman/outbound"
    _ "github.com/xtls/xray-core/app/log"
    _ "github.com/xtls/xray-core/app/policy"
    _ "github.com/xtls/xray-core/app/router"
    _ "github.com/xtls/xray-core/main/json"
    _ "github.com/xtls/xray-core/proxy/vless/outbound"
    _ "github.com/xtls/xray-core/transport/internet/grpc"
    _ "github.com/xtls/xray-core/transport/internet/reality"
    _ "github.com/xtls/xray-core/transport/internet/tagged/taggedimpl"
    _ "github.com/xtls/xray-core/transport/internet/tcp"
)
