/****************************************************************************
//	fbx-to-vmdf
//	Per-model material overrides (<model>.yml "MaterialOverrides").
//	Adapted from Ayataka's exchange/MaterialOverrides (Usagi Engine,
//	Copyright (c) Vitei, Inc. 2013-2018, MIT licence).
*****************************************************************************/
#ifndef FBX2VMDF_MATERIAL_OVERRIDES_H
#define FBX2VMDF_MATERIAL_OVERRIDES_H

#include "Exchange.h"

#include <yaml-cpp/yaml.h>

class MaterialOverrides
{
public:
	bool Init(const std::string& fileName, const char* szDefaultPak, const char* szDefaultEffect, DependencyTracker* pDependencies);
	void InitDefault(const char* szMaterialName, const std::vector<std::string>& defines, exchange::Material* pMatOut);
	void ApplyOverrides(const char* szMaterialName, exchange::Material* pMatOut);
	const YAML::Node& Root() const { return m_mainNode; }

private:
	std::string ResolveEffectPath(const std::string& effectSet) const;

	std::string m_defaultPak;
	std::string m_defaultEffect;
	DependencyTracker* m_pDependencies = nullptr;
	YAML::Node m_mainNode;
	YAML::Node m_overrides;
};

#endif // FBX2VMDF_MATERIAL_OVERRIDES_H
