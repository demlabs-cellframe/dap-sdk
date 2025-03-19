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
