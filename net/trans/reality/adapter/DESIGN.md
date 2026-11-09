# REALITY transport adapter — дизайн SDK-интеграции (v1)

Статус: v0-интеграция доступна БЕЗ этого адаптера (локальный форвардер,
см. ../README.md). Адаптер — нативная регистрация транспорта для
transport policy/fallback. Писать in-tree (нужна сборка SDK; standalone
syntax-check упирается в генерируемые CMake заголовки: json_config.h и др.).

## Тип и регистрация

- Тип: 0x0A. `dap_net_trans_register()` НЕ валидирует по DAP_NET_TRANS_MAX
  (проверено: net/trans/dap_net_trans.c — только dup-check), поэтому общий
  заголовок dap_net_trans.h не трогаем:
  `#define DAP_NET_TRANS_REALITY ((dap_net_trans_type_t)0x0A)` локально.
- Регистрация: `dap_net_trans_register("reality", DAP_NET_TRANS_REALITY,
  &s_reality_ops, DAP_NET_TRANS_SOCKET_TCP, NULL)` — по образцу
  dap_tls13_client.inc:300-306. Точку вызова добавить в инициализацию
  транспортов клиента (cf_vpn_apple_bridge_stack.c:128-139 регистрирует
  http/udp/websocket/tls/dns) под `#ifdef DAP_SDK_NET_TRANS_REALITY`.
- CMake: option(DAP_SDK_NET_TRANS_REALITY OFF) в lib/dap-sdk/CMakeLists.txt;
  при ON — add_subdirectory(net/trans/reality) + дефайн. Default OFF:
  сборки ноды/плагина не меняются.

## Конфигурация

Секция ini (паттерн tls13:host:port из dap_tls13_client.inc:173-223):
```
[reality:<host>:<port>]
sni = www.samsung.com
public_key = <b64url X25519>
short_id = <hex ≤8B>
uuid = <vless uuid>
rate_limit = 5242880        ; байт/с, 0 = без лимита (DPI: ≤6 МБ/с на cellular)
dest_host = 127.0.0.1       ; VLESS-назначение за xray-фронтом
dest_port = 8079            ; HTTP-DAP listener ноды
iface =                     ; опц. привязка исходящего интерфейса
```
Плюс поля в dap_client_t по образцу tls13_spki_sha256/tls13_server_name
(cf_vpn_types.h:186-201) для доставки через IPC: reality_uuid,
reality_public_key, reality_short_id, reality_sni, port_reality.
IPC-сериализация — по образцу cf_vpn_ipc_types.c:419-434,597-612.

## stage_prepare (ключевой узел)

Образец: s_tls_stage_prepare (dap_stream_trans_tls.c:300-375). Отличия:
1. `reality_bridge_start(&bridge_cfg)` — мост сам коннектит TCP + REALITY +
   VLESS (dest = dest_host:dest_port ноды за xray-фронтом).
2. esocket поверх data-fd моста: `dap_events_socket_wrap_no_add(fd, callbacks)`
   (io/dap_events_socket.c:526, объявление io/include/dap_events_socket.h:471)
   — fd уже «соединён» (socketpair), connect не нужен; флаги
   READY_TO_READ|READY_TO_WRITE как после успешного connect.
3. `dap_worker_add_events_socket(params->worker, es)`.
4. `dap_stream_new_es_client(es, node_addr, authorized)`; `stream->trans = trans`.
5. ctrl-fd моста обернуть ВТОРЫМ esocket (те же worker/callbacks-обёртка):
   событие 'R' → можно слать фазы; 'E'/'C' → error_callback стрима.
   Альтернатива без второго esocket: писать фазы сразу (байты буферизуются
   в socketpair до готовности туннеля — проверено: мост читает data-конец
   только после установления сессии), а 'E'/'C' ловить как EOF data-пайпа.
6. transport_priv стрима = контекст адаптера (паттерн staging из
   dap_tls13_client.inc:203-217).

## Фазы DAP поверх одной трубы (HTTP/1.1 keep-alive)

