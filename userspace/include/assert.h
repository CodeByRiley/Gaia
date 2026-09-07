/* userspace/include/assert.h , assertion diagnostics for the legacy libc. */
#ifndef ASSERT_H
#define ASSERT_H

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __assert_fail(const char *expression, const char *file, int line)
    __attribute__((noreturn));
#define assert(x) ((x) ? (void)0 : __assert_fail(#x, __FILE__, __LINE__))
#endif

#endif
