/* TLS13 uses the same DAP request builders as mimicry, not its wire engine. */
#include "dap_tls13_connection.h"
#include "dap_tls13_client_state.h"
#include "dap_client_trans_ctx.h"
#include "dap_wss_dap.h"
#include "dap_wss_client_upgrade.h"
#include "rand/dap_rand.h"
typedef struct tls13_client {
    /* DAP request parameters; never cast the transport context to mimicry. */
    tls_mimicry_ctx_t dap;
    dap_tls13_connection_t connection;
    dap_tls13_client_state_t state;
    dap_stream_t *stream;
    uint8_t *alice;
    char *pin, *sni;
    bool wss;
    dap_wss_dap_t ws;
    dap_wss_client_upgrade_t upgrade;
} tls13_client_t;

static int s_tls13_ws_wire(void *arg,const void *data,size_t size)
{
    tls13_client_t *c=arg;
    return dap_tls_plain_stream_write(&c->connection.endpoint.channel.tls,
        &c->connection.endpoint.channel.outgoing,data,size)==DAP_TLS_PLAIN_WRITE_ACCEPTED ? 0 : -1;
}
static int s_tls13_ws_tail(void *arg,const void *data,size_t size)
{
    tls13_client_t *c=arg;
    return dap_wss_dap_input(&c->ws,data,size);
}
static int s_tls13_ws_ready(void *arg);
static int s_tls13_ws_input(void *arg,const void *data,size_t size)
{
    tls13_client_t *c=arg;
    return dap_wss_client_upgrade_feed(&c->upgrade,data,size,
        s_tls13_ws_ready,s_tls13_ws_tail,c);
}
static int s_tls13_ws_eof(void *arg)
{
    tls13_client_t *c=arg;
    return !c->upgrade.ready || c->ws.incoming.size || c->ws.dap.header_size ||
        c->ws.dap.payload || c->ws.fragmented ? -1 : 0;
}

