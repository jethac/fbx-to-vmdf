/****************************************************************************
//	fbx-to-vmdf
//	Port of Ayataka's fbx/FbxLoad.cpp and fbx/FbxConverter.cpp (Usagi
//	Engine, Copyright (c) Vitei, Inc. 2013-2018, MIT licence) onto the
//	ufbx-backed fbx::Scene. Comments marked "Ayataka:" flag behaviour kept
//	only because the output depends on it.
*****************************************************************************/
#include "Loader.h"
#include "FbxScene.h"
#include "MaterialOverrides.h"
#include "Options.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

FILE* g_probe = nullptr;

using fbxm::AMatrix;
using fbxm::Vec4;

vmdf::Matrix4x4 ToMat4x4(const AMatrix& in)
{
	vmdf::Matrix4x4 m;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			m.M[i][j] = (float)in.Get(i, j);
	return m;
}

// One vertex attribute as Ayataka's FBXVertexElement gathers it. The
// 16-byte value union matters for de-duplication: Ayataka compared CRCs
// of the whole struct, unused lanes included.
struct VertexElement
{
	char hint[32];
	uint32_t type;
	uint32_t eElementType;
	uint32_t uIndex;
	uint32_t uCount;
	union
	{
		float elements[4];
		uint8_t uElements[4];
	};

	VertexElement(const char* inHint, uint32_t inType, uint32_t eType, uint32_t inIndex, uint32_t inCount)
	{
		setField(hint, inHint);
		memset(elements, 0, sizeof(elements));
		type = inType;
		eElementType = eType;
		uIndex = inIndex;
		uCount = inCount;
	}

	void Transform(const AMatrix& matrix, double fW = 0.0)
	{
		Vec4 vec(elements[0], elements[1], elements[2], fW);
		vec = matrix.MultT(vec);
		fbxm::Normalize(vec);
		for (uint32_t i = 0; i < uCount && i < 4; i++)
			elements[i] = (float)vec[(int)i];
	}

	// value identity used for de-duplication (hint text is the same for
	// every vertex of a mesh and is left out)
	void AppendKey(std::string& key) const
	{
		key.append((const char*)&type, 4);
		key.append((const char*)&eElementType, 4);
		key.append((const char*)&uIndex, 4);
		key.append((const char*)&uCount, 4);
		key.append((const char*)elements, 16);
	}
};

struct TempVertex
{
	std::vector<VertexElement> elements;
	uint32_t controlPointIndex = 0;
	std::string key;
	void CalculateKey()
	{
		key.assign((const char*)&controlPointIndex, 4);
		for (const auto& e : elements)
			e.AppendKey(key);
	}
};

struct BoneWeight
{
	int index;
	float fValue;
	bool operator<(const BoneWeight& rhs) const { return fValue < rhs.fValue; }
};

struct BoneInfo
{
	uint32_t uOriginalMapping = 0;
	uint32_t uNewMapping = 0;
	char szName[40] = {};
};

// Ayataka's Vector3 (common/Vector.h) for the shape bounds
inline float calcLength(const vmdf::Vector3f& v) { return det::sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z)); }

// LoaderUtil::uniteSphere
void uniteSphere(vmdf::Vector3f& centerOut, float& radiusOut, const vmdf::Vector3f& centerA, float radiusA, const vmdf::Vector3f& centerB, float radiusB)
{
	using namespace usgm;
	if (centerA == centerB)
	{
		centerOut = centerA;
		radiusOut = radiusA == radiusB ? radiusA : std::max(radiusA, radiusB);
		return;
	}
	vmdf::Vector3f sub = centerA - centerB;
	float magn = Magnitude(sub);
	if (magn + radiusB <= radiusA || radiusB == 0.0f)
	{
		centerOut = centerA;
		radiusOut = radiusA;
		return;
	}
	else if (magn + radiusA <= radiusB || radiusA == 0.0f)
	{
		centerOut = centerB;
		radiusOut = radiusB;
		return;
	}
	Normalise(sub);
	radiusOut = (magn + radiusA + radiusB) / 2.0f;
	centerOut = (centerB - (sub * radiusB)) + (sub * radiusOut);
}

class FbxLoad
{
public:
	void Load(Cmdl& cmdl, fbx::Scene* scene, bool bSkeletonOnly, bool bCollisionMode, bool bForceBinormCalc, DependencyTracker* pDependencies, MaterialOverrides* pOverrides);

private:
	void ReadMeshRecursive(Cmdl& cmdl, fbx::Node* pNode, bool bStatic);
	void ReadSkeleton(exchange::Skeleton* pSkeleton, fbx::Node* pRootNode);
	void ReadBonesRecursive(exchange::Skeleton* pSkeleton, fbx::Node* pNode);
	void ReadLightsRecursive(Cmdl& cmdl, fbx::Node* pNode);
	void ReadCamerasRecursive(Cmdl& cmdl, fbx::Node* pNode);
	void ReadDeformersRecursive(fbx::Node* pNode);

	void AddMesh(Cmdl& cmdl, exchange::Shape* pShape, fbx::Node* pNode, fbx::Mesh* mesh, bool bStatic);
	void GetUV(const fbx::Mesh* pMesh, int iCtrlPoint, int iTexUVIndex, int iUVLayer, VertexElement& outUV);
	bool GetColor(const fbx::Mesh* pMesh, int iCtrlPoint, int inColorId, VertexElement& outColor);
	bool GetVec3(const std::vector<fbx::LayerElement>& layers, int iCtrlPoint, int iVertex, VertexElement& out);
	uint32_t GetBlendWeightsAndIndices(Cmdl& cmdl, fbx::Mesh* currMesh);
	void RemoveDuplicateVertices();

	void AddMaterials(Cmdl& cmdl, fbx::Node* pNode, bool bSkinned);
	void AddMaterialTextures(const fbx::Material* pFBXMaterial, exchange::Material* pNewMaterial);
	bool GetTextureIndex(const char* szTexName, exchange::Material* pMaterial, uint32_t& uIndex);
	exchange::Material* NewMaterial(const fbx::Material* pFBXMaterial, bool bSkin);
	exchange::Material* DummyMaterial(bool bSkinned);
	bool SetDefaultMaterialVariables(const fbx::Material* pFBXMaterial, exchange::Material* pMaterial);
	void SetBoolBasedOnTexture(exchange::Material* pNewMaterial, const char* szTexName, const char* szBoolName);
	void SetRenderState(exchange::Material* pNewMaterial, bool bTransparent) const;

	void AddIdentityBone(exchange::Skeleton* pSkeleton);
	bool IsIdentityBoneRequired(fbx::Node* pRootNode);
	int GetParentBoneCountRecursive(fbx::Node* pNode, int count);
	void AddBone(exchange::Skeleton* pSkeleton, fbx::Node* pNode, bool bIsNeededRendering);
	void AddLight(Cmdl& cmdl, fbx::Node* pNode);
	void AddCamera(Cmdl& cmdl, fbx::Node* pNode);
	uint32_t FindBone(const exchange::Skeleton& skel, const char* szName);
	uint32_t FindBoneRenderingId(Cmdl& cmdl, const char* szName);
	void AddStreams(Cmdl& cmdl, exchange::Shape* pShape, fbx::Node* pNode, fbx::Mesh* pMesh);
	AMatrix GetCombinedMatrixForNode(fbx::Node* pNode, fbx::Node* pParent);
	AMatrix GetGlobalPoseMatrix(fbx::Node* pNode);
	AMatrix GetLocalPoseMatrix(Cmdl& cmdl, const AMatrix& globalPose, const char* szParentName);
	bool IsBone(fbx::Node* pNode);
	bool PostProcessSkeleton(Cmdl& cmdl);
	void SetupAdjacencyStream(Cmdl& cmdl);
	void PostProcessing(Cmdl& cmdl);
	void RegisterBoneUsage(Cmdl& cmdl, const char* szName, vmdf::SkinningType eSkinType);
	uint32_t FindBone(uint32_t uOriginalIndex, const std::vector<BoneInfo>& boneInfo);

	std::vector<BoneInfo> m_skinnedBoneIndices;
	std::vector<BoneInfo> m_rigidBoneIndices;
	std::vector<const fbx::SkinCluster*> m_clusters;
	std::vector<AMatrix> m_globalBonePoses;

	uint32_t m_uMeshMaterialOffset = 0;
	uint32_t m_uMeshMaterialTmp = 0;
	bool m_bHasNormalMap = false;
	bool m_bHasDefaultStaticBone = false;
	bool m_bCollisionMesh = false;
	bool m_bForceBinormalCalc = false;
	double m_appliedScale = 1.0;
	double m_fAttenScale = 1.0;
	fbx::Node* m_pParentBoneNode = nullptr;
	fbx::Scene* m_pScene = nullptr;
	std::vector<uint32_t> m_indicesTmp;
	std::vector<TempVertex> m_activeVerts;
	std::vector<std::vector<BoneWeight>> m_activeWeights;
	DependencyTracker* m_pDependencies = nullptr;
	MaterialOverrides* m_pOverrides = nullptr;

public:
	void SetAttenScale(double s) { m_fAttenScale = s; }
};

// ------------------------------------------------------------ skeleton

