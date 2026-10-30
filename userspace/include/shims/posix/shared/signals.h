#pragma once
#include <lib/services.h>
/* Termination is shared; sigaction/signal-frame layouts stay platform-specific.
 */
int64_t shim_process_terminate(int64_t process);
