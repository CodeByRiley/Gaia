#include "libm.h"
#include <math.h>

double lgamma(double x) { return __lgamma_r(x, &__signgam); }
