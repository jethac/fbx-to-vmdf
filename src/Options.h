/****************************************************************************
//	fbx-to-vmdf
//	Process-wide conversion options.
*****************************************************************************/
#ifndef FBX2VMDF_OPTIONS_H
#define FBX2VMDF_OPTIONS_H

#include <string>

struct Options
{
	// Reproduce Ayataka bugs that were fixed after the Windows reference
	// cook, to prove that everything else matches it (--ayataka-compat).
	bool legacyBounds = false;   // 513d3feb: shape bounds flipped in Z, not mirrored in X
	bool legacyUVIndex = false;  // 1d43dca8: every UV set read through the first set's index array

	// Directory holding <ShaderPak>.yml effect definitions. Empty means
	// Ayataka's search: $USAGI_DIR/Data/GLSL/effects, then the same path
	// under the directory above the engine.
	std::string effectsDir;

	bool verbose = false;
	bool timing = true;          // print the conversion time like Ayataka
};

extern Options g_options;

#endif // FBX2VMDF_OPTIONS_H