void FbxLoad::AddIdentityBone(exchange::Skeleton* pSkeleton)
{
	const char* pRootBoneName = m_pScene->name.c_str();
	vmdf::Skeleton& rSkeleton = pSkeleton->pb();
	setFieldTruncate(rSkeleton.rootBoneName, pRootBoneName);
	rSkeleton.bonesNum = 1;

	vmdf::Bone bone;
	exchange::Bone_init(&bone);
	bone.isNeededRendering = 0;
	bone.scale = usgm::V3(1.0f, 1.0f, 1.0f);
	bone.rotate = usgm::V3(0.0f, 0.0f, 0.0f);
	bone.translate = usgm::V3(0.0f, 0.0f, 0.0f);
	usgm::SetupTransformMatrix(bone.transform, bone.scale, bone.rotate, bone.translate);
	setFieldTruncate(bone.name, pRootBoneName);
	m_bHasDefaultStaticBone = true;
	pSkeleton->Bones().push_back(bone);
	m_globalBonePoses.push_back(AMatrix());
}

bool FbxLoad::IsBone(fbx::Node* pNode)
{
	if (pNode->attrType != fbx::eUnknown)
	{
		fbx::AttrType t = pNode->attrType;
		if (t == fbx::eSkeleton || t == fbx::eNull || t == fbx::eMarker || pNode->parent == m_pScene->GetRootNode())
		{
			if (t == fbx::eMesh && !pNode->meshes.empty() && pNode->meshes[0]->deformerCount > 0)
				return false;
			return true;
		}
	}
	return false;
}

int FbxLoad::GetParentBoneCountRecursive(fbx::Node* pNode, int count)
{
	for (fbx::Node* c : pNode->children)
		count = GetParentBoneCountRecursive(c, count);
	if (IsBone(pNode))
	{
		bool bHasParent = false;
		for (fbx::Node* p = pNode->parent; p; p = p->parent)
		{
			if (IsBone(p))
			{
				bHasParent = true;
				break;
			}
		}
		if (!bHasParent)
			count++;
	}
	return count;
}

bool FbxLoad::IsIdentityBoneRequired(fbx::Node* pNode)
{
	return GetParentBoneCountRecursive(pNode, 0) != 1;
}

AMatrix FbxLoad::GetGlobalPoseMatrix(fbx::Node* pNode)
{
	for (const fbx::BindPose& pose : m_pScene->BindPoses())
	{
		int idx = pose.Find(pNode);
		if (idx >= 0)
			return pose.entries[(size_t)idx].second;
	}
	for (const fbx::SkinCluster* c : m_clusters)
	{
		if (c->link == pNode)
			return c->transformLink;
	}
	return pNode->EvaluateGlobalTransform();
}

AMatrix FbxLoad::GetLocalPoseMatrix(Cmdl& cmdl, const AMatrix& globalPose, const char* szParentName)
{
	uint32_t boneIndex = cmdl.GetSkeleton()->SearchBone(szParentName);
	if (boneIndex != UINT32_MAX)
		return m_globalBonePoses[boneIndex].Inverse() * globalPose;
	return globalPose;
}

uint32_t FbxLoad::FindBone(const exchange::Skeleton& skel, const char* szName)
{
	uint32_t i = skel.SearchBone(szName);
	return i == UINT32_MAX ? 0 : i; // Ayataka: asserts, then uses bone 0
}

uint32_t FbxLoad::FindBoneRenderingId(Cmdl& cmdl, const char* szName)
{
	uint32_t index = 0;
	for (const vmdf::Bone& b : cmdl.GetSkeleton()->Bones())
	{
		if (b.isNeededRendering)
		{
			if (strcmp(b.name, szName) == 0)
				return index;
			index++;
		}
	}
	return 0;
}

void FbxLoad::AddBone(exchange::Skeleton* pSkeleton, fbx::Node* pNode, bool bIsNeededRendering)
{
	vmdf::Skeleton& rSkeleton = pSkeleton->pb();
	// Make the name unique (Ayataka renames the node itself)
	std::string name = pNode->name;
	int loopIdx = 0;
	for (;;)
	{
		bool unique = true;
		for (const vmdf::Bone& b : pSkeleton->Bones())
		{
			if (strcmp(b.name, name.c_str()) == 0)
			{
				unique = false;
				break;
			}
		}
		if (unique)
			break;
		char buf[300];
		snprintf(buf, sizeof(buf), "%s.%03i", pNode->name.c_str(), ++loopIdx);
		name = buf;
	}
	pNode->name = name;
	const char* pBoneName = pNode->name.c_str();

	fbx::Node* pParent = pNode->parent;
	while (pParent && !IsBone(pParent))
		pParent = pParent->parent;

	if (!pParent && !m_bHasDefaultStaticBone)
		setFieldTruncate(rSkeleton.rootBoneName, pBoneName);
	rSkeleton.bonesNum++;

	vmdf::Bone bone;
	exchange::Bone_init(&bone);
	bone.isNeededRendering = bIsNeededRendering ? 1 : 0;

	AMatrix globalPoseMatrix = GetGlobalPoseMatrix(pNode);
	AMatrix localPoseMatrix = globalPoseMatrix;

	if (pParent)
	{
		uint32_t iParentIdx = FindBone(*pSkeleton, pParent->name.c_str());
		setFieldTruncate(bone.parentName, pParent->name.c_str());
		localPoseMatrix = m_globalBonePoses[iParentIdx].Inverse() * localPoseMatrix;
		if (pNode->children.empty() && !bIsNeededRendering)
		{
			fbx::Node* Root = nullptr;
			for (fbx::Node* Tmp = pNode; Tmp->parent; Tmp = Tmp->parent)
			{
				if (IsBone(Tmp))
					Root = Tmp;
			}
			// Convenience hack for markers to match the orientation
			if (Root)
			{
				AMatrix RootPose = Root->EvaluateGlobalTransform();
				RootPose.SetT(Vec4(0.0, 0.0, 0.0, 1.0));
				localPoseMatrix = localPoseMatrix * RootPose.Inverse();
			}
		}
		bone.parentIndex = iParentIdx;
	}
	else
	{
		if (m_bHasDefaultStaticBone)
			setFieldTruncate(bone.parentName, rSkeleton.rootBoneName);
		else
			memset(bone.parentName, 0, sizeof(bone.parentName));
	}

	Vec4 scale = localPoseMatrix.GetS();
	Vec4 rotate = localPoseMatrix.GetR();
	Vec4 translate = localPoseMatrix.GetT();
	bone.scale = usgm::V3((float)scale[0], (float)scale[1], (float)scale[2]);
	bone.rotate = usgm::V3(usgm::DegreesToRadians((float)rotate[0]), usgm::DegreesToRadians((float)rotate[1]), usgm::DegreesToRadians((float)rotate[2]));
	bone.translate = usgm::V3((float)(translate[0] * m_appliedScale), (float)(translate[1] * m_appliedScale), (float)(translate[2] * m_appliedScale));
	usgm::SetupTransformMatrix(bone.transform, bone.scale, bone.rotate, bone.translate);
	setFieldTruncate(bone.name, pBoneName);

	pSkeleton->Bones().push_back(bone);
	m_globalBonePoses.push_back(globalPoseMatrix);
}

void FbxLoad::ReadDeformersRecursive(fbx::Node* pNode)
{
	if (pNode->attrType == fbx::eMesh && !pNode->meshes.empty())
	{
		for (const fbx::Skin& s : pNode->meshes[0]->skins)
			for (const fbx::SkinCluster& c : s.clusters)
				m_clusters.push_back(&c);
	}
	for (fbx::Node* c : pNode->children)
		ReadDeformersRecursive(c);
}

void FbxLoad::ReadSkeleton(exchange::Skeleton* pSkeleton, fbx::Node* pRootNode)
{
	ReadDeformersRecursive(pRootNode);
	ReadBonesRecursive(pSkeleton, pRootNode);
}

void FbxLoad::ReadBonesRecursive(exchange::Skeleton* pSkeleton, fbx::Node* pNode)
{
	if (IsBone(pNode))
		AddBone(pSkeleton, pNode, pNode->attrType == fbx::eSkeleton || pNode->parent == m_pScene->GetRootNode());
	for (fbx::Node* c : pNode->children)
		ReadBonesRecursive(pSkeleton, c);
}

// --------------------------------------------------- lights and cameras

