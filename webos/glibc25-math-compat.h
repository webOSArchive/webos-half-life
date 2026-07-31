/* glibc25-math-compat.h -- force-included (-include) when compiling C++11 with
 * Linaro g++ 4.9 against the PDK's glibc-2.5 sysroot.
 *
 * libstdc++ 4.9's <cmath> was configured against a newer glibc and does
 * `using ::acoshl;` etc. unconditionally. The glibc-2.5 ARM math.h never
 * declares the long-double variants (ARM EABI: long double == double), so
 * <cmath> fails to compile. Declaring the prototypes satisfies the using-
 * declarations; nothing in the engine actually calls the *l variants, so no
 * link-time symbols are needed.
 */
#ifndef WEBOS_GLIBC25_MATH_COMPAT_H
#define WEBOS_GLIBC25_MATH_COMPAT_H
#ifdef __cplusplus
extern "C" {
#endif

extern long double acoshl(long double);
extern long double asinhl(long double);
extern long double atanhl(long double);
extern long double cbrtl(long double);
extern long double copysignl(long double, long double);
extern long double erfl(long double);
extern long double erfcl(long double);
extern long double exp2l(long double);
extern long double expm1l(long double);
extern long double fdiml(long double, long double);
extern long double fmal(long double, long double, long double);
extern long double fmaxl(long double, long double);
extern long double fminl(long double, long double);
extern long double hypotl(long double, long double);
extern int ilogbl(long double);
extern long double lgammal(long double);
extern long long int llrintl(long double);
extern long long int llroundl(long double);
extern long double log1pl(long double);
extern long double log2l(long double);
extern long double logbl(long double);
extern long int lrintl(long double);
extern long int lroundl(long double);
extern long double nanl(const char *);
extern long double nearbyintl(long double);
extern long double nextafterl(long double, long double);
extern long double nexttowardl(long double, long double);
extern long double remainderl(long double, long double);
extern long double remquol(long double, long double, int *);
extern long double rintl(long double);
extern long double roundl(long double);
extern long double scalblnl(long double, long int);
extern long double scalbnl(long double, int);
extern long double tgammal(long double);
extern long double truncl(long double);

/* C++11 <cstdlib> additions missing from glibc 2.5 */
extern int at_quick_exit(void (*)(void));
extern void quick_exit(int) __attribute__((__noreturn__));

#ifdef __cplusplus
}
#endif
#endif /* WEBOS_GLIBC25_MATH_COMPAT_H */
