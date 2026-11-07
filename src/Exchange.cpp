/****************************************************************************
//	fbx-to-vmdf
//	Adapted from Ayataka (Usagi Engine, Copyright (c) Vitei, Inc.
//	2013-2018, MIT licence): exchange/Shape.cpp, exchange/Mesh.cpp,
//	exchange/Material.cpp, exchange/Skeleton.h, cmdl/Cmdl.cpp,
//	Dependencies/DependencyTracker.cpp, and the nanopb *_init defaults
//	of the engine's generated model structs.
*****************************************************************************/
#include "Exchange.h"
#include "Options.h"

namespace exchange {

// ------------------------------------------------------------------ init
// The engine's generated _init functions set only the fields that have a
// default; Ayataka left the rest as whatever the allocation held. Here
// every struct starts zeroed.

void VertexStreamInfo_init(vmdf::VertexStreamInfo* p)
{
	memset(p, 0, sizeof(*p));
	p->scaling = 1.0f;
}

void Shape_init(vmdf::Shape* p)
{
	memset(p, 0, sizeof(*p));
	for (auto& s : p->streamInfo)
	{
		VertexStreamInfo_init(&s);
	}
}

void Mesh_init(vmdf::Mesh* p)
{
	memset(p, 0, sizeof(*p));
	p->shapeRefIndex = 0xFFFFFFFFu;
	p->shapeShadowRefIndex = 0xFFFFFFFFu;
	p->materialRefIndex = 0xFFFFFFFFu;
	p->renderPriority = 0;
	p->isVisible = 1;
}

void Bone_init(vmdf::Bone* p)
{
	memset(p, 0, sizeof(*p));
	p->parentIndex = 0xFFFFFFFFu;
}

void Skeleton_init(vmdf::Skeleton* p)
{
	memset(p, 0, sizeof(*p));
	p->rootBoneIndex = 0xFFFFFFFFu;
}

static void Rasterizer_init(vmdf::Rasterizer* r)
{
	r->attribute = 0;
	r->depthTestFunc = 0;
	r->blendEnabled = 0;
	r->alphaState.rgbSrcFunc = vmdf::BLEND_FUNC_ONE;
	r->alphaState.rgbDestFunc = vmdf::BLEND_FUNC_ZERO;
	r->alphaState.rgbOp = vmdf::BLEND_EQUATION_ADD;
	r->alphaState.alphaSrcFunc = vmdf::BLEND_FUNC_ONE;
	r->alphaState.alphaDestFunc = vmdf::BLEND_FUNC_ZERO;
	r->alphaState.alphaOp = vmdf::BLEND_EQUATION_ADD;
	r->alphaState.constColor = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	r->alphaState.alphaTestFunc = vmdf::ALPHA_TEST_ALWAYS;
	r->alphaState.alphaTestReference = 0.0f;
	r->cullFace = 1;
	r->isPolygonOffsetEnable = 0;
	r->polygonOffsetUnit = 0;
	r->stencilTest.isEnable = 1;
	r->stencilTest.func = vmdf::STENCIL_TEST_ALWAYS;
	r->stencilTest.ref = 128;
	r->stencilTest.readMask = 0;
	r->stencilTest.writeMask = 128;
	r->stencilTest.failOperation = vmdf::STENCIL_OP_KEEP;
	r->stencilTest.zFailOperation = vmdf::STENCIL_OP_KEEP;
	r->stencilTest.passOperation = vmdf::STENCIL_OP_REPLACE;
	r->colorMask = 15;
}

void Material_init(vmdf::Material* p)
{
	memset(p, 0, sizeof(*p));
	// Texture_init: every default is 0
	Rasterizer_init(&p->rasterizer);
	p->textureCoordinators_count = 0;
	// TextureCoordinator_init isn't called by Material_init; zero
	p->attribute.translucencyKind = 0;
}

void ModelHeader_init(vmdf::ModelHeader* p)
{
	memset(p, 0, sizeof(*p));
}

// ----------------------------------------------------------------- Shape
uint32_t Shape::GetVertexAttributeMask() const
{
	uint32_t attr = 0;
	for (const auto& info : m_vertexStreamInfos)
	{
		attr |= 1u << info.attribute;
	}
	return attr;
}

uint32_t Shape::SearchStream(uint32_t attrType) const
{
	for (const auto& info : m_vertexStreamInfos)
	{
		if (info.attribute == attrType)
		{
			return info.refIndex;
		}
	}
	return UINT32_MAX;
}

uint32_t Shape::GetBoneIndexCount() const
{
	for (const auto& info : m_vertexStreamInfos)
	{
		if (info.attribute == vmdf::VertexAttribute_BONE_INDEX)
		{
			return info.columns;
		}
	}
	return 0;
}

bool Shape::HasVertexAlpha() const
{
	for (const auto& info : m_vertexStreamInfos)
	{
		if (info.attribute == vmdf::VertexAttribute_COLOR)
		{
			return info.columns >= 4;
		}
	}
	for (uint32_t i = 0; i < m_shape.singleAttributes_count; ++i)
	{
		if (m_shape.singleAttributes[i].attribute == vmdf::VertexAttribute_COLOR)
		{
			return m_shape.singleAttributes[i].columns >= 4;
		}
	}
	return false;
}

vmdf::VertexStreamInfo* Shape::GetVertexStreamInfoOfType(uint32_t attribType)
{
	for (auto& info : m_vertexStreamInfos)
	{
		if (info.attribute == attribType)
		{
			return &info;
		}
	}
	return nullptr;
}

uint32_t Shape::SearchSingleAttribute(uint32_t attrType) const
{
	for (uint32_t i = 0; i < m_shape.singleAttributes_count; ++i)
	{
		if (m_shape.singleAttributes[i].attribute == attrType)
		{
			return i;
		}
	}
	return UINT32_MAX;
}

// ------------------------------------------------------------------ Mesh
// Ayataka's copyString only copies when the source fits including the
// length it was given, so a 39-character mesh name is dropped entirely.
static void copyString(char* pDest, size_t destLen, const char* pSrc, size_t srcLen)
{
	if (srcLen < destLen)
	{
		memset(pDest, 0, destLen);
		memcpy(pDest, pSrc, srcLen);
		pDest[srcLen] = '\0';
	}
}

void Mesh::SetName(const char* p)
{
	copyString(m_mesh.name, sizeof(m_mesh.name), p, strlen(p) + 1);
}

void Mesh::SetMaterialRefName(const char* p)
{
	copyString(m_mesh.materialRefName, sizeof(m_mesh.materialRefName), p, strlen(p));
}

// -------------------------------------------------------------- Skeleton
uint32_t Skeleton::SearchBone(const char* name) const
{
	for (uint32_t i = 0; i < m_bones.size(); i++)
	{
		if (strcmp(m_bones[i].name, name) == 0)
		{
			return i;
		}
	}
	return UINT32_MAX;
}

vmdf::Matrix4x4 Skeleton::GetParentMatrix(vmdf::Bone& bone)
{
	vmdf::Matrix4x4 mMat = bone.transform;
	if (bone.parentName[0])
	{
		uint32_t uIndex = SearchBone(bone.parentName);
		if (uIndex == UINT32_MAX)
		{
			fatal("bone %s has an unknown parent %s", bone.name, bone.parentName);
		}
		mMat = usgm::Mul(mMat, GetParentMatrix(m_bones.at(uIndex)));
	}
	return mMat;
}

void Skeleton::FillOutBindPoseMatrix(vmdf::Bone& bone)
{
	bone.bindPoseTrans = GetParentMatrix(bone);
	usgm::GetInverse(bone.bindPoseTrans, bone.invBindPoseTrans);
}

void Skeleton::ReverseCoordinate()
{
	for (auto& bone : m_bones)
	{
		bone.translate.x *= -1.0f;
		bone.rotate.y *= -1.0f;
		bone.rotate.z *= -1.0f;
		usgm::SetupTransformMatrix(bone.transform, bone.scale, bone.rotate, bone.translate);
		bone.boundingSphere.center.x *= -1.0f;
	}
	for (auto& bone : m_bones)
	{
		FillOutBindPoseMatrix(bone);
	}
}

// -------------------------------------------------------------- Material
static const char* szPassExtensions[vmdf::Material_RenderPass_COUNT] = { nullptr, "deferred", "translucent", "depth", "omni_depth" };

void Material::InitCustomMaterial(const std::string& effectPath, const char* szEffectName, const std::vector<std::string>& defines)
{
	bool bPassFound = false;
	// The pak name is the effect file's name without ".yml", whichever
	// separator the path uses (Ayataka tested the effect name for '/'
	// instead of the path, so forward-slash paths kept the whole path).
	std::string pakName = pathStem(effectPath);
	for (uint32_t pass = 0; pass < vmdf::Material_RenderPass_COUNT; pass++)
	{
		std::vector<std::string> passDefines;
		if (szPassExtensions[pass])
		{
			passDefines.push_back(szPassExtensions[pass]);
		}
		for (const auto& d : defines)
		{
			if (pass <= vmdf::Material_RenderPass_DEPTH || d == "skel")
			{
				passDefines.push_back(d);
			}
		}
		// Ayataka: every pass loads the effect with the unfiltered defines
		if (m_materialDef[pass].Load(effectPath, szEffectName, defines) >= 0)
		{
			for (uint32_t i = 0; i < m_materialDef[pass].GetConstantSetCount(); i++)
			{
				AddConstantSet(m_materialDef[pass].GetConstantSetName(i), pass, m_materialDef[pass].GetConstantSetSize(i));
				m_materialDef[pass].CopyDefaultData(i, GetConstantSetData(pass, i));
			}
			std::string name = szEffectName;
			for (const auto& d : passDefines)
			{
				if (!d.empty())
				{
					name += ".";
					name += d;
				}
			}
			std::string full = pakName + "." + name;
			if (full.size() >= sizeof(m_material.renderPasses[pass].effectName))
			{
				fatal("effect name %s is too long for the material format", full.c_str());
			}
			setField(m_material.renderPasses[pass].effectName, full);
			bPassFound = true;
		}
		else
		{
			memset(m_material.renderPasses[pass].effectName, 0, sizeof(m_material.renderPasses[pass].effectName));
		}
	}
	if (!bPassFound)
	{
		std::string defineString;
		for (const auto& d : defines)
		{
			defineString += d;
		}
		fatal("no passes found for %s with defines %s", szEffectName, defineString.c_str());
	}

	for (uint32_t i = 0; i < m_materialDef[0].GetTextureCount(); i++)
	{
		uint32_t uDstIndex = 0;
		m_materialDef[0].GetTextureIndex(i, uDstIndex);
		if (uDstIndex >= vmdf::Material::textures_max_count || i >= vmdf::Material::textureCoordinators_max_count)
		{
			fatal("effect %s has more samplers than the material format holds", szEffectName);
		}
		vmdf::Texture& tex = m_material.textures[uDstIndex];
		vmdf::TextureCoordinator& texCo = m_material.textureCoordinators[i];
		texCo.sourceCoordinate = 0;
		texCo.scale = usgm::V2(1.0f, 1.0f);
		texCo.translate = usgm::V2(0.0f, 0.0f);
		texCo.rotate = 0.0f;
		texCo.method = vmdf::TextureCoordinator_MappingMethod_UV_COORDINATE;
		m_materialDef[0].GetSamplerDefaults(i, tex.minFilter, tex.magFilter, tex.mipFilter, tex.anisoLevel, tex.lodBias, tex.lodMinLevel);
	}
	m_material.textureCoordinators_count = m_materialDef[0].GetVariableCount("mTexMatrix");
}

void Material::AddConstantSet(const char* szName, uint32_t uPass, uint32_t uSize)
{
	vmdf::RenderPassData& pass = m_material.renderPasses[uPass];
	uint32_t uIndex = pass.constants_count;
	if (uIndex >= vmdf::RenderPassData::constants_max_count)
	{
		fatal("too many constant sets in a render pass");
	}
	uint32_t uOffset = 0;
	// Ayataka: sums every pass up to this one, one entry past each count
	// (those entries are always still zero)
	for (uint32_t i = 0; i <= uPass; i++)
	{
		for (uint32_t j = 0; j <= m_material.renderPasses[i].constants_count && j < vmdf::RenderPassData::constants_max_count; j++)
		{
			uOffset += m_material.renderPasses[i].constants[j].uSize;
		}
	}
	pass.constants[uIndex].uOffset = uOffset;
	setField(pass.constants[uIndex].bufferName, szName);
	pass.constants[uIndex].uSize = uSize;
	if (uOffset + uSize >= sizeof(m_material.constantData))
	{
		fatal("material constant data overflows the material format");
	}
	pass.constants_count++;
}

void* Material::GetConstantSetData(uint32_t uPass, uint32_t uSet)
{
	return ((uint8_t*)m_material.constantData) + m_material.renderPasses[uPass].constants[uSet].uOffset;
}

} // namespace exchange