void FbxLoad::AddCamera(Cmdl& cmdl, fbx::Node* pNode)
{
	if (!pNode->camera)
		return;
	Cmdl::Camera cam;
	fbx::Node* pParentBone = pNode->parent;
	auto isAnchor = [](fbx::Node* n) { return n && (n->attrType == fbx::eSkeleton || n->attrType == fbx::eNull || n->attrType == fbx::eMarker); };
	while (pParentBone && !isAnchor(pParentBone))
		pParentBone = pParentBone->parent;
	cam.parentBone = isAnchor(pParentBone) ? pParentBone->name : std::string(cmdl.GetSkeleton()->pb().rootBoneName);

	AMatrix rot = AMatrix::Rotation(Vec4(0.0, 90.0, 0.0));
	AMatrix globalPoseMatrix = GetGlobalPoseMatrix(pNode) * rot;
	AMatrix localPoseMatrix = GetLocalPoseMatrix(cmdl, globalPoseMatrix, cam.parentBone.c_str());
	Vec4 rotate = localPoseMatrix.GetR();
	Vec4 translate = localPoseMatrix.GetT();
	cam.name = pNode->name;
	cam.fov = (float)pNode->camera->fieldOfViewY;
	cam.nearPlane = (float)(pNode->camera->nearPlane * m_appliedScale);
	cam.farPlane = (float)(pNode->camera->farPlane * m_appliedScale);
	cam.rotate = usgm::V3(usgm::DegreesToRadians((float)rotate[0]), usgm::DegreesToRadians((float)rotate[1]), usgm::DegreesToRadians((float)rotate[2]));
	cam.position = usgm::V3((float)(translate[0] * m_appliedScale), (float)(translate[1] * m_appliedScale), (float)(translate[2] * m_appliedScale));
	cmdl.cameras.push_back(cam);
}

void FbxLoad::AddLight(Cmdl& cmdl, fbx::Node* pNode)
{
	const fbx::Light* pFBXLight = pNode->light.get();
	if (!pFBXLight)
		return;
	Cmdl::Light light;
	fbx::Node* pParentBone = pNode;
	while (pParentBone && !IsBone(pParentBone))
		pParentBone = pParentBone->parent;
	light.parentBone = (pParentBone && IsBone(pParentBone)) ? pParentBone->name : std::string(cmdl.GetSkeleton()->pb().rootBoneName);

	AMatrix mGeometry = GetLocalPoseMatrix(cmdl, GetGlobalPoseMatrix(pNode), light.parentBone.c_str());
	vmdf::Matrix4x4 mMatUsg = ToMat4x4(mGeometry);
	// Lights face down by default whereas forward is x
	vmdf::Matrix4x4 mRot;
	usgm::MakeRotAboutAxis(mRot, usgm::V4(1.0f, 0.0f, 0.0f, 0.0f), -usgm::pi_over_2);
	mMatUsg = usgm::Mul(mRot, mMatUsg);

	light.name = pNode->name;
	vmdf::Vector3f face = usgm::V3(mMatUsg.M[2][0], mMatUsg.M[2][1], mMatUsg.M[2][2]);
	vmdf::Vector3f pos = usgm::V3(mMatUsg.M[3][0], mMatUsg.M[3][1], mMatUsg.M[3][2]);
	switch (pFBXLight->type)
	{
	case 2: // eSpot
		light.kind = vmdf::LightKind_SPOT;
		light.innerAngle = (float)pFBXLight->innerAngle / 2.f;
		light.outerAngle = (float)pFBXLight->outerAngle / 2.f;
		light.direction = usgm::GetNormalised(face);
		light.position = usgm::operator*(pos, (float)m_appliedScale);
		break;
	case 0: // ePoint
		light.kind = vmdf::LightKind_POINT;
		light.position = usgm::operator*(pos, (float)m_appliedScale);
		break;
	default:
		return; // unhandled
	}
	float fIntensity = 1.0f;
	float c[3] = { (float)pFBXLight->color[0], (float)pFBXLight->color[1], (float)pFBXLight->color[2] };
	for (int i = 0; i < 3; i++)
	{
		light.ambient.rgba[i] = c[i] * fIntensity * 0.05f;
		light.diffuse.rgba[i] = c[i] * fIntensity * 1.0f;
		light.specular.rgba[i] = c[i] * fIntensity * 1.0f;
	}
	light.ambient.rgba[3] = 1.0f * fIntensity * 0.05f;
	light.diffuse.rgba[3] = light.specular.rgba[3] = 1.0f * fIntensity;
	light.bShadow = true;

	float fFarEnd = 100.f;
	float fAttenuationStart = 0.0f;
	if (pFBXLight->enableNearAttenuation)
		fAttenuationStart = (float)pFBXLight->nearAttenuationStart;
	if (pFBXLight->enableFarAttenuation)
	{
		fFarEnd = (float)pFBXLight->farAttenuationEnd;
	}
	else
	{
		fAttenuationStart = std::max((float)pFBXLight->decayStart, fAttenuationStart);
		if (pFBXLight->decayType >= 0 && pFBXLight->decayType <= 3)
		{
			fFarEnd = fAttenuationStart;
			fAttenuationStart *= 0.5f;
		}
		fFarEnd += fAttenuationStart;
	}
	light.attenEnabled = pFBXLight->type != 1; // not eDirectional
	light.attenNear = fAttenuationStart * (float)m_fAttenScale;
	light.attenFar = fFarEnd * (float)m_fAttenScale;
	cmdl.lights.push_back(light);
}

void FbxLoad::ReadCamerasRecursive(Cmdl& cmdl, fbx::Node* pNode)
{
	if (pNode->attrType == fbx::eCamera || pNode->attrType == fbx::eCameraStereo)
		AddCamera(cmdl, pNode);
	for (fbx::Node* c : pNode->children)
		ReadCamerasRecursive(cmdl, c);
}

void FbxLoad::ReadLightsRecursive(Cmdl& cmdl, fbx::Node* pNode)
{
	if (pNode->attrType == fbx::eLight)
		AddLight(cmdl, pNode);
	for (fbx::Node* c : pNode->children)
		ReadLightsRecursive(cmdl, c);
}

// ----------------------------------------------------------- materials

bool FbxLoad::GetTextureIndex(const char* szTexName, exchange::Material* pMaterial, uint32_t& uIndex)
{
	return pMaterial->GetCustomFX(0).GetTextureIndex(szTexName, uIndex) && pMaterial->pb().textures[uIndex].textureName[0] == '\0';
}

void FbxLoad::AddMaterialTextures(const fbx::Material* pFBXMaterial, exchange::Material* pNewMaterial)
{
	// Ayataka: tests FbxSurfaceMaterial::sNormalMap (a non-null string
	// constant), so any mesh with a material counts as normal mapped
	m_bHasNormalMap = true;
	for (const fbx::Material::Texture& t : pFBXMaterial->textures)
	{
		if (t.layered)
		{
			warning("layered texture on %s ignored (Ayataka doesn't support them)", pFBXMaterial->name.c_str());
			continue;
		}
		uint32_t uTexIndex = 0;
		if (!GetTextureIndex(t.property.c_str(), pNewMaterial, uTexIndex))
			continue;
		std::string textName = t.relativeFileName;
		size_t ext = textName.rfind('.');
		textName = textName.substr(0, ext == std::string::npos ? textName.size() : ext);
		size_t pos = textName.find_last_of("\\/");
		if (pos != std::string::npos)
			textName = textName.substr(pos + 1);
		vmdf::Texture& tex = pNewMaterial->pb().textures[uTexIndex];
		if (textName.size() >= sizeof(tex.textureName))
		{
			printf("Texture name %s too long\n", textName.c_str());
			continue;
		}
		setField(tex.textureName, textName);
		tex.wrapS = t.wrapU == 1 ? vmdf::SAMP_WRAP_CLAMP : vmdf::SAMP_WRAP_REPEAT;
		tex.wrapT = t.wrapV == 1 ? vmdf::SAMP_WRAP_CLAMP : vmdf::SAMP_WRAP_REPEAT;
		setField(tex.textureHint, t.property);
		vmdf::TextureCoordinator& texCo = pNewMaterial->pb().textureCoordinators[uTexIndex];
		texCo.sourceCoordinate = 0;
		texCo.method = vmdf::TextureCoordinator_MappingMethod_UV_COORDINATE;
		texCo.scale = usgm::V2((float)t.scale[0], (float)t.scale[1]);
		texCo.translate = usgm::V2((float)t.translation[0], (float)t.translation[1]);
		texCo.rotate = (float)t.rotation;
	}
}

void FbxLoad::SetBoolBasedOnTexture(exchange::Material* pNewMaterial, const char* szTexName, const char* szBoolName)
{
	uint32_t uIndex;
	if (pNewMaterial->GetCustomFX(0).GetTextureIndex(szTexName, uIndex) && pNewMaterial->pb().textures[uIndex].textureName[0] != '\0')
		pNewMaterial->SetVariable(szBoolName, true);
}

static void setDefaultTextureNames(exchange::Material* pMaterial, bool onlyEmpty)
{
	for (uint32_t i = 0; i < pMaterial->GetCustomFX(0).GetTextureCount(); i++)
	{
		uint32_t uTexIndex = i;
		pMaterial->GetCustomFX(0).GetTextureIndex(i, uTexIndex);
		vmdf::Texture& tex = pMaterial->pb().textures[uTexIndex];
		if (onlyEmpty && tex.textureName[0] != '\0')
			continue;
		const char* szTextName = pMaterial->GetCustomFX(0).GetDefaultTexName(i);
		size_t length = std::min(strlen(szTextName), sizeof(tex.textureName));
		memset(tex.textureName, 0, sizeof(tex.textureName));
		memcpy(tex.textureName, szTextName, length);
	}
}

