/****************************************************************************
//	fbx-to-vmdf
//	Adapted from Ayataka's exchange/MaterialOverrides.cpp (Usagi Engine,
//	Copyright (c) Vitei, Inc. 2013-2018, MIT licence).
*****************************************************************************/
#include "MaterialOverrides.h"
#include "Options.h"

#include <algorithm>

namespace {

struct EnumTable
{
	const char* szVarName;
	uint32_t uEnumValue;
};

const EnumTable g_FuncTable[] = {
	{ "Zero", vmdf::BLEND_FUNC_ZERO }, { "One", vmdf::BLEND_FUNC_ONE }, { "SrcColor", vmdf::BLEND_FUNC_SRC_COLOR },
	{ "OneMinusSrc", vmdf::BLEND_FUNC_ONE_MINUS_SRC_COLOR }, { "DstColor", vmdf::BLEND_FUNC_DST_COLOR },
	{ "OneMinusDstColor", vmdf::BLEND_FUNC_ONE_MINUS_DST_COLOR }, { "SrcAlpha", vmdf::BLEND_FUNC_SRC_ALPHA },
	{ "OneMinusSrcAlpha", vmdf::BLEND_FUNC_ONE_MINUS_SRC_ALPHA }, { "DstAlpha", vmdf::BLEND_FUNC_DST_ALPHA },
	{ "OneMinusDstAlpha", vmdf::BLEND_FUNC_ONE_MINUS_DST_ALPHA }, { "ConstantColor", vmdf::BLEND_FUNC_CONSTANT_COLOR },
	{ "OneMinusConstantColor", vmdf::BLEND_FUNC_ONE_MINUS_CONSTANT_COLOR }, { "ConstantAlpha", vmdf::BLEND_FUNC_CONSTANT_ALPHA },
	{ "OneMinusConstantAlpha", vmdf::BLEND_FUNC_ONE_MINUS_CONSTANT_ALPHA }, { "AlphaSaturate", vmdf::BLEND_FUNC_SRC_ALPHA_SATURATE },
	{ nullptr, 0 } };

// Ayataka: these two tables are named the wrong way round (the "func"
// table holds stencil operations and is used for testFunc); kept as is.
const EnumTable g_stencilFuncTable[] = {
	{ "Keep", vmdf::STENCIL_OP_KEEP }, { "Zero", vmdf::STENCIL_OP_ZERO }, { "Replace", vmdf::STENCIL_OP_REPLACE },
	{ "Incr", vmdf::STENCIL_OP_INCR }, { "Decr", vmdf::STENCIL_OP_DECR }, { "Invert", vmdf::STENCIL_OP_INVERT },
	{ "IncrWrap", vmdf::STENCIL_OP_INCR_WRAP }, { "DecrWrap", vmdf::STENCIL_OP_DECR_WRAP }, { nullptr, 0 } };

const EnumTable g_stencilOpTable[] = {
	{ "Never", vmdf::STENCIL_TEST_NEVER }, { "Always", vmdf::STENCIL_TEST_ALWAYS }, { "Equal", vmdf::STENCIL_TEST_EQUAL },
	{ "NotEqual", vmdf::STENCIL_TEST_NOTEQUAL }, { "Less", vmdf::STENCIL_TEST_LESS }, { "LessOrEqual", vmdf::STENCIL_TEST_LEQUAL },
	{ "Greater", vmdf::STENCIL_TEST_GREATER }, { "GreateOrEqual", vmdf::STENCIL_TEST_GEQUAL }, { nullptr, 0 } };

const EnumTable g_opTable[] = {
	{ "Add", vmdf::BLEND_EQUATION_ADD }, { "Sub", vmdf::BLEND_EQUATION_SUBTRACT }, { "RevereSub", vmdf::BLEND_EQUATION_REVERSE_SUBTRACT },
	{ "Min", vmdf::BLEND_EQUATION_MIN }, { "Max", vmdf::BLEND_EQUATION_MAX }, { nullptr, 0 } };

const EnumTable g_layerTable[] = {
	{ "Opaque", vmdf::Translucency_Type_TRANSLUCENCY_OPAQUE }, { "Translucent", vmdf::Translucency_Type_TRANSLUCENCY_TRANSLUCENT },
	{ "Additive", vmdf::Translucency_Type_TRANSLUCENCY_ADDITIVE }, { "Subtractive", vmdf::Translucency_Type_TRANSLUCENCY_SUBTRACTIVE },
	{ nullptr, 0 } };

const EnumTable g_alphaTestTable[] = {
	{ "Never", vmdf::ALPHA_TEST_NEVER }, { "Always", vmdf::ALPHA_TEST_ALWAYS }, { "Equal", vmdf::ALPHA_TEST_EQUAL },
	{ "NotEqual", vmdf::ALPHA_TEST_NOTEQUAL }, { "Less", vmdf::ALPHA_TEST_LESS }, { "LessOrEqual", vmdf::ALPHA_TEST_LEQUAL },
	{ "Greater", vmdf::ALPHA_TEST_GREATER }, { "GreateOrEqual", vmdf::ALPHA_TEST_GEQUAL }, { nullptr, 0 } };

const EnumTable g_cullFaceTable[] = { { "Front", vmdf::CULL_FACE_FRONT }, { "Back", vmdf::CULL_FACE_BACK }, { "None", vmdf::CULL_FACE_NONE }, { nullptr, 0 } };

void StringToValue(const EnumTable* pTable, const YAML::Node& node, uint32_t& uValOut)
{
	if (!node)
		return;
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

// Read-only lookups (a non-const yaml-cpp operator[] can add the key)
YAML::Node get(const YAML::Node& n, const char* key)
{
	if (!n || !n.IsMap())
		return YAML::Node(YAML::NodeType::Undefined);
	const YAML::Node& cn = n;
	return cn[key];
}

} // namespace

bool MaterialOverrides::Init(const std::string& fileName, const char* szDefaultPak, const char* szDefaultEffect, DependencyTracker* pDependencies)
{
	m_pDependencies = pDependencies;
	m_defaultPak = szDefaultPak;
	m_defaultEffect = szDefaultEffect;
	if (!fileExists(fileName))
		return false;
	m_mainNode = loadYamlFileCached(fileName);
	m_pDependencies->LogDependency(fileName);
	m_overrides = get(m_mainNode, "MaterialOverrides");
	return true;
}

std::string MaterialOverrides::ResolveEffectPath(const std::string& effectSet) const
{
	if (!g_options.effectsDir.empty())
	{
		std::string p = g_options.effectsDir + "/" + effectSet + ".yml";
		if (!fileExists(p))
			fatal("could not find effect %s (looked for %s)", effectSet.c_str(), p.c_str());
		return p;
	}
	const char* szUsagiPath = getenv("USAGI_DIR");
	if (!szUsagiPath || !*szUsagiPath)
		fatal("USAGI_DIR is not set and no --effects-dir was given; can't find effect %s", effectSet.c_str());
	std::string usagi = szUsagiPath;
	std::string emuPath = usagi + "/Data/GLSL/effects/" + effectSet + ".yml";
	if (fileExists(emuPath))
		return emuPath;
	// Ayataka's fallback: the same path under the directory above "Usagi"
	emuPath = usagi.substr(0, usagi.find("Usagi")) + "/Data/GLSL/effects/" + effectSet + ".yml";
	if (fileExists(emuPath))
		return emuPath;
	fatal("could not find effect %s", effectSet.c_str());
}

void MaterialOverrides::InitDefault(const char* szMaterialName, const std::vector<std::string>& definesIn, exchange::Material* pMatOut)
{
	std::string effectSet = m_defaultPak;
	std::string effectName = m_defaultEffect;
	std::vector<std::string> defines = definesIn;
	YAML::Node material = get(m_overrides, szMaterialName);
	if (material)
	{
		YAML::Node pak = get(material, "ShaderPak");
		YAML::Node effect = get(material, "Effect");
		if (pak && effect)
		{
			effectSet = yamlString(pak);
			effectName = yamlString(effect);
		}
		YAML::Node addDefines = get(material, "AddDefines");
		YAML::Node subDefines = get(material, "SubDefines");
		if (addDefines)
		{
			for (YAML::const_iterator it = addDefines.begin(); it != addDefines.end(); ++it)
			{
				std::string d = yamlString(*it);
				if (d.empty())
					continue;
				if (std::find(defines.begin(), defines.end(), d) == defines.end())
					defines.push_back(d);
			}
		}
		if (subDefines)
		{
			for (YAML::const_iterator it = subDefines.begin(); it != subDefines.end(); ++it)
			{
				std::string d = yamlString(*it);
				if (d.empty())
					continue;
				auto f = std::find(defines.begin(), defines.end(), d);
				if (f != defines.end())
					defines.erase(f);
			}
		}
	}
	std::string emuPath = ResolveEffectPath(effectSet);
	m_pDependencies->LogDependency(emuPath);
	pMatOut->InitCustomMaterial(emuPath, effectName.c_str(), defines);
}

void MaterialOverrides::ApplyOverrides(const char* szMaterialName, exchange::Material* pMatOut)
{
	YAML::Node material = get(m_overrides, szMaterialName);
	if (!material)
		return;
	vmdf::Material& mat = pMatOut->pb();

	YAML::Node alphaNode = get(material, "AlphaStateGroup");
	if (alphaNode)
	{
		vmdf::AlphaStateGroup& alpha = mat.rasterizer.alphaState;
		StringToValue(g_FuncTable, get(alphaNode, "rgbSrcFunc"), alpha.rgbSrcFunc);
		StringToValue(g_FuncTable, get(alphaNode, "rgbDestFunc"), alpha.rgbDestFunc);
		StringToValue(g_opTable, get(alphaNode, "rgbOp"), alpha.rgbOp);
		StringToValue(g_FuncTable, get(alphaNode, "alphaSrcFunc"), alpha.alphaSrcFunc);
		StringToValue(g_FuncTable, get(alphaNode, "alphaDestFunc"), alpha.alphaDestFunc);
		StringToValue(g_opTable, get(alphaNode, "alphaOp"), alpha.alphaOp);
		StringToValue(g_alphaTestTable, get(alphaNode, "alphaTestFunc"), alpha.alphaTestFunc);
		if (get(alphaNode, "alphaTestReference"))
			alpha.alphaTestReference = yamlFloat(get(alphaNode, "alphaTestReference"));
	}

	YAML::Node stencilNode = get(material, "StencilStateGroup");
	if (stencilNode)
	{
		vmdf::StencilState& stencil = mat.rasterizer.stencilTest;
		if (get(stencilNode, "enable"))
			stencil.isEnable = yamlBool(get(stencilNode, "enable"));
		if (get(stencilNode, "write"))
			stencil.isEnable = yamlBool(get(stencilNode, "write")); // Ayataka: sets isEnable
		StringToValue(g_stencilFuncTable, get(stencilNode, "testFunc"), stencil.func);
		StringToValue(g_stencilOpTable, get(stencilNode, "passOp"), stencil.passOperation);
		StringToValue(g_stencilOpTable, get(stencilNode, "failOp"), stencil.failOperation);
		StringToValue(g_stencilOpTable, get(stencilNode, "depthFailOp"), stencil.zFailOperation);
		if (get(stencilNode, "readMask"))
			stencil.readMask = yamlUInt(get(stencilNode, "readMask"));
		if (get(stencilNode, "writeMask"))
			stencil.writeMask = yamlUInt(get(stencilNode, "writeMask"));
		if (get(stencilNode, "ref"))
			stencil.ref = yamlUInt(get(stencilNode, "ref"));
	}

	if (get(material, "Transparent"))
		mat.rasterizer.blendEnabled = yamlBool(get(material, "Transparent"));

	StringToValue(g_cullFaceTable, get(material, "CullFace"), mat.rasterizer.cullFace);

	if (get(material, "Layer"))
		StringToValue(g_layerTable, get(material, "Layer"), mat.attribute.translucencyKind);

	YAML::Node textureNode = get(material, "Textures");
	if (textureNode)
	{
		for (YAML::const_iterator it = textureNode.begin(); it != textureNode.end(); ++it)
		{
			const YAML::Node& t = *it;
			YAML::Node hint = get(t, "hint");
			YAML::Node texture = get(t, "texture");
			if (!hint)
			{
				warning("texture override missing required data");
				continue;
			}
			uint32_t uTexIndex = 0;
			// Ayataka: always the first coordinator ("FIXME: Ordering of co-ordinators")
			vmdf::TextureCoordinator& texCo = mat.textureCoordinators[0];
			if (pMatOut->GetCustomFX(0).GetTextureIndex(yamlString(hint).c_str(), uTexIndex))
			{
				vmdf::Texture& tex = mat.textures[uTexIndex];
				if (texture)
				{
					std::string name = yamlString(texture);
					if (name.size() >= sizeof(tex.textureName))
						fatal("texture name %s is too long", name.c_str());
					setField(tex.textureName, name);
				}
				std::string h = yamlString(hint);
				if (h.size() >= sizeof(tex.textureHint))
					fatal("texture hint %s is too long", h.c_str());
				setField(tex.textureHint, h);
				GetTexCoordMapperOverrides(t, texCo.sourceCoordinate, texCo.translate, texCo.scale, texCo.rotate);
				GetSamplerOverrides(t, tex.minFilter, tex.magFilter, tex.mipFilter, tex.anisoLevel, tex.lodBias, tex.lodMinLevel);
			}
		}
	}

	YAML::Node constantsNode = get(material, "Constants");
	if (constantsNode)
	{
		for (YAML::const_iterator it = constantsNode.begin(); it != constantsNode.end(); ++it)
		{
			const YAML::Node& c = *it;
			YAML::Node set = get(c, "set");
			YAML::Node var = get(c, "name");
			YAML::Node value = get(c, "value");
			if (!var)
			{
				warning("constant override missing required data");
				continue;
			}
			std::string setName = set ? yamlString(set) : std::string();
			std::string varName = yamlString(var);
			for (uint32_t i = 0; i < vmdf::Material_RenderPass_COUNT; i++)
			{
				for (uint32_t j = 0; j < pMatOut->GetCustomFX(i).GetConstantSetCount(); j++)
				{
					if (set && strcmp(pMatOut->GetCustomFX(i).GetConstantSetName(j), setName.c_str()) != 0)
						continue;
					pMatOut->GetCustomFX(i).OverrideData(j, varName.c_str(), value, pMatOut->GetConstantSetData(i, j));
				}
			}
		}
	}
}
