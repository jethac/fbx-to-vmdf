/****************************************************************************
//	fbx-to-vmdf
//	A small double-precision affine matrix that behaves like the FBX SDK's
//	FbxAMatrix in the ways Ayataka depends on: storage (Get(i, j) is
//	element j of basis vector i; row 3 holds the translation), product
//	order, MultT, the affine Inverse (cofactors, so the signs of zeros come
//	out the same), Transpose and the Euler XYZ decomposition GetR.
//	No Autodesk code is used; behaviour was matched against the SDK by
//	black-box tests (tools/sdkprobe).
*****************************************************************************/
#ifndef FBX2VMDF_FBXMATH_H
#define FBX2VMDF_FBXMATH_H

#include "DetMath.h"

namespace fbxm {

struct Vec4
{
	double v[4];
	Vec4() : v{ 0.0, 0.0, 0.0, 0.0 } {}
	Vec4(double x, double y, double z, double w = 0.0) : v{ x, y, z, w } {}
	double& operator[](int i) { return v[i]; }
	double operator[](int i) const { return v[i]; }
};

const double k180DivPi = 57.295779513082320876798154814105;
const double kPiDiv180 = 0.017453292519943295769236907684886;

struct AMatrix
{
	double d[4][4];

	AMatrix() { SetIdentity(); }
	void SetIdentity()
	{
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				d[i][j] = i == j ? 1.0 : 0.0;
	}
	double Get(int i, int j) const { return d[i][j]; }

	Vec4 GetT() const { return Vec4(d[3][0], d[3][1], d[3][2], 1.0); }
	void SetT(const Vec4& t) { d[3][0] = t[0]; d[3][1] = t[1]; d[3][2] = t[2]; d[3][3] = 1.0; }

	// Product with FBX semantics: (A * B) applied to a point applies B first.
	AMatrix operator*(const AMatrix& b) const
	{
		AMatrix r;
		for (int j = 0; j < 4; j++)
		{
			for (int row = 0; row < 4; row++)
			{
				r.d[j][row] = d[0][row] * b.d[j][0] + d[1][row] * b.d[j][1] + d[2][row] * b.d[j][2] + d[3][row] * b.d[j][3];
			}
		}
		return r;
	}

	Vec4 MultT(const Vec4& v) const
	{
		Vec4 r;
		for (int row = 0; row < 4; row++)
		{
			r[row] = d[0][row] * v[0] + d[1][row] * v[1] + d[2][row] * v[2] + d[3][row] * v[3];
		}
		return r;
	}

	AMatrix Transpose() const
	{
		AMatrix r;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				r.d[i][j] = d[j][i];
		return r;
	}

