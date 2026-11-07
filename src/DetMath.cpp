/****************************************************************************
//	fbx-to-vmdf
//	Deterministic sin/cos/atan/atan2/asin, transcribed from FreeBSD msun:
//
//	 ====================================================
//	 Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
//	 Developed at SunPro, a Sun Microsystems, Inc. business.
//	 Permission to use, copy, modify, and distribute this
//	 software is freely granted, provided that this notice
//	 is preserved.
//	 ====================================================
//
//	Argument reduction handles |x| up to about 2^20 * pi/2 (Cody-Waite with
//	three-part pi/2), which covers every angle a model file can sensibly
//	hold; larger arguments are reduced by fmod first, which is exact.
*****************************************************************************/
#include "DetMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace det {
namespace {

inline uint32_t hi(double x) { uint64_t u; memcpy(&u, &x, 8); return (uint32_t)(u >> 32); }
inline uint32_t lo(double x) { uint64_t u; memcpy(&u, &x, 8); return (uint32_t)u; }
inline double withLo(double x, uint32_t l)
{
	uint64_t u; memcpy(&u, &x, 8);
	u = (u & 0xFFFFFFFF00000000ull) | l;
	memcpy(&x, &u, 8);
	return x;
}

const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
             S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;

double kernel_sin(double x, double y, int iy)
{
	double z = x * x;
	double w = z * z;
	double r = S2 + z * (S3 + z * S4) + z * w * (S5 + z * S6);
	double v = z * x;
	if (iy == 0)
	{
		return x + v * (S1 + z * r);
	}
	return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
             C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;

double kernel_cos(double x, double y)
{
	double z = x * x;
	double w = z * z;
	double r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
	double hz = 0.5 * z;
	w = 1.0 - hz;
	return w + (((1.0 - w) - hz) + (z * r - x * y));
}

const double invpio2 = 6.36619772367581382433e-01;
const double pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11;
const double pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21;
const double pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;

inline double rnint(double x)
{
	const double toint = 6755399441055744.0; // 1.5 * 2^52
	double t = x + toint;
	return t - toint;
}

// Cody-Waite reduction of x by pi/2 (FreeBSD e_rem_pio2.c medium path).
int rem_pio2(double x, double* y)
{
	uint32_t ix = hi(x) & 0x7fffffff;
	double fn = rnint(x * invpio2);
	int n = (int)fn;
	double r = x - fn * pio2_1;
	double w = fn * pio2_1t;
	int j = (int)(ix >> 20);
	y[0] = r - w;
	int i = j - (int)((hi(y[0]) >> 20) & 0x7ff);
	if (i > 16)
	{
		double t = r;
		w = fn * pio2_2;
		r = t - w;
		w = fn * pio2_2t - ((t - r) - w);
		y[0] = r - w;
		i = j - (int)((hi(y[0]) >> 20) & 0x7ff);
		if (i > 49)
		{
			t = r;
			w = fn * pio2_3;
			r = t - w;
			w = fn * pio2_3t - ((t - r) - w);
			y[0] = r - w;
		}
	}
	y[1] = (r - y[0]) - w;
	return n;
}

double reduceLarge(double x)
{
	// fmod is exact; 2*pi as a double is not, but beyond 2^20*pi/2 the
	// result is meaningless anyway, it only has to be deterministic.
	return std::fmod(x, 6.28318530717958647692);
}

const double atanhi[] = { 4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00 };
const double atanlo[] = { 2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17 };
const double aT[] = {
	3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01, -1.11111104054623557880e-01,
	9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02, -5.83357013379057348645e-02,
	4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02 };

const double pio2_hi = 1.57079632679489655800e+00, pio2_lo = 6.12323399573676603587e-17, pio4_hi = 7.85398163397448278999e-01;
const double pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01, pS2 = 2.01212532134862925881e-01,
             pS3 = -4.00555345006794114027e-02, pS4 = 7.91534994289814532176e-04, pS5 = 3.47933107596021167570e-05;
const double qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00, qS3 = -6.88283971605453293030e-01,
             qS4 = 7.70381505559019352791e-02;

} // namespace

double sin(double x)
{
	uint32_t ix = hi(x) & 0x7fffffff;
	if (ix <= 0x3fe921fb)
	{
		if (ix < 0x3e500000 && (int)x == 0)
		{
			return x;
		}
		return kernel_sin(x, 0.0, 0);
	}
	if (ix >= 0x7ff00000)
	{
		return x - x;
	}
	if (ix > 0x413921fb)
	{
		return det::sin(reduceLarge(x));
	}
	double y[2];
	int n = rem_pio2(x, y);
	switch (n & 3)
	{
	case 0: return kernel_sin(y[0], y[1], 1);
	case 1: return kernel_cos(y[0], y[1]);
	case 2: return -kernel_sin(y[0], y[1], 1);
	default: return -kernel_cos(y[0], y[1]);
	}
}

double cos(double x)
{
	uint32_t ix = hi(x) & 0x7fffffff;
	if (ix <= 0x3fe921fb)
	{
		if (ix < 0x3e46a09e && (int)x == 0)
		{
			return 1.0;
		}
		return kernel_cos(x, 0.0);
	}
	if (ix >= 0x7ff00000)
	{
		return x - x;
	}
	if (ix > 0x413921fb)
	{
		return det::cos(reduceLarge(x));
	}
	double y[2];
	int n = rem_pio2(x, y);
	switch (n & 3)
	{
	case 0: return kernel_cos(y[0], y[1]);
	case 1: return -kernel_sin(y[0], y[1], 1);
	case 2: return -kernel_cos(y[0], y[1]);
	default: return kernel_sin(y[0], y[1], 1);
	}
}

double atan(double x)
{
	uint32_t hx = hi(x);
	uint32_t ix = hx & 0x7fffffff;
	int id;
	if (ix >= 0x44100000)
	{
		if (ix > 0x7ff00000 || (ix == 0x7ff00000 && lo(x) != 0))
		{
			return x + x;
		}
		return (hx >> 31) ? -atanhi[3] - atanlo[3] : atanhi[3] + atanlo[3];
	}
	if (ix < 0x3fdc0000)
	{
		if (ix < 0x3e400000)
		{
			return x;
		}
		id = -1;
	}
	else
	{
		x = std::fabs(x);
		if (ix < 0x3ff30000)
		{
			if (ix < 0x3fe60000)
			{
				id = 0;
				x = (2.0 * x - 1.0) / (2.0 + x);
			}
			else
			{
				id = 1;
				x = (x - 1.0) / (x + 1.0);
			}
		}
		else
		{
			if (ix < 0x40038000)
			{
				id = 2;
				x = (x - 1.5) / (1.0 + 1.5 * x);
			}
			else
			{
				id = 3;
				x = -1.0 / x;
			}
		}
	}
	double z = x * x;
	double w = z * z;
	double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
	double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
	if (id < 0)
	{
		return x - x * (s1 + s2);
	}
	z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
	return (hx >> 31) ? -z : z;
}

double atan2(double y, double x)
{
	const double tiny = 1.0e-300, pi_o_4 = 7.8539816339744827900e-01, pi_o_2 = 1.5707963267948965580e+00,
	             pi = 3.1415926535897931160e+00, pi_lo = 1.2246467991473531772e-16;
	if (std::isnan(x) || std::isnan(y))
	{
		return x + y;
	}
	uint32_t hx = hi(x), hy = hi(y);
	uint32_t ix = hx & 0x7fffffff, iy = hy & 0x7fffffff;
	if (x == 1.0)
	{
		return atan(y);
	}
	int m = (int)(((hy >> 31) & 1) | ((hx >> 30) & 2));
	if (y == 0.0)
	{
		switch (m)
		{
		case 0:
		case 1: return y;
		case 2: return pi + tiny;
		default: return -pi - tiny;
		}
	}
	if (x == 0.0)
	{
		return (hy >> 31) ? -pi_o_2 - tiny : pi_o_2 + tiny;
	}
	if (ix == 0x7ff00000)
	{
		if (iy == 0x7ff00000)
		{
			switch (m)
			{
			case 0: return pi_o_4 + tiny;
			case 1: return -pi_o_4 - tiny;
			case 2: return 3.0 * pi_o_4 + tiny;
			default: return -3.0 * pi_o_4 - tiny;
			}
		}
		switch (m)
		{
		case 0: return 0.0;
		case 1: return -0.0;
		case 2: return pi + tiny;
		default: return -pi - tiny;
		}
	}
	if (iy == 0x7ff00000)
	{
		return (hy >> 31) ? -pi_o_2 - tiny : pi_o_2 + tiny;
	}
	int k = ((int)iy - (int)ix) >> 20;
	double z;
	if (k > 60)
	{
		z = pi_o_2 + 0.5 * pi_lo;
		m &= 1;
	}
	else if ((hx >> 31) && k < -60)
	{
		z = 0.0;
	}
	else
	{
		z = atan(std::fabs(y / x));
	}
	switch (m)
	{
	case 0: return z;
	case 1: return -z;
	case 2: return pi - (z - pi_lo);
	default: return (z - pi_lo) - pi;
	}
}

double asin(double x)
{
	uint32_t hx = hi(x);
	uint32_t ix = hx & 0x7fffffff;
	double t, w, p, q, c, r, s;
	if (ix >= 0x3ff00000)
	{
		if (((ix - 0x3ff00000) | lo(x)) == 0)
		{
			return x * pio2_hi + x * pio2_lo;
		}
		return (x - x) / (x - x);
	}
	if (ix < 0x3fe00000)
	{
		if (ix < 0x3e500000)
		{
			return x;
		}
		t = x * x;
		p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
		q = 1.0 + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
		w = p / q;
		return x + x * w;
	}
	w = 1.0 - std::fabs(x);
	t = w * 0.5;
	p = t * (pS0 + t * (pS1 + t * (pS2 + t * (pS3 + t * (pS4 + t * pS5)))));
	q = 1.0 + t * (qS1 + t * (qS2 + t * (qS3 + t * qS4)));
	s = std::sqrt(t);
	if (ix >= 0x3FEF3333)
	{
		w = p / q;
		t = pio2_hi - (2.0 * (s + s * w) - pio2_lo);
	}
	else
	{
		w = withLo(s, 0);
		c = (t - w * w) / (s + w);
		r = p / q;
		p = 2.0 * s * r - (pio2_lo - 2.0 * c);
		q = pio4_hi - 2.0 * w;
		t = pio4_hi - (p - q);
	}
	return (hx >> 31) ? -t : t;
}

double sqrt(double x) { return std::sqrt(x); }
float sqrtf(float x) { return std::sqrt(x); }
float sinf(float x) { return (float)det::sin((double)x); }
float cosf(float x) { return (float)det::cos((double)x); }

} // namespace det
