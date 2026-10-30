#include "rand48.h"
#include <stdlib.h>
#include <string.h>

void lcong48(unsigned short p[7]) { memcpy(__seed48, p, sizeof __seed48); }
