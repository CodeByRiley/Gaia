#include <utilities/limits.h>
#include <utilities/math.h>

#define TWO_PI 6.28318530717958647692
#define LN2 0.69314718055994530942
#define PI_4 0.78539816339744830962

#define EXP_OVERFLOW_LIMIT 709.782712893384
#define EXP_UNDERFLOW_LIMIT (-745.133219101941)

/*
 * Freestanding math helpers. These are small approximations, not a full IEEE
 * libm, but they never lower back into the public symbols below. That matters
 * in kernel code: a wrapper like floor() { return __builtin_floor(x); } can
 * compile into "call floor" and recurse until the boot stack is exhausted.
 */

double fabs(double x) {
  if (x < 0.0)
    return -x;

  /* Ensure fabs(-0.0) produces +0.0. */
  if (x == 0.0)
    return 0.0;

  return x;
}

double floor(double x) {
  if (x != x)
    return x;

  if (x >= F64_NO_FRACTION_LIMIT || x <= -F64_NO_FRACTION_LIMIT)
    return x;

  long long i = (long long)x;

  if ((double)i > x)
    i--;

  return (double)i;
}

double ceil(double x) {
  if (x != x)
    return x;

  if (x >= F64_NO_FRACTION_LIMIT || x <= -F64_NO_FRACTION_LIMIT)
    return x;

  long long i = (long long)x;

  if ((double)i < x)
    i++;

  return (double)i;
}

double sqrt(double x) {
  if (x <= 0.0)
    return 0.0;

  /* +infinity */
  if (x > F64_MAX)
    return x;

  double scale = 1.0;

  while (x >= 4.0) {
    x *= 0.25;
    scale *= 2.0;
  }

  while (x < 1.0) {
    x *= 4.0;
    scale *= 0.5;
  }

  double g = 0.5 * (1.0 + x);

  for (int i = 0; i < 8; i++)
    g = 0.5 * (g + x / g);

  return g * scale;
}

double fmod(double x, double y) {
  /* NaN propagation. */
  if (x != x || y != y)
    return x + y;

  if (y == 0.0)
    return 0.0;

  /* Infinite dividend is treated as invalid by this small libm. */
  if (x > F64_MAX || x < -F64_MAX)
    return 0.0;

  /* Finite value modulo infinity is the original value. */
  if (y > F64_MAX || y < -F64_MAX)
    return x;

  double ax = fabs(x);
  double ay = fabs(y);

  if (ax < ay)
    return x;

  if (ax == ay)
    return x < 0.0 ? -0.0 : 0.0;

  /*
   * Scale the divisor upward by powers of two, then subtract downward.
   * This avoids converting x / y to an integer and therefore works for
   * quotients larger than 64 bits.
   */
  double d = ay;

  while (d <= ax * 0.5)
    d *= 2.0;

  while (d >= ay) {
    if (ax >= d)
      ax -= d;

    d *= 0.5;
  }

  return x < 0.0 ? -ax : ax;
}

static double cbrt_newton(double x) {
  if (x == 0.0)
    return 0.0;

  if (x > F64_MAX || x < -F64_MAX)
    return x;

  double sign = x < 0.0 ? -1.0 : 1.0;
  double ax = fabs(x);
  double scale = 1.0;

  while (ax >= 8.0) {
    ax *= 0.125;
    scale *= 2.0;
  }

  while (ax < 1.0) {
    ax *= 8.0;
    scale *= 0.5;
  }

  double g = 1.0;

  for (int i = 0; i < 10; i++)
    g = (2.0 * g + ax / (g * g)) / 3.0;

  return sign * g * scale;
}

static double ipow(double base, long long expn) {
  unsigned long long e;
  double result = 1.0;

  if (expn < 0) {
    if (base == 0.0)
      return 0.0;

    base = 1.0 / base;

    /* Avoid signed overflow when expn == LLONG_MIN. */
    e = (unsigned long long)(-(expn + 1)) + 1;
  } else {
    e = (unsigned long long)expn;
  }

  while (e) {
    if (e & 1ULL)
      result *= base;

    base *= base;
    e >>= 1;
  }

  return result;
}

double log(double x) {
  if (x <= 0.0)
    return 0.0;

  if (x > F64_MAX)
    return x;

  int k = 0;

  while (x > 1.5) {
    x *= 0.5;
    k++;
  }

  while (x < 0.75) {
    x *= 2.0;
    k--;
  }

  double z = (x - 1.0) / (x + 1.0);
  double z2 = z * z;
  double term = z;
  double sum = 0.0;

  for (int n = 1; n <= 39; n += 2) {
    sum += term / (double)n;
    term *= z2;
  }

  return 2.0 * sum + (double)k * LN2;
}

