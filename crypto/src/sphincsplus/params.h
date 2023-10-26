#define str(s) #s
#define xstr(s) str(s)

#include "sphincsplus_global.h"
extern sphincsplus_params_t g_sphincsplus_params_current;
#ifndef SPHINCSPLUS_FLEX
#include xstr(params/params-PARAMS.h)
#endif

