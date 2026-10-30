#define _GNU_SOURCE
#include "pthread_impl.h"
#include <unistd.h>

pid_t gettid(void) { return __pthread_self()->tid; }
