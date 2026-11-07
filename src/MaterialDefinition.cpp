/****************************************************************************
//	fbx-to-vmdf
//	Adapted from Usagi's MaterialDefinitionExporter.cpp (Copyright (c)
//	Vitei, Inc. 2013-2018, MIT licence). Ayataka's quirks are kept on
//	purpose where they affect the output (marked "Ayataka:").
*****************************************************************************/
#include "MaterialDefinition.h"

#include <map>
#include <sstream>

namespace {

struct Mapping
{
	const char* szName;
	uint32_t uValue;
};

const Mapping g_textureTypeMappings[] = {
	{ "sampler2D", vmdf::TD_TEXTURE2D },
	{ "sampler1D", vmdf::TD_TEXTURE1D },
	{ "samplerCube", vmdf::TD_TEXTURECUBE },
	{ "sampler3D", vmdf::TD_TEXTURE3D },
	{ "sampler2DArray", vmdf::TD_TEXTURE2DARRAY },
	{ nullptr, USG_INVALID_ID } };

const Mapping g_constantMappings[] = {
	{ "mat4x4", vmdf::CT_MATRIX_44 },
	{ "mat3x4", vmdf::CT_MATRIX_43 },
	{ "vec4", vmdf::CT_VECTOR_4 },
	{ "vec3", vmdf::CT_VECTOR_3 },
	{ "vec2", vmdf::CT_VECTOR_2 },
	{ "float", vmdf::CT_FLOAT },
	{ "int", vmdf::CT_INT },
	{ "ivec2", vmdf::CT_VECTOR2I },
	{ "ivec4", vmdf::CT_VECTOR4I },
	{ "uvec4", vmdf::CT_VECTOR4U },
	{ "bool", vmdf::CT_BOOL },
	{ "invalid", vmdf::CT_STRUCT },
	{ nullptr, USG_INVALID_ID } };

const Mapping g_shaderMappings[] = {
	{ "VS", vmdf::SHADER_FLAG_VERTEX },
	{ "GS", vmdf::SHADER_FLAG_GEOMETRY },
	{ "PS", vmdf::SHADER_FLAG_PIXEL },
	{ "TC", vmdf::SHADER_FLAG_TCONTROL },
	{ "TE", vmdf::SHADER_FLAG_TEVAL },
	{ nullptr, USG_INVALID_ID } };

const Mapping g_bufferBindingMappings[] = {
	{ "Material", vmdf::SHADER_CONSTANT_MATERIAL },
	{ "Material1", vmdf::SHADER_CONSTANT_MATERIAL_1 },
	{ "Custom0", vmdf::SHADER_CONSTANT_CUSTOM_0 },
	{ "Custom1", vmdf::SHADER_CONSTANT_CUSTOM_1 },
	{ "Custom2", vmdf::SHADER_CONSTANT_CUSTOM_2 },
	{ "Custom3", vmdf::SHADER_CONSTANT_CUSTOM_3 },
	{ nullptr, USG_INVALID_ID } };

struct EnumTable
{
	const char* szVarName;
	uint32_t uEnumValue;
};

const EnumTable g_filterTable[] = { { "Linear", vmdf::SAMP_FILTER_LINEAR }, { "Point", vmdf::SAMP_FILTER_POINT }, { nullptr, 0 } };
const EnumTable g_mipFilterTable[] = { { "Linear", vmdf::MIP_FILTER_LINEAR }, { "Point", vmdf::MIP_FILTER_POINT }, { nullptr, 0 } };
const EnumTable g_anisoTable[] = {
	{ "1", vmdf::ANISO_LEVEL_1 }, { "2", vmdf::ANISO_LEVEL_2 }, { "4", vmdf::ANISO_LEVEL_4 }, { "8", vmdf::ANISO_LEVEL_8 }, { "16", vmdf::ANISO_LEVEL_16 }, { nullptr, 0 } };

uint32_t GetType(const char* szTypeName, const Mapping* pMapping)
{
	while (pMapping->szName)
	{
		if (strcmp(szTypeName, pMapping->szName) == 0)
		{
			return pMapping->uValue;
		}
		pMapping++;
	}
	fatal("invalid type %s in effect definition", szTypeName);
}

uint32_t GetShaderType(const std::string& types)
{
	std::stringstream stream(types);
	std::string entry;
	uint32_t uOut = 0;
	while (stream >> entry)
	{
		uOut |= GetType(entry.c_str(), g_shaderMappings);
	}
	return uOut;
}

uint32_t AlignOffset(uint32_t uOffset, uint32_t uElementType)
{
	uint32_t uAlign = uElementType == vmdf::CT_BOOL ? 1 : 4;
	return (uOffset + uAlign - 1) - ((uOffset + uAlign - 1) % uAlign);
}

uint32_t GetSize(uint32_t uConstantType, uint32_t uArraySize)
{
	return vmdf::g_uConstantSize[uConstantType] * uArraySize;
}

void StringToValue(const EnumTable* pTable, const YAML::Node& node, uint32_t& uValOut)
{
	if (!node)
	{
		return;
	}
	std::string value = yamlString(node);
	if (value.empty())
	{
		warning("invalid string for override");
		return;
	}
	for (; pTable->szVarName != nullptr; pTable++)
	{
		if (strcmp(pTable->szVarName, value.c_str()) == 0)
		{
			uValOut = pTable->uEnumValue;
			return;
		}
	}
	warning("invalid override semantic %s", value.c_str());
}

template <class T> void putValue(uint8_t*& p, T v)
{
	memcpy(p, &v, sizeof(T));
	p += sizeof(T);
}

void SetDefaultData(uint32_t uConstantType, uint32_t uConstantCount, const YAML::Node& node, uint8_t* pD8)
{
	if (!node)
	{
		memset(pD8, 0, (size_t)vmdf::g_uConstantSize[uConstantType] * uConstantCount);
		return;
	}
	uint8_t* pSrc = pD8;
	auto expect = [&](size_t n) {
		if (node.size() != n)
		{
			fatal("default data contains %d entries, expected %d", (int)node.size(), (int)n);
		}
	};
	switch (uConstantType)
	{
	case vmdf::CT_MATRIX_44: expect(16); for (uint32_t i = 0; i < 16; i++) putValue(pD8, yamlFloat(node[i])); break;
	case vmdf::CT_MATRIX_43: expect(12); for (uint32_t i = 0; i < 12; i++) putValue(pD8, yamlFloat(node[i])); break;
	case vmdf::CT_VECTOR_4: expect(4); for (uint32_t i = 0; i < 4; i++) putValue(pD8, yamlFloat(node[i])); break;
	case vmdf::CT_VECTOR_3: expect(3); for (uint32_t i = 0; i < 3; i++) putValue(pD8, yamlFloat(node[i])); break;
	case vmdf::CT_VECTOR4I: expect(4); for (uint32_t i = 0; i < 4; i++) putValue(pD8, yamlInt(node[i])); break;
	case vmdf::CT_VECTOR4U: expect(4); for (uint32_t i = 0; i < 4; i++) putValue(pD8, yamlUInt(node[i])); break;
	case vmdf::CT_VECTOR_2: expect(2); for (uint32_t i = 0; i < 2; i++) putValue(pD8, yamlFloat(node[i])); break;
	case vmdf::CT_FLOAT: putValue(pD8, yamlFloat(node)); break;
	case vmdf::CT_INT: putValue(pD8, yamlInt(node)); break;
	case vmdf::CT_BOOL: putValue(pD8, (uint8_t)(yamlBool(node) ? 1 : 0)); break;
	default:
		// Ayataka warns and then copies whatever the buffer held; keep it zero.
		warning("no default data for constant type %u", uConstantType);
		memset(pD8, 0, (size_t)vmdf::g_uConstantSize[uConstantType] * uConstantCount);
		return;
	}
	for (uint32_t i = 1; i < uConstantCount; i++)
	{
		memcpy(pD8, pSrc, vmdf::g_uConstantSize[uConstantType]);
		pD8 += vmdf::g_uConstantSize[uConstantType];
	}
}

std::vector<std::string> splitSpaces(const std::string& s)
{
	// Ayataka: splits on single spaces, keeping empty tokens
	std::vector<std::string> out;
	std::string rest = s;
	while (!rest.empty())
	{
		size_t next = rest.find_first_of(' ');
		if (next != std::string::npos)
		{
			out.push_back(rest.substr(0, next));
			rest = rest.substr(next + 1);
		}
		else
		{
			out.push_back(rest);
			rest.clear();
		}
	}
	return out;
}

} // namespace

