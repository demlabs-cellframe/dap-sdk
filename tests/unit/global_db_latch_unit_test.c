/**
 * @brief Storage-capacity latch semantics (MDBX_MAP_FULL fail-soft path).
 * No storage required: the latch is driver-agnostic state.
 */
#include "dap_test.h"
#include "dap_global_db_driver.h"

int main(void)
{
    dap_assert(!dap_global_db_driver_storage_full(), "Latch starts clear");
    dap_global_db_driver_storage_full_set();
    dap_assert(dap_global_db_driver_storage_full(), "Latch set by storage_full_set");
    dap_global_db_driver_storage_full_set(); // idempotent
    dap_assert(dap_global_db_driver_storage_full(), "Latch stays set (re-set is a no-op)");
    dap_test_msg("storage-capacity latch semantics OK");
    return 0;
}
