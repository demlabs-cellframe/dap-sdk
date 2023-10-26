#define str(s) #s
#define xstr(s) str(s)

extern sphincsplus_params_t g_sphincsplus_params_current;
#include "sphincsplus_global.h"
#ifndef SPHINCSPLUS_FLEX
#include xstr(params/params-PARAMS.h)
#endif