Серверная сторона без изменений: xray-фронт → 127.0.0.1:8079 (HTTP listener
ноды). Внутри туннеля адаптер говорит ТОТ ЖЕ HTTP-DAP, что и HTTP-транспорт:
1. handshake_init: `POST /enc_init/<node_b58>?enc_type=...,pkey_exchange_type=...,
   pkey_exchange_size=...,block_key_size=...,protocol_version=...,sign_count=...`
   body = base64(raw pubkey + подписи) — формат и построение тела полностью
   как s_http_trans_handshake_init (dap_net_trans_http_stream.c:844-960:
   dap_cert_add_sign_to_data, dap_enc_base64_encode,
   dap_client_enc_init_url_path, DAP_UPLINK_PATH_ENC_INIT).
2. session_create: `POST /stream_ctl/...` — как s_http_trans_session_create
   (там же :1022-1110); ответ содержит session_id.
3. session_start: `GET /stream?session_id=X HTTP/1.1` — как
   s_http_trans_connect/:1149-1186; после "HTTP/1.1 200" труба становится
   сырым двусторонним потоком (stream takeover) — further read/write ops
   ходят напрямую в bridge pipe.
4. Отличия от HTTP-транспорта: ВСЕ фазы на ОДНОМ соединении (keep-alive),
   а не transient-коннектах; проверить, что http_server ноды держит
   keep-alive между POST-фазами (ожидается да; если нет — fallback:
   фаза = отдельный bridge, см. «Альтернатива»).

Механика: запись запроса в pipe; ответ накапливается в packet_in-колбэке
обёртки esocket до "\r\n\r\n" + Content-Length; затем вызов phase callback
(stream, 0). Обёртка callbacks — по прецеденту WSS layered upgrade
(VPN_TRANSPORT_ROADMAP.md: «HTTP route имеет explicit layered_upgrade_safe,
peer — Upgrade pending/active и callbacks plaintext protocol»): наши
callbacks перехватывают пакет до takeover стрима, после 200 — ставят
peer-callbacks стрима напрямую.

## Альтернатива (если keep-alive фаз не подтвердится)

Модель HTTP-транспорта дословно: каждая фаза — отдельный bridge
(transient), стрим-фаза — постоянный bridge. Дороже (3 REALITY-хендшейка
~90 мс каждый), но zero-risk по серверу. Переключается флагом конфига.

## ops vtable (итог)

```c
static const dap_net_trans_ops_t s_reality_ops = {
    .init             = s_reality_init,        /* config section cache */
    .deinit           = s_reality_deinit,
    .handshake_init   = s_reality_handshake,   /* POST /enc_init в pipe */
    .session_create   = s_reality_session,     /* POST /stream_ctl */
    .session_start    = s_reality_start,       /* GET /stream + takeover */
    .write            = s_reality_write,       /* в bridge pipe (esocket write_unsafe) */
    .close            = s_reality_close,       /* bridge_stop + esocket delete */
    .get_capabilities = s_reality_caps,        /* RELIABLE|ORDERED|BIDIRECTIONAL|OBFUSCATION|MIMICRY */
    .stage_prepare    = s_reality_prepare,     /* bridge + wrap_no_add + stream */
    .get_max_packet_size = NULL,               /* streaming: 0/NULL */
};
```
listen/accept/register_server_handlers = NULL (сервер — xray-фронт, B5a).

## Policy и fallback

- cf_vpn_transport_policy.c: REALITY в transport_priority как основной на
  cellular; WSS — fallback (stall >30 с); реконнект-бэкофф 30/60/120 с
  (cooldown DPI ≈2 мин — docs/MOBILE_DPI_RESULTS_2026-11-07.md).
- rate_limit из конфига прокидывать в reality_config_t (token bucket в ядре).
- GUI: опция «Reality» в TransportsStackPage.qml по образцу TLS13/WSS
  (гейт configManager), IPC-поля из раздела «Конфигурация».

## Чек-лист in-tree верификации

1. Сборка с -DDAP_SDK_NET_TRANS_REALITY=ON (клиентские таргеты; нода/плагин OFF).
2. Loopback: адаптер → bridge → xray (docker v26.10.31) → 127.0.0.1:8079
   ноды: полный цикл ENC→CTL→STREAM, двусторонний трафик.
3. Keep-alive проверка фаз на одном соединении; при проблеме — флаг Альтернативы.
4. Mobile E2E по протоколу ~/vpn-test-results.md (включая rate-cap и
   fallback на WSS при принудительной резке).
5. Paid-flow инвариант: serial-key авторизация проходит БЕЗ изменений кода
   оплаты (требование docs/WSS_REALITY_INTEGRATION_PLAN_2026-11-07.md).
