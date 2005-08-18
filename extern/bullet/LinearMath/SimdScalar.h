/*

Copyright (c) 2005 Gino van den Bergen / Erwin Coumans <www.erwincoumans.com>

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/


#ifndef SIMD___SCALAR_H
#define SIMD___SCALAR_H

#include <math.h>
#undef max



#include <cstdlib>
#include <cfloat>
#include <float.h>

#ifdef WIN32

#ifdef __MINGW32__
#define SIMD_FORCE_INLINE inline
#else
#define SIMD_FORCE_INLINE __forceinline
#endif //__MINGW32__

//#define ATTRIBUTE_ALIGNED16(a) __declspec(align(16)) a
#define ATTRIBUTE_ALIGNED16(a) a

#include <assert.h>
#define ASSERT assert
#else
#define SIMD_FORCE_INLINE inline
#define ATTRIBUTE_ALIGNED16(a) a
#ifndef assert
#include <assert.h>
#endif


#define ASSERT assert

#endif



typedef float    SimdScalar;

const SimdScalar  SIMD_2_PI         = 6.283185307179586232f;
const SimdScalar  SIMD_PI           = SIMD_2_PI * SimdScalar(0.5f);
const SimdScalar  SIMD_HALF_PI		 = SIMD_2_PI * SimdScalar(0.25f);
const SimdScalar  SIMD_RADS_PER_DEG = SIMD_2_PI / SimdScalar(360.0f);
const SimdScalar  SIMD_DEGS_PER_RAD = SimdScalar(360.0f) / SIMD_2_PI;
const SimdScalar  SIMD_EPSILON      = FLT_EPSILON;
const SimdScalar  SIMD_INFINITY     = FLT_MAX;

SIMD_FORCE_INLINE bool      SimdFuzzyZero(SimdScalar x) { return fabsf(x) < SIMD_EPSILON; }

SIMD_FORCE_INLINE bool	SimdEqual(SimdScalar a, SimdScalar eps) {
	return (((a) <= eps) && !((a) < -eps));
}
SIMD_FORCE_INLINE bool	SimdGreaterEqual (SimdScalar a, SimdScalar eps) {
	return (!((a) <= eps));
}

SIMD_FORCE_INLINE SimdScalar SimdCos(SimdScalar x) { return cosf(x); }
SIMD_FORCE_INLINE SimdScalar SimdSin(SimdScalar x) { return sinf(x); }
SIMD_FORCE_INLINE SimdScalar SimdTan(SimdScalar x) { return tanf(x); }
SIMD_FORCE_INLINE int       SimdSign(SimdScalar x) {
    return x < 0.0f ? -1 : x > 0.0f ? 1 : 0;
}
SIMD_FORCE_INLINE SimdScalar SimdAcos(SimdScalar x) { return acosf(x); }
SIMD_FORCE_INLINE SimdScalar SimdAsin(SimdScalar x) { return asinf(x); }
SIMD_FORCE_INLINE SimdScalar SimdAtan(SimdScalar x) { return atanf(x); }
SIMD_FORCE_INLINE SimdScalar SimdAtan2(SimdScalar x, SimdScalar y) { return atan2f(x, y); }

SIMD_FORCE_INLINE SimdScalar SimdRadians(SimdScalar x) { return x * SIMD_RADS_PER_DEG; }
SIMD_FORCE_INLINE SimdScalar SimdDegrees(SimdScalar x) { return x * SIMD_DEGS_PER_RAD; }

#endif
