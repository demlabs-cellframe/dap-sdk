#define str(s) #s
#define xstr(s) str(s)

#include "sphincsplus_params.h"
#ifndef SPHINCSPLUS_FLEX
#include xstr(params/params-PARAMS.h)
#endif

