/****************************************************************************
//	fbx-to-vmdf
//	.vmdf binary store and skeleton/hierarchy XML export.
//	Adapted from Ayataka's cmdl/CmdlBinaryStore.cpp and common/
//	ModelConverterBase.cpp (Usagi Engine, Copyright (c) Vitei, Inc.
//	2013-2018, MIT licence).
*****************************************************************************/
#ifndef FBX2VMDF_WRITER_H
#define FBX2VMDF_WRITER_H

#include "Exchange.h"

#include <string>
#include <vector>

// Builds the complete .vmdf image.
std::vector<uint8_t> StoreCmdl(const Cmdl& cmdl, size_t alignment);

// Writes the <name>.vmdf.xml bone hierarchy used by the entity cooker.
bool ExportBoneHierarchy(const Cmdl& cmdl, const std::string& path);

#endif // FBX2VMDF_WRITER_H