float yamlFloat(const YAML::Node& node)
{
	bool ok = false;
	float v = parseFloat(node.Scalar(), &ok);
	if (!ok)
	{
		fatal("expected a number in YAML, got '%s'", node.Scalar().c_str());
	}
	return v;
}

bool yamlBool(const YAML::Node& node) { return node.as<bool>(); }
uint32_t yamlUInt(const YAML::Node& node) { return node.as<uint32_t>(); }
int32_t yamlInt(const YAML::Node& node) { return node.as<int32_t>(); }
std::string yamlString(const YAML::Node& node) { return node.as<std::string>(); }

YAML::Node loadYamlFileCached(const std::string& path)
{
	static std::map<std::string, YAML::Node> cache;
	auto it = cache.find(path);
	if (it != cache.end())
	{
		return it->second;
	}
	YAML::Node node;
	try
	{
		node = YAML::LoadFile(path);
	}
	catch (const std::exception& e)
	{
		fatal("couldn't parse %s: %s", path.c_str(), e.what());
	}
	cache[path] = node;
	return node;
}

void GetSamplerOverrides(const YAML::Node& attributeNode, uint32_t& uMinFilter, uint32_t& uMagFilter, uint32_t& uMipFilter, uint32_t& uAniso, float& fLodBias, uint32_t& uBaseMip)
{
	StringToValue(g_filterTable, attributeNode["minFilter"], uMinFilter);
	StringToValue(g_filterTable, attributeNode["magFilter"], uMagFilter);
	StringToValue(g_mipFilterTable, attributeNode["mipFilter"], uMipFilter);
	StringToValue(g_anisoTable, attributeNode["anisotropy"], uAniso);
	if (attributeNode["lodBias"])
	{
		fLodBias = yamlFloat(attributeNode["lodBias"]);
	}
	if (attributeNode["lodMinLevel"])
	{
		uBaseMip = yamlUInt(attributeNode["lodMinLevel"]);
	}
}