double exp(double x) {
  if (x > EXP_OVERFLOW_LIMIT)
    return F64_MAX;

  if (x < EXP_UNDERFLOW_LIMIT)
    return 0.0;

  int k = 0;

  while (x > LN2) {
    x -= LN2;
    k++;
  }

  while (x < -LN2) {
    x += LN2;
    k--;
  }

  double term = 1.0;
  double sum = 1.0;

  for (int i = 1; i <= 24; i++) {
    term *= x / (double)i;
    sum += term;
  }

  while (k > 0) {
    sum *= 2.0;
    k--;
  }

  while (k < 0) {
    sum *= 0.5;
    k++;
  }

  return sum;
}

double pow(double base, double expn) {
  if (expn == 0.0)
    return 1.0;

  if (base == 0.0)
    return 0.0;

  /*
   * Handle exact integer powers separately. Check the conversion range
   * before casting to avoid undefined behaviour.
   */
  if (expn >= I64_DOUBLE_MIN && expn < I64_DOUBLE_LIMIT) {
    long long i = (long long)expn;

    if ((double)i == expn)
      return ipow(base, i);
  }

  /*
   * Permit the common real cube-root case for negative bases.
   * Other fractional powers of negative numbers are not supported.
   */
  if (fabs(expn - (1.0 / 3.0)) < 0.000001)
    return cbrt_newton(base);

  if (base < 0.0)
    return 0.0;

  return exp(expn * log(base));
}

static double reduce_angle(double x) {
  x = fmod(x, TWO_PI);

  if (x > M_PI)
    x -= TWO_PI;

  if (x < -M_PI)
    x += TWO_PI;

  return x;
}

double sin(double x) {
  x = reduce_angle(x);

  /*
   * Reflect into [-pi/2, pi/2]. This greatly improves the Taylor
   * approximation compared with evaluating it all the way out to +/-pi.
   */
  if (x > M_PI_2)
    x = M_PI - x;
  else if (x < -M_PI_2)
    x = -M_PI - x;

  double x2 = x * x;

  return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 - (x2 * x2 * x2) / 5040.0 +
              (x2 * x2 * x2 * x2) / 362880.0);
}

double cos(double x) {
  x = reduce_angle(x);

  double sign = 1.0;

  /*
   * Reflect into [-pi/2, pi/2] and account for the quadrant with sign.
   */
  if (x > M_PI_2) {
    x = M_PI - x;
    sign = -1.0;
  } else if (x < -M_PI_2) {
    x = -M_PI - x;
    sign = -1.0;
  }

  double x2 = x * x;

  return sign * (1.0 - x2 / 2.0 + (x2 * x2) / 24.0 - (x2 * x2 * x2) / 720.0 +
                 (x2 * x2 * x2 * x2) / 40320.0);
}

double tan(double x) {
  double c = cos(x);

  if (c == 0.0)
    return 0.0;

  return sin(x) / c;
}

static double atan_unit(double x) {
  double sign = x < 0.0 ? -1.0 : 1.0;
  x = fabs(x);

  if (x > 1.0)
    return sign * (M_PI_2 - atan_unit(1.0 / x));

  /*
   * Around x = 1 the ordinary atan Taylor series converges very slowly.
   * Transform values above tan(pi/8) closer to zero:
   *
   * atan(x) = pi/4 + atan((x - 1) / (x + 1))
   */
  if (x > 0.41421356237309504880) {
    double z = (x - 1.0) / (x + 1.0);
    return sign * (PI_4 + atan_unit(z));
  }

  double x2 = x * x;
  double term = x;
  double sum = x;
  int add = 0;

  for (int n = 3; n <= 31; n += 2) {
    term *= x2;

    if (add)
      sum += term / (double)n;
    else
      sum -= term / (double)n;

    add = !add;
  }

  return sign * sum;
}

double atan2(double y, double x) {
  if (x > 0.0)
    return atan_unit(y / x);

  if (x < 0.0 && y >= 0.0)
    return atan_unit(y / x) + M_PI;

  if (x < 0.0 && y < 0.0)
    return atan_unit(y / x) - M_PI;

  if (y > 0.0)
    return M_PI_2;

  if (y < 0.0)
    return -M_PI_2;

  return 0.0;
}

double asin(double x) {
  if (x > 1.0)
    x = 1.0;

  if (x < -1.0)
    x = -1.0;

  return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) { return M_PI_2 - asin(x); }

double max(double x, double y) {
	if (x > y)
		return x;
	else
		return y;
}
double min(double x, double y) {
	if (x < y)
		return x;
	else
		return y;
}
double round(double x) {
	if (x >= 0.0)
		return floor(x + 0.5);
	else
		return ceil(x - 0.5);
}
