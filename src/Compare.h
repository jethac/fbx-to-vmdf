/****************************************************************************
//	fbx-to-vmdf
//	Structural .vmdf inspection: --dump and --compare.
*****************************************************************************/
#ifndef FBX2VMDF_COMPARE_H
#define FBX2VMDF_COMPARE_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

enum class CompareVerdict
{
	Identical,        // byte for byte
	EquivalentMasked, // every defined byte equal; only don't-care bytes differ
	EquivalentGeometry, // as above, except vertex/index order (same triangles)
	Different,
	Unreadable
};

const char* VerdictName(CompareVerdict v);

// Compares `ours` against `reference` field by field and prints the
// differences (to `out`, at most `maxLines` detail lines).
CompareVerdict CompareVmdf(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& reference, FILE* out, int maxLines = 60);

// Prints the structure of a .vmdf.
bool DumpVmdf(const std::vector<uint8_t>& data, FILE* out, bool verbose);

// Bytes Ayataka never initialised (string tails, unused array slots, the
// adjacency stream, unused constant data...): 1 = defined, 0 = don't care.
bool DefinedByteMask(const std::vector<uint8_t>& data, std::vector<uint8_t>& mask);

#endif // FBX2VMDF_COMPARE_H
