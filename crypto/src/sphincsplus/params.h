#define str(s) #s
#define xstr(s) str(s)

#ifndef GLOBAL_PARAMS
#define GLOBAL_PARAMS
#include "sphincsplus_global.h"
extern sphincsplus_params_t g_sphincsplus_params_current;
#endif
#ifndef SPHINCSPLUS_FLEX
#include xstr(params/params-PARAMS.h)
#endif

