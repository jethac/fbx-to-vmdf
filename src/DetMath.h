/****************************************************************************
//	fbx-to-vmdf
//	Host-independent transcendental functions. The output must not depend
//	on which C library computed a sine, so these are the FreeBSD msun
//	(fdlibm) double-precision algorithms, which use only IEEE basic
//	operations and sqrt, and therefore give bit-identical results on every
//	conforming host. The float variants round the double result once.
*****************************************************************************/
#ifndef FBX2VMDF_DETMATH_H
#define FBX2VMDF_DETMATH_H

namespace det {

double sin(double x);
double cos(double x);
double atan(double x);
double atan2(double y, double x);
double asin(double x);
double sqrt(double x);   // IEEE correctly rounded on every host

float sinf(float x);
float cosf(float x);
float sqrtf(float x);    // IEEE correctly rounded on every host

} // namespace det

#endif // FBX2VMDF_DETMATH_H