bool FbxLoad::SetDefaultMaterialVariables(const fbx::Material* m, exchange::Material* pMaterial)
{
	SetBoolBasedOnTexture(pMaterial, "DiffuseColor", "bDiffuseMap");
	SetBoolBasedOnTexture(pMaterial, "NormalMap", "bBumpMap");
	SetBoolBasedOnTexture(pMaterial, "SpecularColor", "bSpecMap");
	SetBoolBasedOnTexture(pMaterial, "EmissiveFactor", "bEmissiveMap");
	SetBoolBasedOnTexture(pMaterial, "Reflection", "bReflectionMap");
	setDefaultTextureNames(pMaterial, true);

	bool bTransparent = false;
	if (m->isLambert)
	{
		float ambient[4] = { (float)m->ambient[0], (float)m->ambient[1], (float)m->ambient[2], 1.0f };
		pMaterial->SetVariableArray("ambient", ambient, 4);
		float diffuse[4] = { (float)m->diffuse[0], (float)m->diffuse[1], (float)m->diffuse[2], 1.0f };
		bool bHasDiffuse = false;
		pMaterial->GetVariable("bDiffuseMap", &bHasDiffuse);
		if (bHasDiffuse)
		{
			diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] = 1.0f;
		}
		pMaterial->SetVariableArray("diffuse", diffuse, 4);
		// Ignoring 3 channel transparency, so just taking the green
		float transparency = (float)(m->transparencyFactor * m->transparentColor[1]);
		pMaterial->SetVariable("alpha", 1.0f - transparency);
		bTransparent = transparency > 0.0f;
	}
	if (m->isPhong)
	{
		float specular[4] = { (float)m->specular[0], (float)m->specular[1], (float)m->specular[2], 1.0f };
		double shininess = m->shininess;
		pMaterial->SetVariable("specularpow", (float)std::max(shininess, 1.0));
		bool bHasSpecMap = false;
		pMaterial->GetVariable("bSpecMap", &bHasSpecMap);
		static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		if (bHasSpecMap)
			pMaterial->SetVariableArray("specular", white, 4);
		else if (shininess > 0.01)
			pMaterial->SetVariableArray("specular", specular, 4);
		else
			pMaterial->SetVariableArray("specular", black, 4);
		float reflection[4] = { (float)m->reflection[0], (float)m->reflection[1], (float)m->reflection[2], 1.0f };
		pMaterial->SetVariableArray("reflection", reflection, 4);
		pMaterial->SetVariable("reflectionfactor", (float)m->reflectionFactor);
	}
	return bTransparent;
}

void FbxLoad::SetRenderState(exchange::Material* pNewMaterial, bool bTransparent) const
{
	vmdf::Rasterizer& r = pNewMaterial->pb().rasterizer;
	r.cullFace = vmdf::CULL_FACE_BACK;
	r.blendEnabled = 0;
	r.alphaState.rgbOp = vmdf::BLEND_EQUATION_ADD;
	r.alphaState.alphaOp = vmdf::BLEND_EQUATION_ADD;
	if (bTransparent)
	{
		r.blendEnabled = 1;
		r.alphaState.rgbSrcFunc = vmdf::BLEND_FUNC_SRC_ALPHA;
		r.alphaState.rgbDestFunc = vmdf::BLEND_FUNC_ONE_MINUS_SRC_ALPHA;
		r.alphaState.alphaSrcFunc = vmdf::BLEND_FUNC_ONE;
		r.alphaState.alphaDestFunc = vmdf::BLEND_FUNC_ZERO;
	}
	turnOnFlag(r.attribute, 1u << vmdf::Rasterizer_Attribute_DEPTH_TEST_ENABLE);
	turnOnFlag(r.attribute, 1u << vmdf::Rasterizer_Attribute_DEPTH_TEST_WRITE);
	r.depthTestFunc = vmdf::DEPTH_TEST_LEQUAL;
}

static void setMaterialName(exchange::Material* pMat, const std::string& name)
{
	if (name.size() >= sizeof(pMat->pb().materialName))
		fatal("material name %s is too long (39 characters at most)", name.c_str());
	setField(pMat->pb().materialName, name);
}

exchange::Material* FbxLoad::NewMaterial(const fbx::Material* pFBXMaterial, bool bSkin)
{
	exchange::Material* pNewMaterial = new exchange::Material();
	std::vector<std::string> defines;
	if (bSkin)
		defines.push_back("skel");
	for (const fbx::Material::Texture& t : pFBXMaterial->textures)
	{
		if (t.property == "NormalMap")
		{
			defines.push_back("bump");
			break;
		}
	}
	setMaterialName(pNewMaterial, pFBXMaterial->name);
	m_pOverrides->InitDefault(pNewMaterial->pb().materialName, defines, pNewMaterial);
	AddMaterialTextures(pFBXMaterial, pNewMaterial);
	bool bTransparent = SetDefaultMaterialVariables(pFBXMaterial, pNewMaterial);
	SetRenderState(pNewMaterial, bTransparent);
	pNewMaterial->pb().attribute.translucencyKind = bTransparent ? vmdf::Translucency_Type_TRANSLUCENCY_TRANSLUCENT : vmdf::Translucency_Type_TRANSLUCENCY_OPAQUE;
	return pNewMaterial;
}

exchange::Material* FbxLoad::DummyMaterial(bool bSkinned)
{
	exchange::Material* pNewMaterial = new exchange::Material();
	const char* szUsagiPath = getenv("USAGI_DIR");
	std::vector<std::string> defines;
	if (bSkinned)
		defines.push_back("skel");
	// Ayataka logs this path whether or not it's the effect file used
	if (szUsagiPath)
		m_pDependencies->LogDependency(std::string(szUsagiPath) + "/Data/GLSL/effects/Model.yml");
	setMaterialName(pNewMaterial, "Dummy");
	m_pOverrides->InitDefault(pNewMaterial->pb().materialName, defines, pNewMaterial);
	setDefaultTextureNames(pNewMaterial, false);
	vmdf::Color white = { { 1.0f, 1.0f, 1.0f, 1.0f } };
	pNewMaterial->SetVariable("ambient", white);
	pNewMaterial->SetVariable("diffuse", white);
	pNewMaterial->SetVariable("emission", white);
	pNewMaterial->SetVariable("alpha", 1.0f);
	pNewMaterial->pb().attribute.translucencyKind = vmdf::Translucency_Type_TRANSLUCENCY_OPAQUE;
	SetRenderState(pNewMaterial, false);
	return pNewMaterial;
}

void FbxLoad::AddMaterials(Cmdl& cmdl, fbx::Node* pNode, bool bSkinned)
{
	m_bHasNormalMap = false;
	for (int i = 0; i < pNode->GetMaterialCount(); ++i)
		cmdl.materials.emplace_back(NewMaterial(pNode->GetMaterial(i), bSkinned));
	if (pNode->GetMaterialCount() == 0)
		cmdl.materials.emplace_back(DummyMaterial(bSkinned));
}

// --------------------------------------------------------------- meshes

AMatrix FbxLoad::GetCombinedMatrixForNode(fbx::Node* pNode, fbx::Node* pParent)
{
	if (pParent != nullptr)
	{
		AMatrix mWorld = pNode->EvaluateGlobalTransform();
		AMatrix mSkeletonWorld = pParent->EvaluateGlobalTransform();
		return mSkeletonWorld.Inverse() * mWorld;
	}
	AMatrix mGeometry = AMatrix::TRS(pNode->GetGeometricTranslation(), pNode->GetGeometricRotation(), pNode->GetGeometricScaling());
	return pNode->EvaluateGlobalTransform() * mGeometry;
}

bool FbxLoad::GetVec3(const std::vector<fbx::LayerElement>& layers, int iCtrlPoint, int iVertex, VertexElement& out)
{
	if (layers.empty())
		return false;
	const fbx::LayerElement& le = layers[0];
	double v[4] = { 0, 0, 0, 0 };
	if (le.mapping != fbx::eByControlPoint && le.mapping != fbx::eByPolygonVertex)
		return true; // Ayataka: unhandled mapping leaves the previous value
	if (!le.Get(iCtrlPoint, iVertex, v))
		return false;
	out.elements[0] = (float)v[0];
	out.elements[1] = (float)v[1];
	out.elements[2] = (float)v[2];
	return true;
}

void FbxLoad::GetUV(const fbx::Mesh* pMesh, int iCtrlPoint, int iTexUVIndex, int inUVLayer, VertexElement& outUV)
{
	if ((int)pMesh->uvs.size() <= inUVLayer)
		return;
	const fbx::LayerElement& vertexUV = pMesh->uvs[(size_t)inUVLayer];
	double uv[2] = { 0, 0 };
	switch (vertexUV.mapping)
	{
	case fbx::eByControlPoint:
		if (vertexUV.reference == fbx::eDirect)
			vertexUV.GetDirect(iCtrlPoint, uv);
		else if (vertexUV.reference == fbx::eIndexToDirect)
			vertexUV.GetDirect(iCtrlPoint >= 0 && (size_t)iCtrlPoint < vertexUV.index.size() ? vertexUV.index[(size_t)iCtrlPoint] : -1, uv);
		else
			return;
		break;
	case fbx::eByPolygonVertex:
		if (vertexUV.reference != fbx::eDirect && vertexUV.reference != fbx::eIndexToDirect)
			return;
		// Ayataka: indexes the direct array with the "texture UV index",
		// which is -1 for direct UVs (the SDK then returns zeros)
		vertexUV.GetDirect(iTexUVIndex, uv);
		break;
	default:
		return;
	}
	outUV.elements[0] = (float)uv[0];
	outUV.elements[1] = 1.0f - (float)uv[1];
}