void GetTexCoordMapperOverrides(const YAML::Node& attributeNode, uint32_t& uSourceCoord, vmdf::Vector2f& vTrans, vmdf::Vector2f& vScale, float& fRot)
{
	(void)vScale;
	const YAML::Node coordNode = attributeNode["sourceUV"];
	const YAML::Node transNode = attributeNode["trans"];
	const YAML::Node scaleNode = attributeNode["scale"];
	const YAML::Node rotateNode = attributeNode["rotate"];
	if (coordNode)
	{
		uSourceCoord = yamlUInt(coordNode);
	}
	if (transNode)
	{
		vTrans.x = yamlFloat(transNode[0]);
		vTrans.y = yamlFloat(transNode[1]);
	}
	if (scaleNode)
	{
		// Ayataka: the scale override writes the translation
		vTrans.x = yamlFloat(scaleNode[0]);
		vTrans.y = yamlFloat(scaleNode[1]);
	}
	if (rotateNode)
	{
		fRot = yamlFloat(rotateNode);
	}
}

bool MaterialDefinition::LoadAttributes(const YAML::Node& attributes)
{
	for (YAML::const_iterator it = attributes.begin(); it != attributes.end(); ++it)
	{
		const YAML::Node& n = *it;
		vmdf::CustomEffectDecl::Attribute attrib;
		memset(&attrib, 0, sizeof(attrib));
		setField(attrib.hint, yamlString(n["hint"]));
		setField(attrib.name, yamlString(n["name"]));
		attrib.uIndex = yamlUInt(n["index"]);
		attrib.eConstantType = GetType(yamlString(n["type"]).c_str(), g_constantMappings);
		attrib.uCount = n["count"] ? yamlUInt(n["count"]) : 1;
		SetDefaultData(attrib.eConstantType, 1, n["default"], attrib.defaultData);
		if (n["defines"] && !IsValidWithDefineSet(yamlString(n["defines"])))
		{
			continue;
		}
		attrib.uVBIndex = n["buffer"] ? yamlUInt(n["buffer"]) : 0;
		m_attributes.push_back(attrib);
	}
	return true;
}

