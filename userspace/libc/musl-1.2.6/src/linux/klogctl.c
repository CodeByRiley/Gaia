#include "syscall.h"
#include <sys/klog.h>

int klogctl(int type, char *buf, int len) { return syscall(SYS_syslog, type, buf, len); }