static int s_tls13_client_enc_reply(void *arg,const void *data,size_t size)
{
    tls13_client_t *c=arg;
    dap_net_trans_handshake_cb_t callback=c->dap.handshake_cb;
    c->dap.handshake_cb=NULL;
    if (!callback) return -1;
    callback(c->stream,data,size,0);
    return 0;
}
static int s_tls13_client_ctl_reply(void *arg,const void *data,size_t size)
{
    tls13_client_t *c=arg;
    dap_net_trans_session_cb_t callback=c->dap.session_create_cb;
    c->dap.session_create_cb=NULL;
    if (!callback) return -1;
    /* Existing callback takes ownership, frame reader only lends data. */
    char *owned=calloc(1,size+1);
    if (!owned) return -1;
    memcpy(owned,data,size);
    c->dap.phase=TLS_PHASE_STREAMING;
    callback(c->stream,0,owned,size,0);
    return 0;
}
static int s_tls13_client_data(void *arg,const void *data,size_t size)
{
    tls13_client_t *c=arg;
    return dap_stream_data_proc_read_ext(c->stream,data,size)==size ? 0 : -1;
}
static void s_tls13_client_ready(void *arg)
{
    tls13_client_t *c=arg;
    if(c->wss) {
        uint8_t nonce[16]; char key[25];
        randombytes(nonce,sizeof(nonce));
        size_t n=dap_enc_base64_encode(nonce,sizeof(nonce),key,DAP_ENC_DATA_TYPE_B64);
        if(n!=24) {c->stream->esocket->flags|=DAP_SOCK_SIGNAL_CLOSE;return;}
        key[n]=0;
        dap_client_t *client=c->stream->trans_ctx && c->stream->trans_ctx->_inheritor ?
            ((dap_client_trans_ctx_t *)c->stream->trans_ctx->_inheritor)->client : NULL;
        const char *host=c->sni ? c->sni : client ? client->link_info.uplink_addr : NULL;
        if(dap_wss_client_upgrade_start(&c->upgrade,host,"/vpn",key,s_tls13_ws_wire,c))
            c->stream->esocket->flags|=DAP_SOCK_SIGNAL_CLOSE;
        return;
    }
    c->state.phase=DAP_TLS13_WAIT_ENC;
    c->dap.phase=TLS_PHASE_ENC_INIT_WAIT;
    if (s_tls_send_enc_init(c->stream,&c->dap,c->stream->esocket))
        c->stream->esocket->flags|=DAP_SOCK_SIGNAL_CLOSE;
}
static int s_tls13_ws_ready(void *arg)
{
    tls13_client_t *c=arg;
    c->state.phase=DAP_TLS13_WAIT_ENC;
    c->dap.phase=TLS_PHASE_ENC_INIT_WAIT;
    return s_tls_send_enc_init(c->stream,&c->dap,c->stream->esocket);
}
static ssize_t s_tls13_client_write(dap_stream_t *stream,const void *data,size_t size)
{
    tls13_client_t *c=stream && stream->trans_ctx ? stream->trans_ctx->transport_priv : NULL;
    if (!c) return -1;
    uint8_t type;
    switch(c->state.phase) {
        case DAP_TLS13_WAIT_ENC: type=DAP_TLS13_ENC_REQUEST; break;
        case DAP_TLS13_WAIT_CTL: type=DAP_TLS13_CTL_REQUEST; break;
        case DAP_TLS13_STREAMING: type=DAP_TLS13_STREAM; break;
        default: return -1;
    }
    if (c->wss ? dap_wss_dap_send(&c->ws,type,data,size) :
        dap_tls13_channel_send(&c->connection.endpoint.channel,type,data,size)) {
        stream->esocket->flags|=DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    return (ssize_t)size;
}
static void s_tls13_client_close(dap_stream_t *stream)
{
    if (!stream || !stream->trans_ctx) return;
    tls13_client_t *c=stream->trans_ctx->transport_priv;
    if (!c) return;
    c->state.phase=DAP_TLS13_CLOSED;
    dap_tls13_connection_clear(&c->connection);
    dap_wss_dap_clear(&c->ws);
    dap_wss_client_upgrade_clear(&c->upgrade);
    free(c->alice); free(c->pin); free(c->sni);
    free(c);
    stream->trans_ctx->transport_priv=NULL;
}
static int s_tls13_client_prepare(dap_net_trans_t *trans,const dap_net_stage_prepare_params_t *params,
                                 dap_net_stage_prepare_result_t *result)
{
    if (!params || !params->host || !result) return -1;
    memset(result, 0, sizeof(*result));
    result->error_code = -1;
    /* Endpoint-specific configuration; a pin is never inherited from another host. */
    char section[512];
    int n=snprintf(section,sizeof(section),"tls13:%s:%u",params->host,params->port);
    if (n<0 || (size_t)n>=sizeof(section)) return -1;
    char provided_pin[65] = {0}, provided_name[254] = {0};
    const char *pin=NULL, *sni=NULL;
    dap_client_t *client = params->client_ctx;
    if (client && client->tls13_spki_sha256[0]) {
        if (!memchr(client->tls13_spki_sha256,0,sizeof(client->tls13_spki_sha256)) ||
            !memchr(client->tls13_server_name,0,sizeof(client->tls13_server_name))) return -1;
        pin=client->tls13_spki_sha256;
        sni=client->tls13_server_name[0] ? client->tls13_server_name : NULL;
    } else if (s_tls13_trust_provider) {
        if (s_tls13_trust_provider(params->host,params->port,
                provided_pin,sizeof(provided_pin),provided_name,sizeof(provided_name),
                s_tls13_trust_arg) ||
            !memchr(provided_pin,0,sizeof(provided_pin)) ||
            !memchr(provided_name,0,sizeof(provided_name))) return -1;
        pin=provided_pin;
        sni=provided_name[0] ? provided_name : NULL;
    } else if (g_config) {
        pin=dap_config_get_item_str(g_config,section,"spki_sha256");
        sni=dap_config_get_item_str(g_config,section,"server_name");
    } else return -1;
    uint8_t decoded[32];
    if (dap_tls13_pin_decode(pin,decoded)) return -1;
    tls13_client_t *c=calloc(1,sizeof(*c));
    if (!c) return -1;
    c->wss=trans && trans->type == DAP_NET_TRANS_WSS;
    c->pin=dap_strdup(pin); c->sni=sni ? dap_strdup(sni) : NULL;
    if (!c->pin || (sni && !c->sni)) { free(c->pin); free(c->sni); free(c); return -1; }
    /* dap_stream_new_es_client does not allocate trans_ctx. Supply a staging
     * context before publishing transport_priv; FSM adopts it after prepare. */
    dap_net_trans_ctx_t *staging=calloc(1,sizeof(*staging));
    if (!staging) { free(c->pin); free(c->sni); free(c); return -1; }
    int rc=s_tls_stage_prepare(trans,params,result);
    if (rc) { free(staging); free(c->pin); free(c->sni); free(c); return rc; }
    c->stream=result->stream;
    staging->stream=c->stream;
    staging->esocket=result->esocket;
    staging->trans=trans;
    c->stream->trans_ctx=staging;
    c->stream->trans_ctx->transport_priv=c;
    return 0;
}
static int s_tls13_client_handshake(dap_stream_t *stream,dap_net_handshake_params_t *params,
                                   dap_net_trans_handshake_cb_t callback)
{
    tls13_client_t *c=stream && stream->trans_ctx ? stream->trans_ctx->transport_priv : NULL;
    if (!c || !params || !callback || c->connection.endpoint.context) return -1;
    if (!params->alice_pub_key || !params->alice_pub_key_size || params->alice_pub_key_size>65536) return -1;
    c->alice=malloc(params->alice_pub_key_size);
    if (!c->alice) return -1;
    memcpy(c->alice,params->alice_pub_key,params->alice_pub_key_size);
    c->dap.handshake_params=*params;
    c->dap.handshake_params.alice_pub_key=c->alice;
    c->dap.handshake_cb=callback;
    c->state=(dap_tls13_client_state_t){.enc_reply=s_tls13_client_enc_reply,
        .ctl_reply=s_tls13_client_ctl_reply,.stream_data=s_tls13_client_data,.arg=c};
    int rc;
    if(c->wss) {
        rc=dap_wss_dap_init(&c->ws,false,dap_tls13_client_dispatch,&c->state,s_tls13_ws_wire,c);
        if(!rc) rc=dap_tls13_endpoint_client_protocol(&c->connection.endpoint,c->pin,c->sni,
            dap_tls13_socket_send_wire,&c->connection.io,dap_tls13_client_dispatch,&c->state,true);
        if(!rc) {
            c->connection.endpoint.channel.plain_input=s_tls13_ws_input;
            c->connection.endpoint.channel.plain_eof=s_tls13_ws_eof;
            c->connection.endpoint.channel.plain_arg=c;
            rc=dap_tls13_socket_bind(&c->connection.io,stream->esocket,&c->connection.endpoint.channel);
        }
        if(!rc) {
            c->connection.io.ready=s_tls13_client_ready;
            c->connection.io.ready_arg=c;
            rc=dap_tls13_socket_drive(&c->connection.io)<0 ? -1 : 0;
        }
        if(rc) {dap_tls13_connection_clear(&c->connection);dap_wss_dap_clear(&c->ws);}
    } else rc = dap_tls13_connection_client(&c->connection,stream->esocket,c->pin,c->sni,
        dap_tls13_client_dispatch,&c->state,s_tls13_client_ready,c);
    /* FSM transfers the original buffer only on successful initiation.
     * Our asynchronous request uses its own copy until transport teardown. */
    if (!rc) DAP_DELETE(params->alice_pub_key);
    return rc;
}
static int s_tls13_client_session(dap_stream_t *stream,dap_net_session_params_t *params,
                                 dap_net_trans_session_cb_t callback)
{
    tls13_client_t *c=stream && stream->trans_ctx ? stream->trans_ctx->transport_priv : NULL;
    if (!c || !params || !callback || c->state.phase!=DAP_TLS13_ENC_DONE) return -1;
    c->state.phase=DAP_TLS13_WAIT_CTL;
    c->dap.session_create_cb=callback;
    int rc=s_tls_send_session_request(stream,params);
    if (rc) {
        c->dap.session_create_cb=NULL;
        c->state.phase=DAP_TLS13_CLOSED;
        stream->esocket->flags|=DAP_SOCK_SIGNAL_CLOSE;
    }
    return rc;
}
static int s_tls13_client_start(dap_stream_t *stream,uint32_t session,dap_net_trans_ready_cb_t callback)
{
    tls13_client_t *c=stream && stream->trans_ctx ? stream->trans_ctx->transport_priv : NULL;
    if (!c || !session || c->state.phase!=DAP_TLS13_STREAMING) return -1;
    /* Retire the ENC-stage UUID holder once; retain our socket delete wrapper
     * but delegate to the normal STREAM cleanup after this transition. */
    dap_events_socket_callbacks_t callbacks;
    dap_client_trans_ctx_get_stream_callbacks(&callbacks);
    free(c->connection.io.previous_arg);
    c->connection.io.previous_arg = NULL;
    c->connection.io.previous_delete = callbacks.delete_callback;
    c->connection.io.previous_read = callbacks.read_callback;
    c->connection.io.previous_write = callbacks.write_callback;
    c->connection.io.previous_error = callbacks.error_callback;
    if (callback) callback(stream,0);
    return 0;
}
static const dap_net_trans_ops_t s_tls13_client_ops={
    .stage_prepare=s_tls13_client_prepare,.handshake_init=s_tls13_client_handshake,
    .session_create=s_tls13_client_session,.session_start=s_tls13_client_start,
    .write=s_tls13_client_write,.close=s_tls13_client_close,.get_capabilities=s_tls_get_caps
};
static int s_tls13_register_client(void)
{
    int rc = dap_net_trans_register("tls13",DAP_NET_TRANS_TLS13,&s_tls13_client_ops,
        DAP_NET_TRANS_SOCKET_TCP,NULL);
    if (rc) return rc;
    return dap_net_trans_register("wss",DAP_NET_TRANS_WSS,&s_tls13_client_ops,
        DAP_NET_TRANS_SOCKET_TCP,NULL);
}
