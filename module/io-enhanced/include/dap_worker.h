/**
 * @file dap_worker.h
 * @brief Public worker I/O entry — @ref dap_worker_reactor.h (enhanced epoll/IOCP runtime).
 *
 * Per architecture plan: keep a single “worker” include name; implementation lives
 * in the same headers as today — this file is a stable include path for the
 * conn → bus → worker → io stack.
 */
#pragma once

#include "dap_worker_reactor.h"
