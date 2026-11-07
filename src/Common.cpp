/****************************************************************************
//	fbx-to-vmdf
//	Shared helpers: diagnostics, portable paths and file I/O, locale-free
//	number parsing/formatting, CRC32.
*****************************************************************************/
#include "Common.h"

#include <cstdarg>
#include <cerrno>
#include <cmath>

[[noreturn]] void fatal(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "fbx-to-vmdf: error: ");
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

void warning(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "fbx-to-vmdf: warning: ");
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
}

std::string toForwardSlashes(const std::string& path)
{
	std::string out = path;
	for (char& c : out)
	{
		if (c == '\\')
		{
			c = '/';
		}
	}
	return out;
}

std::string pathBasename(const std::string& path)
{
	size_t pos = path.find_last_of("/\\");
	return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string pathStem(const std::string& path)
{
	std::string base = pathBasename(path);
	size_t dot = base.find_last_of('.');
	return dot == std::string::npos ? base : base.substr(0, dot);
}

std::string pathWithoutExtension(const std::string& path)
{
	size_t sep = path.find_last_of("/\\");
	size_t dot = path.find_last_of('.');
	if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
	{
		return path;
	}
	return path.substr(0, dot);
}

std::string pathDirname(const std::string& path)
{
	size_t pos = path.find_last_of("/\\");
	return pos == std::string::npos ? std::string() : path.substr(0, pos);
}

bool fileExists(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
	{
		return false;
	}
	fclose(f);
	return true;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
	{
		return false;
	}
	out.clear();
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
	{
		out.insert(out.end(), buf, buf + n);
	}
	fclose(f);
	return true;
}

bool writeFile(const std::string& path, const void* data, size_t size)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
	{
		return false;
	}
	bool ok = size == 0 || fwrite(data, size, 1, f) == 1;
	ok = (fclose(f) == 0) && ok;
	return ok;
}

float parseFloat(const std::string& s, bool* ok)
{
	const char* begin = s.c_str();
	char* end = nullptr;
	errno = 0;
	float v = strtof(begin, &end);
	if (ok)
	{
		*ok = end != begin;
	}
	return v;
}

double parseDouble(const std::string& s, bool* ok)
{
	const char* begin = s.c_str();
	char* end = nullptr;
	double v = strtod(begin, &end);
	if (ok)
	{
		*ok = end != begin;
	}
	return v;
}

// ---------------------------------------------------------------------------
// Exact fixed-point formatting with a tiny little-endian base-2^32 bignum.
// ---------------------------------------------------------------------------
namespace {
struct BigUInt
{
	std::vector<uint32_t> w; // little-endian limbs

