#include "libm.h"
#include <math.h>

float lgammaf(float x) { return __lgammaf_r(x, &__signgam); }
