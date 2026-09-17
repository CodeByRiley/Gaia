#ifndef ASSERT_H
#define ASSERT_H

#include <utilities/panic.h>

#ifdef NDEBUG

#define assert(expr) ((void)0)

#else

#define assert(expr)                                                           \
  do {                                                                         \
    if (!(expr))                                                               \
      panicf("Assertion failed: %s", #expr);                                   \
  } while (0)

#endif /* NDEBUG */

#endif /* ASSERT_H */
