/****************************************************************************
//	fbx-to-vmdf
//	FBX scene -> exchange model. A port of Ayataka's fbx/FbxConverter.cpp
//	and fbx/FbxLoad.cpp (Usagi Engine, Copyright (c) Vitei, Inc.
//	2013-2018, MIT licence) from the FBX SDK onto fbx::Scene.
*****************************************************************************/
#ifndef FBX2VMDF_LOADER_H
#define FBX2VMDF_LOADER_H

#include "Exchange.h"

#include <string>

struct LoadResult
{
	bool ok = false;
	std::string error;
	bool skippedAnimation = false;
};

// Loads `path` into `cmdl`, logging dependencies (the .yml overrides and
// effect definitions). Mirrors FbxConverter::Load.
LoadResult LoadFbx(const std::string& path, bool bAsCollision, bool bSkeletonOnly, Cmdl& cmdl, DependencyTracker& deps);

// --probe: print the per-corner vertex elements in tools/sdkprobe's format
void SetProbeOutput(FILE* f);

#endif // FBX2VMDF_LOADER_H