	AMatrix Inverse() const
	{
		// math matrix a[r][c] = d[c][r] for the upper 3x3
		auto a = [this](int r, int c) { return d[c][r]; };
		double c00 = a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1);
		double c01 = -(a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0));
		double c02 = a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0);
		double c10 = -(a(0, 1) * a(2, 2) - a(0, 2) * a(2, 1));
		double c11 = a(0, 0) * a(2, 2) - a(0, 2) * a(2, 0);
		double c12 = -(a(0, 0) * a(2, 1) - a(0, 1) * a(2, 0));
		double c20 = a(0, 1) * a(1, 2) - a(0, 2) * a(1, 1);
		double c21 = -(a(0, 0) * a(1, 2) - a(0, 2) * a(1, 0));
		double c22 = a(0, 0) * a(1, 1) - a(0, 1) * a(1, 0);
		double det = a(0, 0) * c00 + a(0, 1) * c01 + a(0, 2) * c02;
		AMatrix r;
		if (det == 0.0)
		{
			return r;
		}
		// inverse math matrix inv[i][j] = C[j][i] / det; stored transposed
		double inv[3][3] = { { c00 / det, c10 / det, c20 / det }, { c01 / det, c11 / det, c21 / det }, { c02 / det, c12 / det, c22 / det } };
		for (int rr = 0; rr < 3; rr++)
			for (int cc = 0; cc < 3; cc++)
				r.d[cc][rr] = inv[rr][cc];
		r.d[0][3] = r.d[1][3] = r.d[2][3] = 0.0;
		for (int rr = 0; rr < 3; rr++)
		{
			r.d[3][rr] = -(inv[rr][0] * d[3][0] + inv[rr][1] * d[3][1] + inv[rr][2] * d[3][2]);
		}
		r.d[3][3] = 1.0;
		return r;
	}

	Vec4 GetS() const
	{
		double s[3];
		for (int i = 0; i < 3; i++)
		{
			s[i] = det::sqrt(d[i][0] * d[i][0] + d[i][1] * d[i][1] + d[i][2] * d[i][2]);
		}
		double det3 = d[0][0] * (d[1][1] * d[2][2] - d[1][2] * d[2][1]) - d[0][1] * (d[1][0] * d[2][2] - d[1][2] * d[2][0]) + d[0][2] * (d[1][0] * d[2][1] - d[1][1] * d[2][0]);
		if (det3 < 0.0)
		{
			s[0] = -s[0]; s[1] = -s[1]; s[2] = -s[2];
		}
		return Vec4(s[0], s[1], s[2], 1.0);
	}

	// Euler angles in degrees for rotation order XYZ (R = Rz * Ry * Rx).
	Vec4 GetR() const
	{
		Vec4 s = GetS();
		double r00 = d[0][0] / s[0], r01 = d[0][1] / s[0], r02 = d[0][2] / s[0];
		double r12 = d[1][2] / s[1], r22 = d[2][2] / s[2];
		double r10 = d[1][0] / s[1], r11 = d[1][1] / s[1];
		double x, y, z;
		if (r02 < 1.0 && r02 > -1.0)
		{
			y = det::asin(-r02);
			x = det::atan2(r12, r22);
			z = det::atan2(r01, r00);
		}
		else
		{
			// gimbal lock: put everything in x
			y = r02 <= -1.0 ? 1.5707963267948965580 : -1.5707963267948965580;
			x = det::atan2(-r10 * (r02 <= -1.0 ? -1.0 : 1.0), r11);
			z = 0.0;
		}
		return Vec4(x * k180DivPi, y * k180DivPi, z * k180DivPi, 0.0);
	}

	// Rotation matrix for Euler XYZ degrees (R = Rz * Ry * Rx).
	static AMatrix Rotation(const Vec4& rdeg)
	{
		double rx = rdeg[0] * kPiDiv180, ry = rdeg[1] * kPiDiv180, rz = rdeg[2] * kPiDiv180;
		double cx = det::cos(rx), sx = det::sin(rx), cy = det::cos(ry), sy = det::sin(ry), cz = det::cos(rz), sz = det::sin(rz);
		AMatrix m;
		// basis vectors (columns of the math matrix)
		m.d[0][0] = cy * cz;  m.d[0][1] = cy * sz;  m.d[0][2] = -sy;
		m.d[1][0] = sx * sy * cz - cx * sz;  m.d[1][1] = sx * sy * sz + cx * cz;  m.d[1][2] = sx * cy;
		m.d[2][0] = cx * sy * cz + sx * sz;  m.d[2][1] = cx * sy * sz - sx * cz;  m.d[2][2] = cx * cy;
		return m;
	}
	static AMatrix Translation(const Vec4& t) { AMatrix m; m.SetT(t); return m; }
	static AMatrix Scaling(const Vec4& s) { AMatrix m; m.d[0][0] = s[0]; m.d[1][1] = s[1]; m.d[2][2] = s[2]; return m; }
	static AMatrix TRS(const Vec4& t, const Vec4& r, const Vec4& s)
	{
		AMatrix m = Rotation(r);
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				m.d[i][j] *= s[i];
		m.SetT(t);
		return m;
	}
};

// FbxVector4::Normalize: scale x, y, z by 1/length(xyz); w is left alone.
inline void Normalize(Vec4& v)
{
	double len = det::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len != 0.0)
	{
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
}

} // namespace fbxm

#endif // FBX2VMDF_FBXMATH_H