bool FbxLoad::GetColor(const fbx::Mesh* pMesh, int iCtrlPoint, int inColorId, VertexElement& outColor)
{
	if ((int)pMesh->colors.size() <= inColorId)
		return false;
	const fbx::LayerElement& vc = pMesh->colors[0];
	double c[4] = { 0, 0, 0, 0 };
	switch (vc.mapping)
	{
	case fbx::eByControlPoint:
		if (vc.reference == fbx::eDirect)
			vc.GetDirect(iCtrlPoint, c);
		else if (vc.reference == fbx::eIndexToDirect)
			vc.GetDirect(iCtrlPoint >= 0 && (size_t)iCtrlPoint < vc.index.size() ? vc.index[(size_t)iCtrlPoint] : -1, c);
		else
			return false;
		break;
	case fbx::eByPolygonVertex:
		if (vc.reference != fbx::eDirect && vc.reference != fbx::eIndexToDirect)
			return false;
		// Ayataka: reads entry `inColorId` (0) for every polygon vertex
		vc.GetDirect(inColorId, c);
		break;
	default:
		return true;
	}
	for (int i = 0; i < 4; i++)
		outColor.elements[i] = (float)c[i];
	return true;
}

uint32_t FbxLoad::GetBlendWeightsAndIndices(Cmdl& cmdl, fbx::Mesh* pCurrMesh)
{
	m_activeWeights.clear();
	uint32_t uMaxWeights = 0;
	if (pCurrMesh->deformerCount > 0)
		m_activeWeights.resize(pCurrMesh->controlPoints.size());
	for (const fbx::Skin& skin : pCurrMesh->skins)
	{
		for (const fbx::SkinCluster& c : skin.clusters)
		{
			uint32_t uJointIndex = FindBoneRenderingId(cmdl, c.link ? c.link->name.c_str() : "");
			BoneWeight weight;
			weight.index = (int)uJointIndex;
			for (size_t i = 0; i < c.indices.size(); ++i)
			{
				int cp = c.indices[i];
				if (cp < 0 || (size_t)cp >= m_activeWeights.size())
					continue;
				weight.fValue = i < c.weights.size() ? (float)c.weights[i] : 0.0f;
				if (skin.rigid)
					m_activeWeights[(size_t)cp].clear();
				m_activeWeights[(size_t)cp].push_back(weight);
			}
		}
	}
	for (auto& w : m_activeWeights)
	{
		if (w.size() > 4)
		{
			std::sort(w.begin(), w.end());
			w.resize(4);
			float fTotalWeight = 0.0f;
			for (const BoneWeight& b : w)
				fTotalWeight += b.fValue;
			for (BoneWeight& b : w)
				b.fValue /= fTotalWeight;
		}
		uMaxWeights = std::max((uint32_t)w.size(), uMaxWeights);
	}
	for (auto& w : m_activeWeights)
	{
		if (w.empty() && uMaxWeights > 0)
			w.push_back(BoneWeight{ 0, 1.0f });
		while (w.size() < uMaxWeights)
			w.push_back(BoneWeight{ 0, 0.0f });
	}
	return uMaxWeights;
}

void FbxLoad::AddMesh(Cmdl& cmdl, exchange::Shape* pShape, fbx::Node* pNode, fbx::Mesh* currMesh, bool bStatic)
{
	(void)pShape;
	(void)bStatic;
	m_activeVerts.clear();
	m_indicesTmp.clear();
	const uint32_t uTriangles = (uint32_t)currMesh->GetPolygonCount();
	m_indicesTmp.reserve((size_t)uTriangles * 3);
	m_activeVerts.reserve((size_t)uTriangles * 3);

	VertexElement position("position", vmdf::VertexAttribute_POSITION, vmdf::VE_FLOAT, 0, 3);
	VertexElement normal("normal", vmdf::VertexAttribute_NORMAL, vmdf::VE_FLOAT, 0, 3);
	VertexElement tangent("tangent", vmdf::VertexAttribute_TANGENT, vmdf::VE_FLOAT, 0, 3);
	VertexElement color("color", vmdf::VertexAttribute_COLOR, vmdf::VE_FLOAT, 0, 3);
	VertexElement binormal("binormal", vmdf::VertexAttribute_BINORMAL, vmdf::VE_FLOAT, 0, 3);
	VertexElement blendweight("blendweight", vmdf::VertexAttribute_BONE_WEIGHT, vmdf::VE_FLOAT, 0, 4);
	VertexElement blendindices("blendindex", vmdf::VertexAttribute_BONE_INDEX, vmdf::VE_UBYTE, 0, 4);
	VertexElement UV("UV0", vmdf::VertexAttribute_UV, vmdf::VE_FLOAT, 0, 2);

	AMatrix transform = GetCombinedMatrixForNode(pNode, m_pParentBoneNode);
	AMatrix normalTransform = transform.Inverse().Transpose();

	int materialIndex = 0;
	if (!currMesh->materials.empty())
	{
		const fbx::LayerElement& me = currMesh->materials[0];
		if (me.mapping != fbx::eAllSame)
			warning("mesh %s still has per-polygon materials after the split", pNode->name.c_str());
		materialIndex = me.index.empty() ? 0 : me.index[0];
	}
	m_uMeshMaterialTmp = (uint32_t)materialIndex + m_uMeshMaterialOffset;

	if (m_bHasNormalMap && (currMesh->tangents.empty() || m_bForceBinormalCalc))
		currMesh->GenerateTangentsData(0, m_bForceBinormalCalc);

	int iVertex = 0;
	const int uUVCount = (int)currMesh->uvs.size();
	for (uint32_t uTriangle = 0; uTriangle < uTriangles; ++uTriangle)
	{
		for (uint32_t uTriangleVert = 0; uTriangleVert < 3; ++uTriangleVert)
		{
			int iVertexIndex = currMesh->GetPolygonVertex((int)uTriangle, (int)uTriangleVert);
			Vec4 ctrlPoint = transform.MultT(currMesh->controlPoints[(size_t)iVertexIndex]);
			position.elements[0] = (float)(ctrlPoint[0] * m_appliedScale);
			position.elements[1] = (float)(ctrlPoint[1] * m_appliedScale);
			position.elements[2] = (float)(ctrlPoint[2] * m_appliedScale);

			TempVertex vertexOut;
			vertexOut.controlPointIndex = (uint32_t)iVertexIndex;
			vertexOut.elements.push_back(position);

			if (GetVec3(currMesh->normals, iVertexIndex, iVertex, normal))
			{
				normal.Transform(normalTransform, 0.0);
				vertexOut.elements.push_back(normal);
			}
			if (!m_bCollisionMesh)
			{
				if (GetVec3(currMesh->binormals, iVertexIndex, iVertex, binormal))
				{
					binormal.Transform(normalTransform, 0.0);
					vertexOut.elements.push_back(binormal);
				}
				if (GetVec3(currMesh->tangents, iVertexIndex, iVertex, tangent))
				{
					tangent.Transform(normalTransform, 0.0);
					vertexOut.elements.push_back(tangent);
				}
				if (GetColor(currMesh, iVertexIndex, 0, color))
					vertexOut.elements.push_back(color);
				for (int k = 0; k < uUVCount; ++k)
				{
					int iUVIndex = currMesh->GetTextureUVIndex((int)uTriangle, (int)uTriangleVert);
					const fbx::LayerElement& uvEl = currMesh->uvs[(size_t)k];
					if (!g_options.legacyUVIndex && k > 0 && uvEl.mapping == fbx::eByPolygonVertex)
					{
						// each UV set through its own index array (Ayataka 1d43dca8)
						const int iPolygonVertex = currMesh->GetPolygonVertexIndex((int)uTriangle) + (int)uTriangleVert;
						iUVIndex = uvEl.reference == fbx::eDirect ? iPolygonVertex
						         : ((size_t)iPolygonVertex < uvEl.index.size() ? uvEl.index[(size_t)iPolygonVertex] : -1);
					}
					GetUV(currMesh, iVertexIndex, iUVIndex, k, UV);
					setField(UV.hint, uvEl.name);
					vertexOut.elements.push_back(UV);
				}
			}
			m_activeVerts.push_back(vertexOut);
			m_indicesTmp.push_back((uint32_t)iVertex);
			++iVertex;
		}
	}

	uint32_t uMaxWeights = GetBlendWeightsAndIndices(cmdl, currMesh);
	bool bRigid = uMaxWeights == 1;
	if (uMaxWeights > 0)
		uMaxWeights = uMaxWeights < 4 ? 4 : uMaxWeights;
	if (uMaxWeights > 4)
		fatal("mesh %s has vertices with more than 4 bone weights after trimming", pNode->name.c_str());

	bool bNeedsBlendStream = false;
	for (const auto& w : m_activeWeights)
		for (const BoneWeight& b : w)
			if (b.index != 0)
				bNeedsBlendStream = true;

	for (TempVertex& v : m_activeVerts)
	{
		if (uMaxWeights > 0 && bNeedsBlendStream)
		{
			const std::vector<BoneWeight>& info = m_activeWeights[v.controlPointIndex];
			for (uint32_t i = 0; i < info.size() && i < 4; i++)
			{
				blendweight.elements[i] = info[i].fValue;
				blendindices.uElements[i] = (uint8_t)info[i].index;
			}
			for (uint32_t i = (uint32_t)info.size(); i < uMaxWeights; i++)
			{
				blendindices.uElements[i] = 0;
				blendweight.elements[i] = 0.0f;
			}
			blendindices.uCount = uMaxWeights;
			blendweight.uCount = uMaxWeights;
			v.elements.push_back(blendindices);
			if (!bRigid)
				v.elements.push_back(blendweight);
		}
		v.CalculateKey();
	}
}

