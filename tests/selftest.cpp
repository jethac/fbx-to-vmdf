/****************************************************************************
//	fbx-to-vmdf self-test: host-independence of the pieces the output
//	depends on (formatting, CRC, transcendental functions, layouts).
//	Every expected value is a bit pattern, so a pass on Linux, Windows and
//	macOS means those hosts agree.
*****************************************************************************/
#include "Common.h"
#include "DetMath.h"
#include "VmdfFormat.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
	if (!ok)
	{
		printf("FAIL: %s\n", what);
		g_failures++;
	}
}

static uint64_t bits(double d)
{
	uint64_t u;
	memcpy(&u, &d, 8);
	return u;
}

static uint32_t bitsf(float f)
{
	uint32_t u;
	memcpy(&u, &f, 4);
	return u;
}

int main()
{
	// "%f"-style formatting (skeleton XML)
	check(formatFixed(0.0, 6) == "0.000000", "format 0");
	check(formatFixed(-0.0, 6) == "-0.000000", "format -0");
	check(formatFixed((double)0.235005f, 6) == "0.235005", "format 0.235005f");
	check(formatFixed((double)-0.268638f, 6) == "-0.268638", "format -0.268638f");
	check(formatFixed(1.0, 6) == "1.000000", "format 1");
	check(formatFixed(0.0000005, 6) == "0.000000", "format tie below (0.0000005 is just under half)");
	check(formatFixed(2.5, 0) == "2", "format ties to even");
	check(formatFixed(3.5, 0) == "4", "format ties to even (odd)");
	check(formatFixed(123456.789, 2) == "123456.79", "format 2 decimals");
	check(formatFixed(1e20, 6) == "100000000000000000000.000000", "format large");
	check(formatFixed(-1e-9, 6) == "-0.000000", "format tiny negative");

	// CRC32 (engine's utl::CRC32)
	check(crc32("123456789") == 0xCBF43926u, "crc32 check value");

	// deterministic transcendental functions: exact values, and bit
	// patterns that any IEEE host must reproduce
	check(bits(det::sin(0.0)) == 0, "sin(0)");
	check(bits(det::sin(-0.0)) == 0x8000000000000000ull, "sin(-0)");
	check(det::cos(0.0) == 1.0, "cos(0)");
	check(bits(det::asin(-0.0)) == 0x8000000000000000ull, "asin(-0)");
	check(bits(det::atan2(0.0, 1.0)) == 0, "atan2(0,1)");
	check(bits(det::atan2(-0.0, 1.0)) == 0x8000000000000000ull, "atan2(-0,1)");
	check(bits(det::atan2(1.0, 1.0)) == 0x3fe921fb54442d18ull, "atan2(1,1) = pi/4");
	check(bits(det::sin(1.0)) == 0x3feaed548f090ceeull, "sin(1)");
	check(bits(det::cos(1.0)) == 0x3fe14a280fb5068cull, "cos(1)");
	check(bits(det::sin(3.0)) == 0x3fc210386db6d55bull, "sin(3)");
	check(bits(det::asin(0.5)) == 0x3fe0c152382d7366ull, "asin(0.5)");
	check(bitsf(det::sinf(0.5f)) == 0x3ef57744u, "sinf(0.5)");
	check(bitsf(det::cosf(0.5f)) == 0x3f60a940u, "cosf(0.5)");

	// struct layouts are pinned at compile time; double-check a couple
	check(sizeof(vmdf::Material) == 6040, "Material size");
	check(sizeof(vmdf::Shape) == 868, "Shape size");

	// little-endian host
	uint32_t one = 1;
	unsigned char b0;
	memcpy(&b0, &one, 1);
	check(b0 == 1, "little-endian host");

	if (g_failures)
	{
		printf("%d failure(s)\n", g_failures);
		return 1;
	}
	printf("selftest: all checks passed\n");
	return 0;
}
