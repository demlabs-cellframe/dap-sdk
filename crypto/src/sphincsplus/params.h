#define str(s) #s
#define xstr(s) str(s)

#ifndef SPHINCSPLUS_FLEX
#include xstr(params/params-PARAMS.h)
#endif
#include "sphincsplus_params.h"

