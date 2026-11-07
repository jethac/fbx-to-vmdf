/****************************************************************************
//	fbx-to-vmdf
//	Custom effect definitions (Data/GLSL/effects/<Effect>.yml): constant
//	set layouts and defaults, samplers, attributes; also writes .cfx files.
//	Adapted from Usagi's Tools/Source/ResourceLib/MaterialDefinition/
//	MaterialDefinitionExporter (Copyright (c) Vitei, Inc. 2013-2018, MIT
//	licence). Behaviour, including its quirks, is kept so that material
//	constant data comes out identical to Ayataka's.
*****************************************************************************/
#ifndef FBX2VMDF_MATERIAL_DEFINITION_H
#define FBX2VMDF_MATERIAL_DEFINITION_H

#include "Common.h"
#include "VmdfFormat.h"

#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>

void GetSamplerOverrides(const YAML::Node& attributeNode, uint32_t& uMinFilter, uint32_t& uMagFilter, uint32_t& uMipFilter, uint32_t& uAniso, float& fLodBias, uint32_t& uBaseMip);
void GetTexCoordMapperOverrides(const YAML::Node& attributeNode, uint32_t& uSourceCoord, vmdf::Vector2f& vTrans, vmdf::Vector2f& vScale, float& fRot);

// Locale-independent YAML scalar conversions (yaml-cpp's as<float>() goes
// through an iostream and the global locale).
float yamlFloat(const YAML::Node& node);
bool yamlBool(const YAML::Node& node);
uint32_t yamlUInt(const YAML::Node& node);
int32_t yamlInt(const YAML::Node& node);
std::string yamlString(const YAML::Node& node);

// Parsed YAML documents are cached per path: Ayataka reloads the effect
// file once per material per pass.
YAML::Node loadYamlFileCached(const std::string& path);

class MaterialDefinition
{
public:
	int Load(const std::string& path, const std::string& defines);
	int Load(const std::string& path, const char* effect, const std::vector<std::string>& defineSets);
	int Load(const YAML::Node& mainNode, const std::string& defines);

	bool ExportFile(const std::string& path);

	bool GetTextureIndex(const char* szHint, uint32_t& indexOut) const;
	bool GetTextureIndex(uint32_t uTex, uint32_t& indexOut) const;
	uint32_t GetVariableCount(const char* szName) const;
	uint32_t GetConstantSetCount() const { return (uint32_t)m_constantSets.size(); }
	uint32_t GetConstantSetSize(uint32_t uSet) const { return (uint32_t)m_constantSets[uSet].rawData.size(); }
	const char* GetConstantSetName(uint32_t uSet) const { return m_constantSets[uSet].set.szName; }
	void CopyDefaultData(uint32_t uSet, void* pDst) const;
	uint32_t GetTextureCount() const { return (uint32_t)m_samplers.size(); }
	const char* GetDefaultTexName(uint32_t uTex) const { return m_samplers[uTex].texName; }
	void GetSamplerDefaults(uint32_t uTex, uint32_t& uMinFilter, uint32_t& uMagFilter, uint32_t& uMipFilter, uint32_t& uAniso, float& fLodBias, uint32_t& uBaseMip) const;

	bool OverrideData(uint32_t uSet, const char* szVariableName, const YAML::Node& node, void* pDst) const;
	void OverrideDefault(uint32_t uSet, const char* szName, void* pDst, const void* pSrc, uint32_t uSize, uint32_t uOffset) const;
	bool GetVariable(uint32_t uSet, const void* pSrc, const char* szName, void* pData, uint32_t uSize, uint32_t uOffset) const;

private:
	struct ConstantSetData
	{
		vmdf::CustomEffectDecl::ConstantSet set;
		std::vector<vmdf::CustomEffectDecl::Constant> constants;
		std::vector<uint8_t> rawData;
	};

	bool LoadAttributes(const YAML::Node& attributeNode);
	bool LoadSamplers(const YAML::Node& attributeNode);
	bool LoadConstantSets(const YAML::Node& attributeNode);
	bool IsValidWithDefineSet(const std::string& conditions) const;
	void InitBinaryData();

	vmdf::CustomEffectDecl::Header m_header = {};
	std::vector<vmdf::CustomEffectDecl::Sampler> m_samplers;
	std::vector<vmdf::CustomEffectDecl::Attribute> m_attributes;
	std::vector<ConstantSetData> m_constantSets;
	std::vector<std::string> m_defines;
	std::vector<uint8_t> m_binary;
};

#endif // FBX2VMDF_MATERIAL_DEFINITION_H
