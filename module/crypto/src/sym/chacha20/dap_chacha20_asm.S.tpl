{{! ================================================================ }}
{{! ChaCha20 Assembly — Shared Algorithm Framework                 }}
{{! ================================================================ }}
{{! This file defines all algorithmic constants for ChaCha20:       }}
{{!   - Sigma constants ("expand 32-byte k")                        }}
{{!   - Double-round iteration range (10 DR = 20 rounds)            }}
{{!   - Counter increment vector (8 lanes for 8-way parallel)       }}
{{!   - State layout (16 words: 4 sigma + 8 key + 1 ctr + 3 nonce) }}
{{! Architecture-specific code is in ARCH_IMPL (passed via CMake).  }}
{{!                                                                 }}
{{! Adding a new arch: create arch/<family>/chacha20_asm_<isa>.tpl  }}
{{! that uses the variables below.                                  }}
{{! ================================================================ }}

{{! === Double-round iteration range (10 double-rounds = 20 QRs) === }}
{{#set DR=0|1|2|3|4|5|6|7|8|9}}

{{! === ChaCha20 "expand 32-byte k" sigma constants === }}
{{#set SIGMA_0=0x61707865}}
{{#set SIGMA_1=0x3320646e}}
{{#set SIGMA_2=0x79622d32}}
{{#set SIGMA_3=0x6b206574}}

{{! === Counter increment vector (8 lanes for dual-block YMM) === }}
{{#set CTR_INC=0|1|2|3|4|5|6|7}}

{{! === State word layout indices === }}
{{#set STATE_WORDS=0|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15}}
{{#set KEY_WORDS=0|1|2|3|4|5|6|7}}

{{! === Include architecture-specific implementation === }}
{{#include ARCH_IMPL}}