void FbxLoad::RemoveDuplicateVertices()
{
	std::vector<TempVertex> uniqueVertices;
	std::map<std::string, uint32_t> uniqueMap;
	for (const TempVertex& v : m_activeVerts)
	{
		if (uniqueMap.find(v.key) == uniqueMap.end())
		{
			uniqueMap[v.key] = (uint32_t)uniqueVertices.size();
			uniqueVertices.push_back(v);
		}
	}
	for (size_t i = 0; i < m_activeVerts.size(); i++)
		m_indicesTmp[i] = uniqueMap[m_activeVerts[i].key];
	m_activeVerts.swap(uniqueVertices);
}

void FbxLoad::RegisterBoneUsage(Cmdl& cmdl, const char* szBoneName, vmdf::SkinningType eSkinType)
{
	if (eSkinType == vmdf::SkinningType_NO_SKINNING)
		return;
	std::vector<BoneInfo>& searchSet = eSkinType == vmdf::SkinningType_SMOOTH_SKINNING ? m_skinnedBoneIndices : m_rigidBoneIndices;
	uint32_t uIdInSkeleton = FindBoneRenderingId(cmdl, szBoneName);
	for (const BoneInfo& b : searchSet)
		if (b.uOriginalMapping == uIdInSkeleton)
			return;
	BoneInfo boneInfo;
	boneInfo.uOriginalMapping = uIdInSkeleton;
	setField(boneInfo.szName, szBoneName);
	searchSet.push_back(boneInfo);
}

void FbxLoad::AddStreams(Cmdl& cmdl, exchange::Shape* pShape, fbx::Node* pNode, fbx::Mesh* pMesh)
{
	const uint32_t uStreamCount = (uint32_t)m_activeVerts[0].elements.size();
	for (uint32_t i = 0; i < uStreamCount; i++)
	{
		std::unique_ptr<exchange::Stream> pNewStream(new exchange::Stream());
		const VertexElement& first = m_activeVerts[0].elements[i];
		uint32_t uColumnNum = first.uCount;
		switch (first.eElementType)
		{
		case vmdf::VE_FLOAT:
		case vmdf::VE_INT:
		case vmdf::VE_UINT:
		{
			pNewStream->allocate<float>((uint32_t)m_activeVerts.size() * uColumnNum, uColumnNum);
			float* pData = (float*)pNewStream->GetStreamArrayPtr();
			for (const TempVertex& v : m_activeVerts)
			{
				if (v.elements.size() <= i || v.elements[i].uCount != uColumnNum || v.elements[i].type != first.type)
					fatal("mesh %s: vertices don't all have the same attributes", pNode->name.c_str());
				for (uint32_t e = 0; e < uColumnNum; e++)
					*pData++ = v.elements[i].elements[e];
			}
			break;
		}
		case vmdf::VE_UBYTE:
		case vmdf::VE_BYTE:
		{
			pNewStream->allocate<uint8_t>((uint32_t)m_activeVerts.size() * uColumnNum, uColumnNum);
			uint8_t* pData = (uint8_t*)pNewStream->GetStreamArrayPtr();
			for (const TempVertex& v : m_activeVerts)
			{
				if (v.elements.size() <= i || v.elements[i].uCount != uColumnNum)
					fatal("mesh %s: vertices don't all have the same attributes", pNode->name.c_str());
				for (uint32_t e = 0; e < uColumnNum; e++)
					*pData++ = v.elements[i].uElements[e];
			}
			break;
		}
		default:
			fatal("internal error: unexpected vertex element type");
		}
		cmdl.streams.push_back(std::move(pNewStream));

		vmdf::VertexStreamInfo info;
		exchange::VertexStreamInfo_init(&info);
		info.elementType = first.eElementType;
		info.scaling = 1.0f;
		info.attribute = first.type;
		setField(info.usageHint, first.hint);
		info.columns = uColumnNum;
		info.refIndex = cmdl.GetStreamNum() - 1;
		pShape->AddVertexStreamInfo(info);
	}

	std::unique_ptr<exchange::Stream> pIndex(new exchange::Stream());
	pIndex->allocate<uint32_t>((uint32_t)m_indicesTmp.size(), 3);
	memcpy(pIndex->GetStreamArrayPtr(), m_indicesTmp.data(), m_indicesTmp.size() * sizeof(uint32_t));
	cmdl.streams.push_back(std::move(pIndex));

	bool bHasSkin = !pMesh->skins.empty();
	vmdf::SkinningType eSkinType = bHasSkin ? vmdf::SkinningType_RIGID_SKINNING : vmdf::SkinningType_NO_SKINNING;
	for (const fbx::Skin& s : pMesh->skins)
		if (!s.rigid)
			eSkinType = vmdf::SkinningType_SMOOTH_SKINNING;

	exchange::PrimitiveInfo& info = pShape->GetPrimitiveInfo();
	info = exchange::PrimitiveInfo();
	info.lodLevel = 0;
	info.eSkinType = eSkinType;
	info.adjacencyStreamRefIndex = 0;
	info.indexStreamRefIndex = cmdl.GetStreamNum() - 1;
	info.indexStreamFormatSize = sizeof(uint32_t);

	for (const fbx::Skin& s : pMesh->skins)
	{
		for (const fbx::SkinCluster& c : s.clusters)
		{
			std::string boneName = c.link ? c.link->name : std::string();
			size_t lodPos = boneName.find("_LOD");
			if (lodPos != std::string::npos)
				boneName = boneName.substr(0, lodPos); // Don't use the LOD name
			RegisterBoneUsage(cmdl, boneName.c_str(), eSkinType);
			setFieldTruncate(info.rootBoneName, boneName.c_str());
			info.rootBoneIndex = FindBone(*cmdl.GetSkeleton(), boneName.c_str());
		}
	}

	fbx::Node* pParent = pNode;
	while (pParent && !IsBone(pParent))
		pParent = pParent->parent;
	if (eSkinType == vmdf::SkinningType_NO_SKINNING && !m_bCollisionMesh)
	{
		if (!pParent)
			fatal("mesh %s isn't under any node that becomes a bone", pNode->name.c_str());
		setFieldTruncate(info.rootBoneName, pParent->name.c_str());
		info.rootBoneIndex = FindBone(*cmdl.GetSkeleton(), pParent->name.c_str());
	}
	if (info.rootBoneName[0] == '\0')
	{
		setFieldTruncate(info.rootBoneName, cmdl.GetSkeleton()->Bones()[0].name);
		info.rootBoneIndex = 0;
	}
}

void FbxLoad::ReadMeshRecursive(Cmdl& cmdl, fbx::Node* pNode, bool bStatic)
{
	m_pParentBoneNode = nullptr;
	if (pNode->attrType == fbx::eMesh)
	{
		// Ayataka casts the node to an FbxMesh here, so its skin check is
		// always false and every mesh is placed relative to its bone
		fbx::Node* pParent = pNode;
		while (pParent && !IsBone(pParent))
			pParent = pParent->parent;
		m_pParentBoneNode = bStatic ? nullptr : pParent;

		bool bSkinned = false;
		for (auto& m : pNode->meshes)
			if (m->GetPolygonCount() > 0)
				bSkinned |= !m->skins.empty();

		m_uMeshMaterialOffset = cmdl.GetMaterialNum();
		AddMaterials(cmdl, pNode, bSkinned);

		for (auto& m : pNode->meshes)
		{
			if (m->GetPolygonCount() <= 0)
				continue;
			std::unique_ptr<exchange::Shape> pShape(new exchange::Shape());
			AddMesh(cmdl, pShape.get(), pNode, m.get(), bStatic);
			RemoveDuplicateVertices();
			AddStreams(cmdl, pShape.get(), pNode, m.get());
			cmdl.shapes.push_back(std::move(pShape));

			std::unique_ptr<exchange::Mesh> pMesh(new exchange::Mesh());
			pMesh->SetName(pNode->name.c_str());
			pMesh->SetShapeRefIndex(cmdl.GetShapeNum() - 1);
			pMesh->SetMaterialRefName(cmdl.GetMaterialPtr(m_uMeshMaterialTmp)->pb().materialName);
			pMesh->SetMaterialRefIndex(m_uMeshMaterialTmp);
			cmdl.meshes.push_back(std::move(pMesh));
		}
	}
	for (fbx::Node* c : pNode->children)
		ReadMeshRecursive(cmdl, c, bStatic);
}