bool MaterialDefinition::LoadSamplers(const YAML::Node& samplers)
{
	for (YAML::const_iterator it = samplers.begin(); it != samplers.end(); ++it)
	{
		const YAML::Node& n = *it;
		vmdf::CustomEffectDecl::Sampler sampler;
		memset(&sampler, 0, sizeof(sampler));
		setField(sampler.hint, yamlString(n["hint"]));
		sampler.uIndex = yamlUInt(n["index"]);
		if (n["default"])
		{
			setField(sampler.texName, yamlString(n["default"]));
		}
		if (n["defines"] && !IsValidWithDefineSet(yamlString(n["defines"])))
		{
			continue;
		}
		sampler.eTexType = n["type"] ? GetType(yamlString(n["type"]).c_str(), g_textureTypeMappings) : (uint32_t)vmdf::TD_TEXTURE2D;
		sampler.eFilterMin = vmdf::SAMP_FILTER_LINEAR;
		sampler.eFilterMag = vmdf::SAMP_FILTER_LINEAR;
		sampler.eFilterMip = vmdf::SAMP_FILTER_LINEAR;
		sampler.eAnisoLevel = vmdf::ANISO_LEVEL_16;
		sampler.LodBias = 0.0f;
		sampler.LodMinLevel = 0;
		GetSamplerOverrides(n, sampler.eFilterMin, sampler.eFilterMag, sampler.eFilterMip, sampler.eAnisoLevel, sampler.LodBias, sampler.LodMinLevel);
		sampler.uShaderSets = n["shaderType"] ? GetShaderType(yamlString(n["shaderType"])) : (uint32_t)vmdf::SHADER_FLAG_PIXEL;
		m_samplers.push_back(sampler);
	}
	return true;
}

void MaterialDefinition::GetSamplerDefaults(uint32_t uTex, uint32_t& uMinFilter, uint32_t& uMagFilter, uint32_t& uMipFilter, uint32_t& uAniso, float& fLodBias, uint32_t& uBaseMip) const
{
	uMinFilter = m_samplers[uTex].eFilterMin;
	uMagFilter = m_samplers[uTex].eFilterMag;
	uMipFilter = m_samplers[uTex].eFilterMip;
	uAniso = m_samplers[uTex].eAnisoLevel;
	fLodBias = m_samplers[uTex].LodBias;
	uBaseMip = m_samplers[uTex].LodMinLevel;
}