// ------------------------------------------------------------------ Cmdl
void Cmdl::ReverseCoordinate()
{
	for (auto& s : shapes)
	{
		ReverseCoordinateInt(s.get());
	}
	for (auto& l : lights)
	{
		l.position.x *= -1.0f;
		l.direction.x *= -1.0f;
	}
	for (auto& c : cameras)
	{
		c.position.x *= -1.0f;
		c.rotate.y *= -1.0f;
		c.rotate.z *= -1.0f;
	}
	if (skeleton)
	{
		skeleton->ReverseCoordinate();
	}
}

void Cmdl::ReverseCoordinateInt(exchange::Stream& stream)
{
	if (!stream.IsAllocated())
	{
		return;
	}
	size_t step = stream.GetColumnNum();
	float* p = (float*)stream.GetStreamArrayPtr();
	size_t length = stream.GetSize() / sizeof(float) / step;
	for (size_t i = 0; i < length; ++i)
	{
		p[i * step] *= -1.0f;
	}
}

void Cmdl::ReverseCoordinateInt(exchange::Shape* pShape)
{
	for (uint32_t i = 0; i < pShape->GetVertexStreamNum(); ++i)
	{
		const vmdf::VertexStreamInfo& info = pShape->GetVertexStreamInfo(i);
		if (info.attribute == vmdf::VertexAttribute_POSITION || info.attribute == vmdf::VertexAttribute_NORMAL ||
		    info.attribute == vmdf::VertexAttribute_TANGENT || info.attribute == vmdf::VertexAttribute_BINORMAL)
		{
			ReverseCoordinateInt(*streams.at(info.refIndex));
		}
	}

	bool legacy = g_options.legacyBounds;
	uint32_t attrIndex;
	const uint32_t singles[] = { vmdf::VertexAttribute_NORMAL, vmdf::VertexAttribute_TANGENT, vmdf::VertexAttribute_BINORMAL };
	for (uint32_t a : singles)
	{
		if ((attrIndex = pShape->SearchSingleAttribute(a)) != UINT32_MAX)
		{
			vmdf::SingleAttribute& attr = pShape->pb().singleAttributes[attrIndex];
			if (legacy)
				attr.value.z *= -1.0f;
			else
				attr.value.x *= -1.0f;
		}
	}

	vmdf::Vector3f mn = pShape->GetAABBMin();
	vmdf::Vector3f mx = pShape->GetAABBMax();
	if (legacy)
	{
		// Ayataka before 513d3feb: swapped the Z extents without negating
		float newMaxZ = mn.z;
		float newMinZ = mx.z;
		mn.z = newMaxZ;
		mx.z = newMinZ;
		pShape->SetAABB(mn, mx);
		pShape->pb().boundingSphere.center.z *= -1.0f;
	}
	else
	{
		// The streams are mirrored in X, so mirror the bounds the same way
		float newMinX = -mx.x;
		float newMaxX = -mn.x;
		mn.x = newMinX;
		mx.x = newMaxX;
		pShape->SetAABB(mn, mx);
		pShape->pb().boundingSphere.center.x *= -1.0f;
	}
}