	explicit BigUInt(uint64_t v = 0)
	{
		while (v)
		{
			w.push_back((uint32_t)v);
			v >>= 32;
		}
	}
	bool isZero() const { return w.empty(); }
	void trim()
	{
		while (!w.empty() && w.back() == 0)
		{
			w.pop_back();
		}
	}
	void mulSmall(uint32_t m)
	{
		uint64_t carry = 0;
		for (uint32_t& limb : w)
		{
			uint64_t v = (uint64_t)limb * m + carry;
			limb = (uint32_t)v;
			carry = v >> 32;
		}
		if (carry)
		{
			w.push_back((uint32_t)carry);
		}
	}
	void addSmall(uint32_t a)
	{
		uint64_t carry = a;
		for (size_t i = 0; i < w.size() && carry; i++)
		{
			uint64_t v = (uint64_t)w[i] + carry;
			w[i] = (uint32_t)v;
			carry = v >> 32;
		}
		if (carry)
		{
			w.push_back((uint32_t)carry);
		}
	}
	void shiftLeft(unsigned bits)
	{
		if (isZero())
		{
			return;
		}
		unsigned limbs = bits / 32, rem = bits % 32;
		if (rem)
		{
			uint32_t carry = 0;
			for (uint32_t& limb : w)
			{
				uint32_t next = limb >> (32 - rem);
				limb = (limb << rem) | carry;
				carry = next;
			}
			if (carry)
			{
				w.push_back(carry);
			}
		}
		w.insert(w.begin(), limbs, 0u);
	}
	// Bit `i` of the value.
	bool bit(unsigned i) const
	{
		unsigned limb = i / 32;
		return limb < w.size() && ((w[limb] >> (i % 32)) & 1u);
	}
	// true if any bit below `bits` is set
	bool anyBelow(unsigned bits) const
	{
		for (unsigned i = 0; i < bits / 32 && i < w.size(); i++)
		{
			if (w[i])
			{
				return true;
			}
		}
		unsigned limb = bits / 32, rem = bits % 32;
		return rem && limb < w.size() && (w[limb] & ((1u << rem) - 1u));
	}
	void shiftRight(unsigned bits)
	{
		unsigned limbs = bits / 32, rem = bits % 32;
		if (limbs >= w.size())
		{
			w.clear();
			return;
		}
		w.erase(w.begin(), w.begin() + limbs);
		if (rem)
		{
			for (size_t i = 0; i < w.size(); i++)
			{
				uint32_t hi = i + 1 < w.size() ? w[i + 1] : 0;
				w[i] = (w[i] >> rem) | (hi << (32 - rem));
			}
		}
		trim();
	}
	uint32_t divSmall(uint32_t d)
	{
		uint64_t rem = 0;
		for (size_t i = w.size(); i-- > 0;)
		{
			uint64_t cur = (rem << 32) | w[i];
			w[i] = (uint32_t)(cur / d);
			rem = cur % d;
		}
		trim();
		return (uint32_t)rem;
	}
	std::string toDecimal() const
	{
		if (isZero())
		{
			return "0";
		}
		BigUInt t = *this;
		std::string s;
		while (!t.isZero())
		{
			s.push_back((char)('0' + t.divSmall(10)));
		}
		return std::string(s.rbegin(), s.rend());
	}
};
} // namespace

std::string formatFixed(double value, int decimals)
{
	if (std::isnan(value))
	{
		return std::signbit(value) ? "-nan" : "nan";
	}
	if (std::isinf(value))
	{
		return value < 0 ? "-inf" : "inf";
	}
	bool negative = std::signbit(value);
	double a = std::fabs(value);
	int exp2 = 0;
	double frac = std::frexp(a, &exp2); // a = frac * 2^exp2, frac in [0.5, 1)
	uint64_t mant = (uint64_t)std::ldexp(frac, 53);  // exact: 53-bit integer
	int e = exp2 - 53;                               // a = mant * 2^e
	BigUInt n(mant);
	for (int i = 0; i < decimals; i++)
	{
		n.mulSmall(10);
	}
	if (e >= 0)
	{
		n.shiftLeft((unsigned)e);
	}
	else
	{
		unsigned k = (unsigned)(-e);
		bool half = n.bit(k - 1);
		bool sticky = k >= 2 && n.anyBelow(k - 1);
		n.shiftRight(k);
		bool odd = n.bit(0);
		if (half && (sticky || odd))
		{
			n.addSmall(1);
		}
	}
	std::string digits = n.toDecimal();
	if ((int)digits.size() <= decimals)
	{
		digits.insert(0, (size_t)(decimals + 1 - (int)digits.size()), '0');
	}
	std::string out;
	if (negative)
	{
		out.push_back('-');
	}
	out += digits.substr(0, digits.size() - (size_t)decimals);
	if (decimals > 0)
	{
		out.push_back('.');
		out += digits.substr(digits.size() - (size_t)decimals);
	}
	return out;
}

uint32_t crc32(const void* data, size_t size)
{
	static uint32_t table[256];
	static bool init = false;
	if (!init)
	{
		for (uint32_t i = 0; i < 256; ++i)
		{
			uint32_t c = i;
			for (int j = 0; j < 8; ++j)
			{
				c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
			}
			table[i] = c;
		}
		init = true;
	}
	uint32_t crc = ~0u;
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < size; i++)
	{
		crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	}
	return ~crc;
}

uint32_t crc32(const char* str)
{
	return crc32(str, strlen(str));
}