bool MaterialDefinition::LoadConstantSets(const YAML::Node& constantDefs)
{
	for (YAML::const_iterator setIt = constantDefs.begin(); setIt != constantDefs.end(); ++setIt)
	{
		const YAML::Node& sn = *setIt;
		ConstantSetData setData;
		memset(&setData.set, 0, sizeof(setData.set));
		if (sn["defines"] && !IsValidWithDefineSet(yamlString(sn["defines"])))
		{
			continue;
		}
		if (!sn["shaderType"])
		{
			fatal("constant set without a shaderType in effect definition");
		}
		setData.set.uShaderSets = GetShaderType(yamlString(sn["shaderType"]));
		setData.set.uBinding = GetType(yamlString(sn["binding"]).c_str(), g_bufferBindingMappings);
		const YAML::Node variableDefs = sn["Variables"];
		setData.set.uConstants = (uint32_t)variableDefs.size();
		if (sn["name"])
		{
			setField(setData.set.szName, yamlString(sn["name"]));
		}
		uint32_t uOffset = 0;
		for (YAML::const_iterator vit = variableDefs.begin(); vit != variableDefs.end(); ++vit)
		{
			const YAML::Node& vn = *vit;
			vmdf::CustomEffectDecl::Constant constant;
			memset(&constant, 0, sizeof(constant));
			constant.eConstantType = GetType(yamlString(vn["type"]).c_str(), g_constantMappings);
			setField(constant.szName, yamlString(vn["name"]));
			constant.uNameHash = crc32(constant.szName);
			constant.uiCount = vn["count"] ? yamlUInt(vn["count"]) : 1;
			// Ayataka: the offset advances before the define test, so a
			// variable compiled out still takes up its space
			uOffset = AlignOffset(uOffset, constant.eConstantType);
			constant.uiOffset = uOffset;
			uOffset += GetSize(constant.eConstantType, constant.uiCount);
			if (vn["defines"] && !IsValidWithDefineSet(yamlString(vn["defines"])))
			{
				continue;
			}
			setData.constants.push_back(constant);
		}
		uint32_t uRawDataSize = (uint32_t)calcAlignedSize(uOffset, 4);
		setData.set.uDataSize = uRawDataSize;
		setData.rawData.assign(uRawDataSize, 0);
		uint32_t uIndex = 0;
		for (YAML::const_iterator vit = variableDefs.begin(); vit != variableDefs.end(); ++vit)
		{
			const YAML::Node& vn = *vit;
			if (vn["defines"] && !IsValidWithDefineSet(yamlString(vn["defines"])))
			{
				continue;
			}
			const vmdf::CustomEffectDecl::Constant& c = setData.constants[uIndex];
			SetDefaultData(c.eConstantType, c.uiCount, vn["default"], setData.rawData.data() + c.uiOffset);
			uIndex++;
		}
		m_constantSets.push_back(setData);
	}
	return true;
}

int MaterialDefinition::Load(const YAML::Node& mainNode, const std::string& defines)
{
	m_defines = splitSpaces(defines);
	LoadAttributes(mainNode["Attributes"]);
	LoadSamplers(mainNode["Samplers"]);
	LoadConstantSets(mainNode["ConstantDefs"]);
	return 0;
}

int MaterialDefinition::Load(const std::string& path, const std::string& defines)
{
	YAML::Node mainNode = loadYamlFileCached(path);
	return Load(mainNode, defines);
}

int MaterialDefinition::Load(const std::string& path, const char* effect, const std::vector<std::string>& defineSets)
{
	const YAML::Node mainNode = loadYamlFileCached(path);
	if (mainNode.IsNull())
	{
		return -1;
	}
	const YAML::Node customFX = mainNode["CustomEffects"];
	const YAML::Node effects = mainNode["Effects"];

	auto countMatches = [&](const YAML::Node& sets) {
		uint32_t found = 0;
		for (YAML::const_iterator it = sets.begin(); it != sets.end(); ++it)
		{
			for (size_t i = 0; i < defineSets.size(); i++)
			{
				if (defineSets[i] == yamlString((*it)["name"]))
				{
					found++;
				}
			}
		}
		return found;
	};

	// Pick the effect entry with this name that matches the most define
	// sets (the last one when no define sets were asked for).
	uint32_t uMaxDefinesFound = 0;
	int chosen = -1;
	int index = 0;
	for (YAML::const_iterator it = effects.begin(); it != effects.end(); ++it, ++index)
	{
		const YAML::Node& e = *it;
		uint32_t uFoundThisNode = 0;
		if (e["define_sets"])
		{
			uFoundThisNode += countMatches(e["define_sets"]);
		}
		if (e["global_sets"])
		{
			uFoundThisNode += countMatches(e["global_sets"]);
		}
		if (e["name"] && yamlString(e["name"]) == effect && ((uFoundThisNode > uMaxDefinesFound) || defineSets.empty()))
		{
			uMaxDefinesFound = uFoundThisNode;
			chosen = index;
		}
	}
	if (chosen < 0)
	{
		return -1;
	}
	const YAML::Node effectNode = effects[(size_t)chosen];
	YAML::Node customFXNode;
	if (effectNode["custom_effect"])
	{
		std::string customFXString = yamlString(effectNode["custom_effect"]);
		customFXNode = customFX[customFXString];
		if (!customFXNode || customFXNode.IsNull())
		{
			warning("could not find custom effect %s", customFXString.c_str());
		}
	}
	else
	{
		return -1;
	}

	// Ayataka: the define_sets branch reads the misspelt key "defineSets"
	// (so contributes nothing) and matches are concatenated without a
	// separator.
	std::string defines;
	if (effectNode["global_sets"])
	{
		const YAML::Node defineNode = effectNode["global_sets"];
		for (YAML::const_iterator it = defineNode.begin(); it != defineNode.end(); ++it)
		{
			for (size_t i = 0; i < defineSets.size(); i++)
			{
				if (defineSets[i] == yamlString((*it)["name"]))
				{
					defines += yamlString((*it)["defines"]);
				}
			}
		}
	}
	Load(customFXNode, defines);
	return 1;
}

