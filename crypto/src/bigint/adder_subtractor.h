#ifndef ADDER_SUBTRACTOR_H
#define ADDER_SUBTRACTOR_H

#endif // ADDER_SUBTRACTOR_H
#include "circuit_formalism.h"

//the work will be to remove the authors notations when we understnad what they are for
struct dap_adder_subtractor{

    uint64_t pi_clk;
    uint64_t pi_rst;

    uint64_t pi_ctrl_ch_A;
    uint64_t pi_ctrl_ch_B;
    uint64_t pi_ctrl_valid_n;

    uint64_t pi_data;
    uint64_t pi_data_last;
    uint64_t pi_data_wr_en;

    uint64_t po_data_up;
    uint64_t po_data_lo;
    uint64_t po_data_last;
    uint64_t po_data_wr_en;
    uint64_t po_data_all_ones;
    uint64_t po_data_zero;

};
typedef struct dap_adder_subtractor dap_adder_subtractor_t;

typedef struct dap_slice_a {
    struct {
        union {
                struct {
                bool carry_out:1;
                bool zero:1;
                bool all_ones:1;
            } DAP_ALIGN_PACKED;
        };
    } DAP_ALIGN_PACKED;
    uint64_t a;
    uint64_t b;
    uint64_t sum;
    uint64_t carry_out;
} DAP_ALIGN_PACKED dap_slice_a_t;