uint32_t FbxLoad::FindBone(uint32_t uOriginalIndex, const std::vector<BoneInfo>& boneInfo)
{
	for (uint32_t i = 0; i < boneInfo.size(); i++)
		if (boneInfo[i].uOriginalMapping == uOriginalIndex)
			return i;
	return 0;
}

bool FbxLoad::PostProcessSkeleton(Cmdl& cmdl)
{
	exchange::Skeleton* pSkeleton = cmdl.GetSkeleton();
	for (vmdf::Bone& b : pSkeleton->Bones())
		pSkeleton->FillOutBindPoseMatrix(b);

	bool bWrong = false;
	for (uint32_t i = 0; i < cmdl.GetShapeNum(); ++i)
	{
		exchange::PrimitiveInfo& primInfo = cmdl.GetShapePtr(i)->GetPrimitiveInfo();
		primInfo.rootBoneIndex = FindBone(*pSkeleton, primInfo.rootBoneName);
		vmdf::Bone& bone = pSkeleton->Bones().at(primInfo.rootBoneIndex);
		setFieldTruncate(primInfo.rootBoneName, bone.name);
		const char* p = strstr(bone.name, "_LOD");
		if (p)
		{
			if (p[4] < '0' || '9' < p[4])
			{
				printf("This node named \"%s\" doesn't have correct LOD level!\n", bone.name);
				bWrong = true;
				break;
			}
			int lodLevel = p[4] - '0';
			if (lodLevel >= 1)
			{
				primInfo.rootBoneName[p - bone.name] = '\0';
				primInfo.lodLevel = (uint32_t)lodLevel;
			}
		}
	}
	if (bWrong)
		return false;

	auto& bones = pSkeleton->Bones();
	for (auto it = bones.begin(); it != bones.end();)
	{
		int lodLevel = 0;
		const char* p = strstr(it->name, "LOD");
		if (p)
		{
			char valueChar[] = { p[3], '\0' };
			lodLevel = atoi(valueChar);
		}
		if (lodLevel >= 1)
			it = bones.erase(it);
		else
			++it;
	}

	for (uint32_t i = 0; i < cmdl.GetShapeNum(); ++i)
	{
		exchange::Shape* pShape = cmdl.GetShapePtr(i);
		exchange::PrimitiveInfo& primInfo = pShape->GetPrimitiveInfo();
		primInfo.rootBoneIndex = pSkeleton->SearchBone(primInfo.rootBoneName);
		if (primInfo.rootBoneIndex == UINT32_MAX)
			fatal("shape root bone %s was removed", primInfo.rootBoneName);
		vmdf::Bone& bone = bones.at(primInfo.rootBoneIndex);
		vmdf::Sphere tempSphere = pShape->pb().boundingSphere;
		if (pShape->GetSkinningType() != vmdf::SkinningType_NO_SKINNING)
			tempSphere.center = usgm::TransformVec3(bone.invBindPoseTrans, tempSphere.center);
		if (usgm::Magnitude(bone.boundingSphere.center) == 0.0f && bone.boundingSphere.radius == 0.0f)
		{
			bone.boundingSphere = tempSphere;
		}
		else
		{
			vmdf::Vector3f c = bone.boundingSphere.center;
			float r = bone.boundingSphere.radius;
			uniteSphere(bone.boundingSphere.center, bone.boundingSphere.radius, c, r, tempSphere.center, tempSphere.radius);
		}
	}

	for (BoneInfo& b : m_rigidBoneIndices)
		b.uNewMapping = pSkeleton->SearchBone(b.szName);
	for (BoneInfo& b : m_skinnedBoneIndices)
		b.uNewMapping = pSkeleton->SearchBone(b.szName);
	pSkeleton->pb().bonesNum = (uint32_t)bones.size();
	return true;
}

void FbxLoad::SetupAdjacencyStream(Cmdl& cmdl)
{
	for (uint32_t i = 0; i < cmdl.GetShapeNum(); ++i)
	{
		exchange::PrimitiveInfo& primInfo = cmdl.GetShapePtr(i)->GetPrimitiveInfo();
		const exchange::Stream* pIndexStream = cmdl.GetStreamPtr(primInfo.indexStreamRefIndex);
		std::unique_ptr<exchange::Stream> pAdj(new exchange::Stream());
		pAdj->allocate<uint32_t>(pIndexStream->GetLength(), pIndexStream->GetColumnNum());
		primInfo.adjacencyStreamRefIndex = cmdl.GetStreamNum();
		cmdl.streams.push_back(std::move(pAdj));
	}
}

void FbxLoad::PostProcessing(Cmdl& cmdl)
{
	cmdl.rigidBones.clear();
	for (const BoneInfo& b : m_rigidBoneIndices)
		cmdl.rigidBones.push_back((uint8_t)b.uNewMapping);
	cmdl.smoothSkinBones.clear();
	for (const BoneInfo& b : m_skinnedBoneIndices)
		cmdl.smoothSkinBones.push_back((uint8_t)b.uNewMapping);

	for (uint32_t i = 0; i < cmdl.GetShapeNum(); ++i)
	{
		exchange::Shape* pShape = cmdl.GetShapePtr(i);
		const exchange::PrimitiveInfo& info = pShape->GetPrimitiveInfo();
		vmdf::VertexStreamInfo* pStreamInfo = pShape->GetVertexStreamInfoOfType(vmdf::VertexAttribute_BONE_INDEX);
		if (pStreamInfo)
		{
			const std::vector<BoneInfo>& boneSet = info.eSkinType == vmdf::SkinningType_SMOOTH_SKINNING ? m_skinnedBoneIndices : m_rigidBoneIndices;
			exchange::Stream* pStream = cmdl.GetStreamPtr(pStreamInfo->refIndex);
			uint8_t* pData = (uint8_t*)pStream->GetStreamArrayPtr();
			for (uint32_t index = 0; index < pStream->GetLength(); index++)
				pData[index] = (uint8_t)FindBone(pData[index], boneSet);
		}
	}

	for (uint32_t i = 0; i < cmdl.GetMeshNum(); ++i)
	{
		exchange::Mesh* pMesh = cmdl.GetMeshPtr(i);
		if (pMesh->GetMaterialRefIndex() == UINT32_MAX || pMesh->GetShapeRefIndex() == UINT32_MAX)
			continue;
		exchange::Material* pMaterial = cmdl.GetMaterialPtr(pMesh->GetMaterialRefIndex());
		exchange::Shape* pShape = cmdl.GetShapePtr(pMesh->GetShapeRefIndex());
		if (pShape->HasVertexAlpha())
			pMaterial->SetVariable("bVertexAlpha", true);
	}

	for (uint32_t i = 0; i < cmdl.GetMaterialNum(); i++)
	{
		exchange::Material* pMaterial = cmdl.GetMaterialPtr(i);
		m_pOverrides->ApplyOverrides(pMaterial->pb().materialName, pMaterial);
	}
	SetupAdjacencyStream(cmdl);
}

void FbxLoad::Load(Cmdl& cmdl, fbx::Scene* scene, bool bSkeletonOnly, bool bCollisionModel, bool bForceBinormCalc, DependencyTracker* pDependencies, MaterialOverrides* pOverrides)
{
	m_pOverrides = pOverrides;
	m_pDependencies = pDependencies;
	m_pScene = scene;
	m_bCollisionMesh = bCollisionModel;
	m_bForceBinormalCalc = bForceBinormCalc;

	fbx::Node* pRootNode = scene->GetRootNode();
	cmdl.skeleton.reset(new exchange::Skeleton());
	exchange::Skeleton* pSkeleton = cmdl.skeleton.get();

	if (bCollisionModel || IsIdentityBoneRequired(pRootNode))
		AddIdentityBone(pSkeleton);
	if (!bCollisionModel)
		ReadSkeleton(pSkeleton, pRootNode);
	if (!bCollisionModel)
	{
		ReadLightsRecursive(cmdl, pRootNode);
		ReadCamerasRecursive(cmdl, pRootNode);
	}
	if (!bSkeletonOnly)
		ReadMeshRecursive(cmdl, pRootNode, bCollisionModel);

	if (!bSkeletonOnly)
	{
		for (uint32_t n = 0; n < cmdl.GetShapeNum(); n++)
		{
			exchange::Shape* pShape = cmdl.GetShapePtr(n);
			const exchange::Stream* pPos = cmdl.GetStreamPtr(pShape->GetPositionStreamRefIndex());
			const float* p = (const float*)pPos->GetStreamArrayPtr();
			vmdf::Vector3f vMin = usgm::V3(FLT_MAX, FLT_MAX, FLT_MAX);
			vmdf::Vector3f vMax = usgm::V3(FLT_MIN, FLT_MIN, FLT_MIN);
			uint32_t vertexNum = pPos->GetLength() / pPos->GetColumnNum();
			for (uint32_t v = 0; v < vertexNum; ++v)
			{
				float x = p[v * 3], y = p[v * 3 + 1], z = p[v * 3 + 2];
				if (v == 0)
				{
					vMin.x = vMax.x = x;
					vMin.y = vMax.y = y;
					vMin.z = vMax.z = z;
				}
				else
				{
					vMin.x = x < vMin.x ? x : vMin.x;
					vMin.y = y < vMin.y ? y : vMin.y;
					vMin.z = z < vMin.z ? z : vMin.z;
					vMax.x = x > vMax.x ? x : vMax.x;
					vMax.y = y > vMax.y ? y : vMax.y;
					vMax.z = z > vMax.z ? z : vMax.z;
				}
			}
			pShape->SetAABB(vMin, vMax);
			vmdf::Vector3f center = usgm::V3((vMax.x + vMin.x) / 2.0f, (vMax.y + vMin.y) / 2.0f, (vMax.z + vMin.z) / 2.0f);
			vmdf::Sphere& sphere = pShape->pb().boundingSphere;
			sphere.center = center;
			sphere.radius = calcLength(usgm::V3(vMax.x - center.x, vMax.y - center.y, vMax.z - center.z));
		}
	}
	PostProcessSkeleton(cmdl);
	if (!bSkeletonOnly)
		PostProcessing(cmdl);
}