void MaterialDefinition::CopyDefaultData(uint32_t uSet, void* pDst) const
{
	memcpy(pDst, m_constantSets[uSet].rawData.data(), m_constantSets[uSet].rawData.size());
}

bool MaterialDefinition::GetTextureIndex(uint32_t uTex, uint32_t& indexOut) const
{
	if (uTex < m_samplers.size())
	{
		indexOut = m_samplers[uTex].uIndex;
		return true;
	}
	return false;
}

bool MaterialDefinition::GetTextureIndex(const char* szHint, uint32_t& indexOut) const
{
	for (const auto& s : m_samplers)
	{
		if (strcmp(szHint, s.hint) == 0)
		{
			indexOut = s.uIndex;
			return true;
		}
	}
	return false;
}

uint32_t MaterialDefinition::GetVariableCount(const char* szName) const
{
	for (const auto& set : m_constantSets)
	{
		for (const auto& c : set.constants)
		{
			if (strcmp(c.szName, szName) == 0)
			{
				return c.uiCount;
			}
		}
	}
	return 0;
}

bool MaterialDefinition::OverrideData(uint32_t uSet, const char* szVariableName, const YAML::Node& node, void* pDst) const
{
	for (const auto& c : m_constantSets[uSet].constants)
	{
		if (strcmp(c.szName, szVariableName) == 0)
		{
			SetDefaultData(c.eConstantType, c.uiCount, node, ((uint8_t*)pDst) + c.uiOffset);
			return true;
		}
	}
	return false;
}

void MaterialDefinition::OverrideDefault(uint32_t uSet, const char* szName, void* pDst, const void* pSrc, uint32_t uSize, uint32_t uOffset) const
{
	for (const auto& var : m_constantSets[uSet].constants)
	{
		if (strcmp(var.szName, szName) == 0)
		{
			uint32_t off = uOffset + var.uiOffset;
			if ((off + uSize) <= (vmdf::g_uConstantSize[var.eConstantType] * var.uiCount) + var.uiOffset)
			{
				memcpy(((uint8_t*)pDst) + off, pSrc, uSize);
			}
			// Ayataka: the offset accumulates across same-named matches
			uOffset = off;
		}
	}
}

bool MaterialDefinition::GetVariable(uint32_t uSet, const void* pSrc, const char* szName, void* pData, uint32_t uSize, uint32_t uOffset) const
{
	for (const auto& var : m_constantSets[uSet].constants)
	{
		if (strcmp(var.szName, szName) == 0)
		{
			uOffset += var.uiOffset;
			if ((uOffset + uSize) <= (vmdf::g_uConstantSize[var.eConstantType] * var.uiCount) + var.uiOffset)
			{
				memcpy(pData, ((const uint8_t*)pSrc) + uOffset, uSize);
				return true;
			}
		}
	}
	return false;
}