void Cmdl::FlipUV()
{
	for (auto& s : shapes)
	{
		for (uint32_t i = 0; i < s->GetVertexStreamNum(); ++i)
		{
			const vmdf::VertexStreamInfo& info = s->GetVertexStreamInfo(i);
			if (info.attribute == vmdf::VertexAttribute_UV)
			{
				exchange::Stream* pUV = streams.at(info.refIndex).get();
				float* p = (float*)pUV->GetStreamArrayPtr();
				for (uint32_t j = 1; j < pUV->GetLength(); j += 2)
				{
					p[j] = 1.0f - p[j];
				}
			}
		}
	}
}

// ---------------------------------------------------- DependencyTracker
void DependencyTracker::StartRecord(const std::string& dependencyFile, const std::string& outputFile)
{
	m_dependencyFile = dependencyFile;
	m_outputFile = toForwardSlashes(outputFile);
}

void DependencyTracker::LogDependency(const std::string& dependency)
{
	std::string dep = toForwardSlashes(dependency);
	for (const auto& d : m_dependencies)
	{
		if (d == dep)
		{
			return;
		}
	}
	m_dependencies.push_back(dep);
}

bool DependencyTracker::ExportDependencies() const
{
	if (m_dependencyFile.empty())
	{
		return true;
	}
	std::string text = m_outputFile + ":";
	for (const auto& d : m_dependencies)
	{
		text += " \\\n";
		text += d;
	}
	return writeFile(m_dependencyFile, text.data(), text.size());
}
