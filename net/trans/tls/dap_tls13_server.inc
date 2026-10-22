/* Included after the common bounded HTTP parser and DAP request router.
 * TLS13 has a dedicated listener and never auto-detects legacy mimicry. */
#include "dap_tls13_connection.h"
#include "dap_tls13_server_dispatch.h"
typedef struct tls13_listener {
    dap_server_t *server;
    char *cert, *key;
} tls13_listener_t;
typedef struct tls13_peer {
    dap_tls13_connection_t connection;
    dap_tls13_server_dispatch_t dispatch;
    dap_events_socket_t *es;
    dap_stream_t *stream;
} tls13_peer_t;

static int s_tls13_send(void *arg, uint8_t type, const void *data, size_t size)
{
    tls13_peer_t *p = arg;
    return dap_tls13_channel_send(&p->connection.endpoint.channel, type, data, size);
}
static ssize_t s_tls13_stream_write(dap_stream_t *stream, const void *data, size_t size)
{
    tls13_peer_t *p = stream && stream->esocket ? stream->esocket->_inheritor : NULL;
    if (!p || p->dispatch.phase != DAP_TLS13_SERVER_STREAM) return -1;
    if (s_tls13_send(p, DAP_TLS13_STREAM, data, size)) {
        p->es->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    return (ssize_t)size;
}
static const dap_net_trans_ops_t s_tls13_server_stream_ops = {.write=s_tls13_stream_write};
static dap_net_trans_t s_tls13_server_transport = {
    .type=DAP_NET_TRANS_TLS13, .ops=&s_tls13_server_stream_ops,
    .socket_type=DAP_NET_TRANS_SOCKET_TCP
};
static int s_tls13_data(void *arg, const void *data, size_t size)
{
    tls13_peer_t *p = arg;
    if (!p->stream) return -1;
    return dap_stream_data_proc_read_ext(p->stream, data, size) == size ? 0 : -1;
}
static int s_tls13_control(tls13_peer_t *p, bool enc, const void *data, size_t size,
                           void **out, size_t *out_size)
{
    char *path=NULL, *query=NULL, *body=NULL;
    size_t body_size=0;
    int rc=s_parse_http_post((char *)data,size,&path,&query,&body,&body_size);
    const char *prefix=enc ? "/enc_init/" : "/stream_ctl/";
    if (rc || !path || strncmp(path,prefix,strlen(prefix))) { rc=-1; goto done; }
    rc=s_route_request(path,query,body,body_size,(char **)out,out_size);
    if (rc || !*out || !*out_size) { rc=-1; goto done; }
    if (!enc) {
        char *reply=calloc(1,*out_size+1);
        if (!reply) { rc=-1; goto done; }
        memcpy(reply,*out,*out_size);
        unsigned id=0;
        int parsed=sscanf(reply,"%u",&id);
        free(reply);
        dap_stream_session_t *session=parsed==1 && id ? dap_stream_session_id_mt(id) : NULL;
        if (!session || p->stream) { rc=-1; goto done; }
        p->stream=dap_stream_new_es_server(p->es,session);
        if (!p->stream) { rc=-1; goto done; }
        p->stream->trans=&s_tls13_server_transport;
    }
done:
    free(path); free(query); free(body);
    return rc;
}
static int s_tls13_enc(void *arg,const void *data,size_t size,void **out,size_t *out_size)
{ return s_tls13_control(arg,true,data,size,out,out_size); }
static int s_tls13_ctl(void *arg,const void *data,size_t size,void **out,size_t *out_size)
{ return s_tls13_control(arg,false,data,size,out,out_size); }
static void s_tls13_delete(dap_events_socket_t *es, void *arg)
{
    (void)arg;
    tls13_peer_t *p=es->_inheritor;
    if (!p) return;
    dap_tls13_connection_clear(&p->connection);
    if (p->stream) dap_stream_delete_unsafe(p->stream);
    es->_inheritor=NULL;
    free(p);
}
static void s_tls13_new(dap_events_socket_t *es, void *arg)
{
    (void)arg;
    tls13_listener_t *listener=es->server ? es->server->_inheritor : NULL;
    if (!listener) { es->flags|=DAP_SOCK_SIGNAL_CLOSE; return; }
    tls13_peer_t *p=calloc(1,sizeof(*p));
    if (!p) { es->flags|=DAP_SOCK_SIGNAL_CLOSE; return; }
    p->es=es;
    p->dispatch=(dap_tls13_server_dispatch_t){.enc=s_tls13_enc,.ctl=s_tls13_ctl,
        .send=s_tls13_send,.data=s_tls13_data,.arg=p};
    es->_inheritor=p;
    es->callbacks.delete_callback=s_tls13_delete;
    if (dap_tls13_connection_server(&p->connection,es,listener->cert,listener->key,
            dap_tls13_server_dispatch_frame,&p->dispatch)) es->flags|=DAP_SOCK_SIGNAL_CLOSE;
}
static void *s_tls13_new_listener(const char *name)
{ (void)name; return calloc(1,sizeof(tls13_listener_t)); }
static void s_tls13_stop(void *arg)
{
    tls13_listener_t *s=arg;
    if (s->server) { dap_server_delete(s->server); s->server=NULL; }
}
static int s_tls13_start(void *arg,const char *cfg,const char **addresses,uint16_t *ports,size_t count)
{
    tls13_listener_t *s=arg;
    if (!s || s->server || !ports || !count || !g_config) return -1;
    /* Node passes the generic "server" section to all transport factories.
     * Credentials belong to this listener, never to global server settings. */
    const char *section=cfg && strcmp(cfg,"server") ? cfg : "server_transport_tls13";
    const char *cert=dap_config_get_item_str(g_config,section,"certificate");
    const char *key=dap_config_get_item_str(g_config,section,"private_key");
    dap_tls_context_t *check=dap_tls_context_new_server(cert,key);
    if (!check) return -1;
    dap_tls_context_free(check);
    free(s->cert); free(s->key);
    s->cert=dap_strdup(cert); s->key=dap_strdup(key);
    if (!s->cert || !s->key) return -1;
    dap_events_socket_callbacks_t callbacks={.accept_callback=dap_net_server_accept_callback,.new_callback=s_tls13_new};
    s->server=dap_server_new(NULL,NULL,&callbacks);
    if (!s->server) return -1;
    s->server->_inheritor=s;
    dap_events_socket_callbacks_t listener={.accept_callback=dap_net_server_accept_callback};
    for(size_t i=0;i<count;i++)
        if(dap_server_listen_addr_add(s->server,addresses && addresses[i] ? addresses[i] : "0.0.0.0",
            ports[i],DESCRIPTOR_TYPE_SOCKET_LISTENING,&listener)) { s_tls13_stop(s); return -1; }
    return 0;
}
static void s_tls13_delete_listener(void *arg)
{
    tls13_listener_t *s=arg;
    if (!s) return;
    s_tls13_stop(s); free(s->cert); free(s->key); free(s);
}
static const dap_net_trans_server_ops_t s_tls13_server_ops={
    .new=s_tls13_new_listener,.start=s_tls13_start,.stop=s_tls13_stop,.delete=s_tls13_delete_listener
};