bool MaterialDefinition::IsValidWithDefineSet(const std::string& conditions) const
{
	for (std::string cond : splitSpaces(conditions))
	{
		bool bShouldNotHave = !cond.empty() && cond[0] == '!';
		if (bShouldNotHave)
		{
			cond = cond.substr(1);
		}
		bool bFound = false;
		for (const auto& d : m_defines)
		{
			if (cond == d)
			{
				bFound = true;
			}
		}
		if (bShouldNotHave == bFound)
		{
			return false;
		}
	}
	return true;
}

void MaterialDefinition::InitBinaryData()
{
	uint32_t uAttributeSize = (uint32_t)(sizeof(vmdf::CustomEffectDecl::Attribute) * m_attributes.size());
	uint32_t uSamplerSize = (uint32_t)(sizeof(vmdf::CustomEffectDecl::Sampler) * m_samplers.size());
	uint32_t uConstantSetSize = (uint32_t)(sizeof(vmdf::CustomEffectDecl::ConstantSet) * m_constantSets.size());
	uint32_t uDeclSize = 0, uRawDataSize = 0;
	for (const auto& s : m_constantSets)
	{
		uDeclSize += (uint32_t)(s.constants.size() * sizeof(vmdf::CustomEffectDecl::Constant));
		uRawDataSize += (uint32_t)s.rawData.size();
	}
	m_binary.assign(uAttributeSize + uSamplerSize + uConstantSetSize + uDeclSize + uRawDataSize, 0);

	m_header.uAttributeCount = (uint32_t)m_attributes.size();
	m_header.uAttributeOffset = 0;
	m_header.uSamplerCount = (uint32_t)m_samplers.size();
	m_header.uSamplerOffset = m_header.uAttributeOffset + uAttributeSize;
	m_header.uConstantSetCount = (uint32_t)m_constantSets.size();
	m_header.uConstantSetDeclOffset = m_header.uSamplerOffset + uSamplerSize;

	if (uAttributeSize)
		memcpy(m_binary.data() + m_header.uAttributeOffset, m_attributes.data(), uAttributeSize);
	if (uSamplerSize)
		memcpy(m_binary.data() + m_header.uSamplerOffset, m_samplers.data(), uSamplerSize);

	uint32_t uDeclOffset = m_header.uConstantSetDeclOffset + uConstantSetSize;
	uint32_t uVariableOffset = uDeclOffset + uDeclSize;
	uint8_t* pDst = m_binary.data() + m_header.uConstantSetDeclOffset;
	for (auto& s : m_constantSets)
	{
		s.set.uDeclOffset = uDeclOffset;
		s.set.uDataOffset = uVariableOffset;
		uDeclOffset += (uint32_t)(s.constants.size() * sizeof(vmdf::CustomEffectDecl::Constant));
		uVariableOffset += (uint32_t)s.rawData.size();
		memcpy(pDst, &s.set, sizeof(s.set));
		pDst += sizeof(s.set);
	}
	for (const auto& s : m_constantSets)
	{
		size_t n = sizeof(vmdf::CustomEffectDecl::Constant) * s.constants.size();
		if (n)
			memcpy(pDst, s.constants.data(), n);
		pDst += n;
	}
	for (const auto& s : m_constantSets)
	{
		if (!s.rawData.empty())
			memcpy(pDst, s.rawData.data(), s.rawData.size());
		pDst += s.rawData.size();
	}
}

bool MaterialDefinition::ExportFile(const std::string& path)
{
	InitBinaryData();
	std::vector<uint8_t> out(sizeof(m_header) + m_binary.size());
	memcpy(out.data(), &m_header, sizeof(m_header));
	if (!m_binary.empty())
		memcpy(out.data() + sizeof(m_header), m_binary.data(), m_binary.size());
	return writeFile(path, out.data(), out.size());
}
