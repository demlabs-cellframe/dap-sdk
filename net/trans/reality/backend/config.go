package main

import "encoding/json"

// Accept only the intended embedded mode; reject arbitrary listener/direct
// configurations rather than accidentally routing DAP outside the tunnel.
func validConfig(raw []byte) bool {
    var c struct {
        Inbounds []json.RawMessage `json:"inbounds"`
        Outbounds []struct {
            Protocol string `json:"protocol"`
            Stream struct {
                Network string `json:"network"`
                Security string `json:"security"`
            } `json:"streamSettings"`
        } `json:"outbounds"`
        Routing json.RawMessage `json:"routing"`
    }
    if json.Unmarshal(raw, &c) != nil || len(c.Inbounds) != 0 || len(c.Outbounds) != 1 || len(c.Routing) != 0 { return false }
    o := c.Outbounds[0]
    return o.Protocol == "vless" && o.Stream.Network == "grpc" && o.Stream.Security == "reality"
}
