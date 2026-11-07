/****************************************************************************
//	fbx-to-vmdf
//	Small shared helpers. Parts adapted from Ayataka (Usagi Engine,
//	Copyright (c) Vitei, Inc. 2013-2018, MIT licence): common.h, StringUtil.h.
*****************************************************************************/
#ifndef FBX2VMDF_COMMON_H
#define FBX2VMDF_COMMON_H

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ----------------------------------------------------------------------
// Determinism guards. The output must be byte-identical on every host, so
// refuse configurations where float maths or memory layout would differ.
// ----------------------------------------------------------------------
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "fbx-to-vmdf writes the .vmdf in host byte order and only supports little-endian hosts");
#endif
#if defined(FLT_EVAL_METHOD)
static_assert(FLT_EVAL_METHOD == 0, "fbx-to-vmdf needs IEEE single/double evaluation (SSE2/NEON, no x87 excess precision)");
#endif
#if defined(__FAST_MATH__)
#error "fbx-to-vmdf must not be built with -ffast-math: its output depends on exact IEEE rounding"
#endif
static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE float/double required");

typedef uint8_t uint8;
typedef int8_t sint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int32_t sint32;
typedef uint64_t uint64;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static const uint32_t DEFAULT_ALIGNMENT = 16;
static const uint32_t USG_INVALID_ID = 0xFFFFFFFFu;

inline size_t calcAlignedSize(size_t size, size_t align)
{
	if (size & (align - 1))
	{
		size = (size & ~(align - 1)) + align;
	}
	return size;
}

inline void turnOnFlag(uint32_t& attr, uint32_t flag) { attr |= flag; }

// Fatal error: print and exit with a failure code (Ayataka's FATAL_RELEASE).
[[noreturn]] void fatal(const char* fmt, ...);
void warning(const char* fmt, ...);

// ----------------------------------------------------------------------
// Fixed-size string fields. Ayataka filled these with strcpy_s / strncpy /
// sprintf_s and left whatever was in memory after the terminator (stack or
// heap garbage on Windows, 0xFE on the Linux shim). fbx-to-vmdf always
// zero-fills the whole field so the output is deterministic.
// ----------------------------------------------------------------------
// strcpy_s semantics: on overflow the destination becomes "" (MSVC).
inline void setField(char* dst, size_t size, const char* src)
{
	size_t len = strlen(src);
	memset(dst, 0, size);
	if (len < size)
	{
		memcpy(dst, src, len);
	}
}
template <size_t N> inline void setField(char (&dst)[N], const char* src) { setField(dst, N, src); }
template <size_t N> inline void setField(char (&dst)[N], const std::string& src) { setField(dst, N, src.c_str()); }

// strncpy(dst, src, N - 1) semantics (Ayataka's STRING_COPY): truncate.
inline void setFieldTruncate(char* dst, size_t size, const char* src)
{
	memset(dst, 0, size);
	size_t len = strlen(src);
	if (len > size - 1)
	{
		len = size - 1;
	}
	memcpy(dst, src, len);
}
template <size_t N> inline void setFieldTruncate(char (&dst)[N], const char* src) { setFieldTruncate(dst, N, src); }

// ----------------------------------------------------------------------
// Paths: accept both separators on input, always write '/'.
// ----------------------------------------------------------------------
std::string toForwardSlashes(const std::string& path);
std::string pathBasename(const std::string& path);          // "a/b/c.ext" -> "c.ext"
std::string pathStem(const std::string& path);              // "a/b/c.ext" -> "c"
std::string pathWithoutExtension(const std::string& path);  // "a/b/c.ext" -> "a/b/c"
std::string pathDirname(const std::string& path);           // "a/b/c.ext" -> "a/b"
bool fileExists(const std::string& path);
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const void* data, size_t size);

inline bool startsWith(const std::string& s, const char* prefix)
{
	return s.compare(0, strlen(prefix), prefix) == 0;
}
inline bool endsWith(const std::string& s, const char* suffix)
{
	size_t n = strlen(suffix);
	return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Locale-independent decimal parsing (strtof/strtod honour LC_NUMERIC).
float parseFloat(const std::string& s, bool* ok = nullptr);
double parseDouble(const std::string& s, bool* ok = nullptr);

// Locale-independent, host-independent "%f"-style formatting of a float
// value (as promoted to double by printf) with `decimals` fraction digits,
// correctly rounded (ties to even), matching a conforming C library.
std::string formatFixed(double value, int decimals);

// CRC32 as used by the engine (utl::CRC32: reflected 0xEDB88320).
uint32_t crc32(const void* data, size_t size);
uint32_t crc32(const char* str);

#endif // FBX2VMDF_COMMON_H
