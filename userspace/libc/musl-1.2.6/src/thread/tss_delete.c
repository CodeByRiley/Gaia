#include <pthread.h>
#include <threads.h>

void tss_delete(tss_t key) { __pthread_key_delete(key); }