// --------------------------------------------------------------- probe
void probeHex(const char* tag, const float* v, int n)
{
	fprintf(g_probe, " %s", tag);
	for (int i = 0; i < n; i++)
	{
		uint32_t u;
		memcpy(&u, &v[i], 4);
		fprintf(g_probe, " %08x", u);
	}
}

void probeMatrix(const char* tag, const AMatrix& m)
{
	fprintf(g_probe, "%s", tag);
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
		{
			double d = m.Get(i, j);
			uint64_t u;
			memcpy(&u, &d, 8);
			fprintf(g_probe, " %016llx", (unsigned long long)u);
		}
	fprintf(g_probe, "\n");
}

bool probeIsBone(fbx::Scene* scene, fbx::Node* n)
{
	if (n->attrType == fbx::eUnknown)
		return false;
	if (n->attrType == fbx::eSkeleton || n->attrType == fbx::eNull || n->attrType == fbx::eMarker || n->parent == scene->GetRootNode())
		return !(n->attrType == fbx::eMesh && !n->meshes.empty() && n->meshes[0]->deformerCount > 0);
	return false;
}

const char* mapName(int m) { static const char* n[] = { "none", "cp", "poly", "pv", "edge", "same" }; return (m >= 0 && m < 6) ? n[m] : "?"; }
const char* refName(int r) { static const char* n[] = { "direct", "index", "i2d" }; return (r >= 0 && r < 3) ? n[r] : "?"; }

void probeMesh(fbx::Scene* scene, fbx::Node* node, fbx::Mesh* mesh)
{
	fbx::Node* parent = node;
	while (parent && !probeIsBone(scene, parent))
		parent = parent->parent;
	AMatrix xf;
	if (parent)
		xf = parent->EvaluateGlobalTransform().Inverse() * node->EvaluateGlobalTransform();
	else
		xf = node->EvaluateGlobalTransform() * AMatrix::TRS(node->geoT, node->geoR, node->geoS);
	AMatrix nxf = xf.Inverse().Transpose();
	fprintf(g_probe, "MESH %s polys=%d cps=%d uvlayers=%d elUV=%d elN=%d elT=%d elB=%d elC=%d elMat=%d parent=%s\n", node->name.c_str(), mesh->GetPolygonCount(),
	        (int)mesh->controlPoints.size(), (int)mesh->uvs.size(), (int)mesh->uvs.size(), (int)mesh->normals.size(), (int)mesh->tangents.size(),
	        (int)mesh->binormals.size(), (int)mesh->colors.size(), (int)mesh->materials.size(), parent ? parent->name.c_str() : "-");
	probeMatrix("XF", xf);
	probeMatrix("NXF", nxf);
	if (!node->materials.empty() && mesh->tangents.empty())
	{
		bool ok = mesh->GenerateTangentsData(0, false);
		fprintf(g_probe, "GENTAN %d\n", ok ? 1 : 0);
	}
	auto layers = [&](const char* tag, const std::vector<fbx::LayerElement>& v, bool named) {
		for (size_t i = 0; i < v.size(); i++)
		{
			if (named)
				fprintf(g_probe, "LAYER %s%d %s %s %s\n", tag, (int)i, v[i].name.c_str(), mapName(v[i].mapping), refName(v[i].reference));
			else
				fprintf(g_probe, "LAYER %s%d %s %s\n", tag, (int)i, mapName(v[i].mapping), refName(v[i].reference));
		}
	};
	layers("N", mesh->normals, false);
	layers("T", mesh->tangents, false);
	layers("B", mesh->binormals, false);
	layers("UV", mesh->uvs, true);
	layers("C", mesh->colors, false);
	layers("M", mesh->materials, false);
	int corner = 0;
	for (int t = 0; t < mesh->GetPolygonCount(); t++)
	{
		for (int v = 0; v < mesh->GetPolygonSize(t); v++, corner++)
		{
			int cp = mesh->GetPolygonVertex(t, v);
			fprintf(g_probe, "V %d %d cp=%d", t, v, cp);
			Vec4 p = xf.MultT(mesh->controlPoints[(size_t)cp]);
			float pos[3] = { (float)p[0], (float)p[1], (float)p[2] };
			probeHex("P", pos, 3);
			const std::vector<fbx::LayerElement>* sets[3] = { &mesh->normals, &mesh->binormals, &mesh->tangents };
			const char* tags[3][2] = { { "Nraw", "N" }, { "Braw", "B" }, { "Traw", "T" } };
			for (int s = 0; s < 3; s++)
			{
				double d[4] = { 0, 0, 0, 0 };
				if (!sets[s]->empty() && (*sets[s])[0].Get(cp, corner, d))
				{
					float f[3] = { (float)d[0], (float)d[1], (float)d[2] };
					Vec4 vec(f[0], f[1], f[2], 0.0);
					vec = nxf.MultT(vec);
					fbxm::Normalize(vec);
					float o[3] = { (float)vec[0], (float)vec[1], (float)vec[2] };
					probeHex(tags[s][0], f, 3);
					probeHex(tags[s][1], o, 3);
				}
			}
			double c[4] = { 0, 0, 0, 0 };
			if (!mesh->colors.empty() && mesh->colors[0].Get(cp, corner, c))
			{
				float f[4] = { (float)c[0], (float)c[1], (float)c[2], (float)c[3] };
				probeHex("C", f, 4);
			}
			for (size_t k = 0; k < mesh->uvs.size(); k++)
			{
				double uv[2] = { 0, 0 };
				if (mesh->uvs[k].Get(cp, corner, uv))
				{
					float f[2] = { (float)uv[0], (float)uv[1] };
					probeHex(k == 0 ? "UV0" : "UVn", f, 2);
				}
			}
			fprintf(g_probe, " uvi0=%d\n", mesh->GetTextureUVIndex(t, v));
		}
	}
}

void probeRecurse(fbx::Scene* scene, fbx::Node* node)
{
	if (node->attrType == fbx::eMesh)
	{
		fprintf(g_probe, "NODE %s attrs=%d mats=%d\n", node->name.c_str(), (int)node->meshes.size(), node->GetMaterialCount());
		for (int a = 0; a < (int)node->meshes.size(); a++)
		{
			if (node->meshes[(size_t)a]->GetPolygonCount() > 0)
			{
				fprintf(g_probe, "ATTR %d\n", a);
				probeMesh(scene, node, node->meshes[(size_t)a].get());
			}
		}
	}
	for (fbx::Node* c : node->children)
		probeRecurse(scene, c);
}

} // namespace

void SetProbeOutput(FILE* f)
{
	g_probe = f;
}

LoadResult LoadFbx(const std::string& path, bool bAsCollision, bool bSkeletonOnly, Cmdl& cmdl, DependencyTracker& deps)
{
	LoadResult result;
	fbx::Scene scene;
	if (!scene.Load(path, result.error))
		return result;

	// scene name: the file name without its last extension
	std::string name = pathBasename(path);
	size_t dot = name.find_last_of('.');
	scene.name = name.substr(0, dot);

	if (!scene.Axis().IsOpenGL())
		warning("%s: axis system isn't Y-up right-handed (OpenGL); FBX SDK axis conversion is not emulated", path.c_str());
	// FbxSystemUnit::m.ConvertScene: a no-op for files already in metres
	double attenScale = scene.Axis().unitScaleFactor / 100.0;
	if (scene.Axis().unitScaleFactor != 100.0)
		warning("%s: unit scale factor %g isn't metres; FBX SDK unit conversion is not emulated", path.c_str(), scene.Axis().unitScaleFactor);
	if (scene.HasAnimation())
		result.skippedAnimation = true;

	if (!bSkeletonOnly)
	{
		scene.Triangulate();
		if (!bAsCollision)
			scene.SplitMeshesPerMaterial();
	}

	if (g_probe)
	{
		probeRecurse(&scene, scene.GetRootNode());
		result.ok = true;
		return result;
	}

	MaterialOverrides overrides;
	overrides.Init(pathWithoutExtension(path) + ".yml", "Model", "FBXDefault", &deps);

	FbxLoad loader;
	loader.SetAttenScale(attenScale);
	loader.Load(cmdl, &scene, bSkeletonOnly, bAsCollision, false, &deps, &overrides);

	// Ayataka's SetNameFromPath: the file name up to its first '.'
	std::string base = pathBasename(path);
	cmdl.name = base.substr(0, base.find('.'));
	result.ok = true;
	return result;
}
