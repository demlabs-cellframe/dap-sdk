/*
 * dap_client_pvt.c - Backward compatibility stub
 *
 * The original implementation has been split into:
 *   - dap_client_fsm.c    (FSM state machine, runs on dedicated FSM threads)
 *   - dap_client_esocket.c (IO/transport layer, runs on worker threads)
 *
 * This file is intentionally empty. All functions are now provided by the
 * new modules, with backward-compatible aliases defined in dap_client_esocket.h.
 */
