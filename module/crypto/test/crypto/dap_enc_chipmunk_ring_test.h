/*
 * Authors:
 * [Developer Name] <email@demlabs.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run ChipmunkRing tests
 * @param a_times Number of test iterations (unused for now)
 * @return 0 on success, negative on failure
 */
int dap_enc_chipmunk_ring_tests_run(const int a_times);

#ifdef __cplusplus
}
#endif
