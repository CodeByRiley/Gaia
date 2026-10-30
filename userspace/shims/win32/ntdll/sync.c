#include <shims/win32/windows.h>
#include <shims/win32/handles.h>
#include <lib/services.h>
void *WINAPI RtlAllocateHeap(HANDLE,ULONG,SIZE_T);
BOOL WINAPI RtlFreeHeap(HANDLE,ULONG,void *);
struct event {unsigned signaled,manual;};
HANDLE WINAPI CreateEventA(void *security,BOOL manual,BOOL initial,const char *name){
 if(security||name){SetLastError(120);return 0;}
 struct event *e=RtlAllocateHeap((HANDLE)1,8,sizeof(*e));if(!e)return 0;e->manual=!!manual;e->signaled=!!initial;
 HANDLE h=GaiaHandleCreate(GH_EVENT,(int64_t)(uintptr_t)e,0);if(h==INVALID_HANDLE_VALUE){RtlFreeHeap((HANDLE)1,0,e);return 0;}return h;
}
HANDLE WINAPI CreateEventW(void *s,BOOL m,BOOL i,const WCHAR *n){if(n){SetLastError(120);return 0;}return CreateEventA(s,m,i,0);}
BOOL WINAPI SetEvent(HANDLE h){int64_t value;if(!GaiaHandleLookup(h,GH_EVENT,&value,0))return FALSE;struct event *e=(void *)(uintptr_t)value;__atomic_store_n(&e->signaled,1,__ATOMIC_RELEASE);return TRUE;}
BOOL WINAPI ResetEvent(HANDLE h){int64_t value;if(!GaiaHandleLookup(h,GH_EVENT,&value,0))return FALSE;struct event *e=(void *)(uintptr_t)value;__atomic_store_n(&e->signaled,0,__ATOMIC_RELEASE);return TRUE;}
DWORD GaiaWaitEvent(HANDLE h,DWORD timeout){int64_t value;if(!GaiaHandleLookup(h,GH_EVENT,&value,0))return WAIT_FAILED;struct event *e=(void *)(uintptr_t)value;uint64_t start,now;gaia_time(&start);
 for(;;){if(e->manual){if(__atomic_load_n(&e->signaled,__ATOMIC_ACQUIRE))return WAIT_OBJECT_0;}else{unsigned expected=1;if(__atomic_compare_exchange_n(&e->signaled,&expected,0,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return WAIT_OBJECT_0;}
 gaia_time(&now);if(timeout!=INFINITE&&(now-start)/1000000>=timeout)return WAIT_TIMEOUT;gaia_sleep(1000000);}
}
void WINAPI InitializeCriticalSection(CRITICAL_SECTION *c){*c=(CRITICAL_SECTION){0};}
void WINAPI DeleteCriticalSection(CRITICAL_SECTION *c){(void)c;}
BOOL WINAPI TryEnterCriticalSection(CRITICAL_SECTION *c){HANDLE me=(HANDLE)(uintptr_t)gaia_tid();if(c->OwningThread==me){c->RecursionCount++;return TRUE;}LONG expected=0;if(!__atomic_compare_exchange_n(&c->LockCount,&expected,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return FALSE;c->OwningThread=me;c->RecursionCount=1;return TRUE;}
void WINAPI EnterCriticalSection(CRITICAL_SECTION *c){while(!TryEnterCriticalSection(c))gaia_yield();}
void WINAPI LeaveCriticalSection(CRITICAL_SECTION *c){if(c->OwningThread!=(HANDLE)(uintptr_t)gaia_tid())return;if(--c->RecursionCount)return;c->OwningThread=0;__atomic_store_n(&c->LockCount,0,__ATOMIC_RELEASE);}
