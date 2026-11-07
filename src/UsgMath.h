/****************************************************************************
//	fbx-to-vmdf
//	The handful of usg maths routines Ayataka uses, transcribed operation
//	for operation from the Usagi Engine (Engine/Maths/Matrix4x4.cpp,
//	_vulkan/Matrix4x4_ps.cpp, Vector3f.h, Vector4f.h; Copyright (c) Vitei,
//	Inc. 2013, MIT licence) so that float results match bit for bit.
//	sinf/cosf come from DetMath so that no host libm is involved.
*****************************************************************************/
#ifndef FBX2VMDF_USGMATH_H
#define FBX2VMDF_USGMATH_H

#include "DetMath.h"
#include "VmdfFormat.h"

#include <cstring>

namespace usgm {

using vmdf::Matrix4x4;
using vmdf::Vector2f;
using vmdf::Vector3f;
using vmdf::Vector4f;

const float pi = 3.14159265358979323846f;
const float pi_over_2 = 1.5707963267f;

inline float DegreesToRadians(float deg) { return deg * (pi / 180.0f); }

inline Vector2f V2(float x, float y) { Vector2f v = { x, y }; return v; }
inline Vector3f V3(float x, float y, float z) { Vector3f v = { x, y, z }; return v; }
inline Vector4f V4(float x, float y, float z, float w) { Vector4f v = { x, y, z, w }; return v; }

inline Vector3f operator+(const Vector3f& a, const Vector3f& b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vector3f operator-(const Vector3f& a, const Vector3f& b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vector3f operator*(const Vector3f& a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
inline bool operator==(const Vector3f& a, const Vector3f& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline float MagnitudeSquared(const Vector3f& v) { return (v.x * v.x) + (v.y * v.y) + (v.z * v.z); }
inline float Magnitude(const Vector3f& v) { return det::sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z)); }
inline void Normalise(Vector3f& v)
{
	float fMagSq = MagnitudeSquared(v);
	if (fMagSq > 0.0f)
	{
		float length = det::sqrtf(fMagSq);
		v.x /= length;
		v.y /= length;
		v.z /= length;
	}
}
inline Vector3f GetNormalised(const Vector3f& v) { Vector3f o = v; Normalise(o); return o; }

inline void LoadIdentity(Matrix4x4& m)
{
	memset(&m, 0, sizeof(m));
	m.M[0][0] = m.M[1][1] = m.M[2][2] = m.M[3][3] = 1.0f;
}
inline Matrix4x4 Identity() { Matrix4x4 m; LoadIdentity(m); return m; }

inline void MakeScale(Matrix4x4& m, const Vector3f& v)
{
	LoadIdentity(m);
	m.M[0][0] = v.x;
	m.M[1][1] = v.y;
	m.M[2][2] = v.z;
}

inline void MakeRotate(Matrix4x4& m, float x, float y, float z)
{
	float cx = det::cosf(x);
	float sx = det::sinf(x);
	float cy = det::cosf(y);
	float sy = det::sinf(y);
	float cz = det::cosf(z);
	float sz = det::sinf(z);

	m.M[0][0] = cy * cz;
	m.M[0][1] = cy * sz;
	m.M[0][2] = -sy;
	m.M[0][3] = 0.0f;

	m.M[1][0] = (sx * sy * cz) - (cx * sz);
	m.M[1][1] = (sx * sy * sz) + (cx * cz);
	m.M[1][2] = sx * cy;
	m.M[1][3] = 0.0f;

	m.M[2][0] = (cx * sy * cz) + (sx * sz);
	m.M[2][1] = (cx * sy * sz) - (sx * cz);
	m.M[2][2] = cx * cy;
	m.M[2][3] = 0.0f;

	m.M[3][0] = 0.0f;
	m.M[3][1] = 0.0f;
	m.M[3][2] = 0.0f;
	m.M[3][3] = 1.0f;
}

inline void MakeRotAboutAxis(Matrix4x4& m, const Vector4f& axis, float fRadians)
{
	float fCos = det::cosf(fRadians);
	float fSin = det::sinf(fRadians);
	float fSum = 1.0f - fCos;

	m.M[0][0] = (axis.x * axis.x) * fSum + fCos;
	m.M[0][1] = (axis.x * axis.y) * fSum - (axis.z * fSin);
	m.M[0][2] = (axis.x * axis.z) * fSum + (axis.y * fSin);

	m.M[1][0] = (axis.y * axis.x) * fSum + (axis.z * fSin);
	m.M[1][1] = (axis.y * axis.y) * fSum + fCos;
	m.M[1][2] = (axis.y * axis.z) * fSum - (axis.x * fSin);

	m.M[2][0] = (axis.z * axis.x) * fSum - (axis.y * fSin);
	m.M[2][1] = (axis.z * axis.y) * fSum + (axis.x * fSin);
	m.M[2][2] = (axis.z * axis.z) * fSum + fCos;

	m.M[3][3] = 1.0f;
	m.M[3][0] = m.M[3][1] = m.M[3][2] = m.M[0][3] = m.M[1][3] = m.M[2][3] = 0.0f;
}

inline Matrix4x4 Mul(const Matrix4x4& a, const Matrix4x4& b)
{
	Matrix4x4 t;
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			t.M[i][j] = (a.M[i][0] * b.M[0][j]) + (a.M[i][1] * b.M[1][j]) + (a.M[i][2] * b.M[2][j]) + (a.M[i][3] * b.M[3][j]);
		}
	}
	return t;
}

inline void SetTranslation(Matrix4x4& m, const Vector3f& v)
{
	m.M[3][0] = v.x;
	m.M[3][1] = v.y;
	m.M[3][2] = v.z;
}

inline Vector3f TransformVec3(const Matrix4x4& m, const Vector3f& v, float fW = 1.0f)
{
	return V3(v.x * m.M[0][0] + v.y * m.M[1][0] + v.z * m.M[2][0] + fW * m.M[3][0],
	          v.x * m.M[0][1] + v.y * m.M[1][1] + v.z * m.M[2][1] + fW * m.M[3][1],
	          v.x * m.M[0][2] + v.y * m.M[1][2] + v.z * m.M[2][2] + fW * m.M[3][2]);
}

inline Vector4f CrossProduct4D(const Vector4f& vec1, const Vector4f& vec2, const Vector4f& vec3)
{
	Vector4f r;
	r.x = vec1.y * (vec2.z * vec3.w - vec3.z * vec2.w) - vec1.z * (vec2.y * vec3.w - vec3.y * vec2.w) + vec1.w * (vec2.y * vec3.z - vec2.z * vec3.y);
	r.y = -(vec1.x * (vec2.z * vec3.w - vec3.z * vec2.w) - vec1.z * (vec2.x * vec3.w - vec3.x * vec2.w) + vec1.w * (vec2.x * vec3.z - vec3.x * vec2.z));
	r.z = vec1.x * (vec2.y * vec3.w - vec3.y * vec2.w) - vec1.y * (vec2.x * vec3.w - vec3.x * vec2.w) + vec1.w * (vec2.x * vec3.y - vec3.x * vec2.y);
	r.w = -(vec1.x * (vec2.y * vec3.z - vec3.y * vec2.z) - vec1.y * (vec2.x * vec3.z - vec3.x * vec2.z) + vec1.z * (vec2.x * vec3.y - vec3.x * vec2.y));
	return r;
}

inline float Determinant(const Matrix4x4& m)
{
	Vector4f v1 = V4(m.M[0][0], m.M[1][0], m.M[2][0], m.M[3][0]);
	Vector4f v2 = V4(m.M[0][1], m.M[1][1], m.M[2][1], m.M[3][1]);
	Vector4f v3 = V4(m.M[0][2], m.M[1][2], m.M[2][2], m.M[3][2]);
	Vector4f minor = CrossProduct4D(v1, v2, v3);
	return -(m.M[0][3] * minor.x + m.M[1][3] * minor.y + m.M[2][3] * minor.z + m.M[3][3] * minor.w);
}

// usg::Matrix4x4::GetInverse (leaves `out` untouched when singular)
inline void GetInverse(const Matrix4x4& in, Matrix4x4& out)
{
	const float* m = &in.M[0][0];
	float* o = &out.M[0][0];
	Vector4f v, vec[3];
	float fDet = Determinant(in);
	if (fDet == 0.0f)
	{
		return;
	}
	for (uint32_t i = 0; i < 4; i++)
	{
		for (uint32_t j = 0; j < 4; j++)
		{
			if (j != i)
			{
				uint32_t a = j;
				if (j > i)
				{
					a = a - 1;
				}
				vec[a].x = m[(j * 4) + 0];
				vec[a].y = m[(j * 4) + 1];
				vec[a].z = m[(j * 4) + 2];
				vec[a].w = m[(j * 4) + 3];
			}
		}
		v = CrossProduct4D(vec[0], vec[1], vec[2]);
		// powf(-1.0f, (float)i) is exactly +-1
		float sign = (i & 1) ? -1.0f : 1.0f;
		for (uint32_t j = 0; j < 4; j++)
		{
			float fCofactor = j == 0 ? v.x : j == 1 ? v.y : j == 2 ? v.z : v.w;
			o[(j * 4) + i] = sign * fCofactor / fDet;
		}
	}
}

// LoaderUtil::setupTransformMatrix
inline void SetupTransformMatrix(Matrix4x4& out, const Vector3f& scale, const Vector3f& rotate, const Vector3f& translate)
{
	Matrix4x4 scaleMtx = Identity();
	MakeScale(scaleMtx, scale);
	Matrix4x4 rotateMtx = Identity();
	MakeRotate(rotateMtx, rotate.x, rotate.y, rotate.z);
	out = Mul(scaleMtx, rotateMtx);
	SetTranslation(out, translate);
}

} // namespace usgm

#endif // FBX2VMDF_USGMATH_H
