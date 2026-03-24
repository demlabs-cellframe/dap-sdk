/*
 * dap_client_pvt.c - Backward compatibility stub
 *
 * The original implementation has been split into:
 *   - dap_client_fsm.c    (FSM state machine, runs on dedicated FSM threads)
 *   - dap_client_esocket.c (IO/transport layer, runs on worker threads)
 *
 * This file is intentionally empty. All functions are now provided by the
 * new modules, with backward-compatible aliases defined in dap_client_esocket.h:
 *   - dap_client_pvt_init          -> dap_client_esocket_init
 *   - dap_client_pvt_deinit        -> dap_client_esocket_deinit
 *   - dap_client_pvt_new           -> dap_client_esocket_new
 *   - dap_client_pvt_delete_unsafe -> dap_client_esocket_delete_unsafe
 *   - dap_client_pvt_queue_add     -> dap_client_esocket_queue_add
 *   - dap_client_pvt_queue_clear   -> dap_client_esocket_queue_clear
 */
