/****************************************************************************
//	fbx-to-vmdf
//	The intermediate ("exchange") model that the loader fills in and the
//	binary store writes out. Adapted from Ayataka's exchange/Stream.h,
//	exchange/Shape.h, exchange/Mesh.h, exchange/Skeleton.h, exchange/
//	Material.h and cmdl/Cmdl.h (Usagi Engine, Copyright (c) Vitei, Inc.
//	2013-2018, MIT licence).
*****************************************************************************/
#ifndef FBX2VMDF_EXCHANGE_H
#define FBX2VMDF_EXCHANGE_H

#include "Common.h"
#include "MaterialDefinition.h"
#include "UsgMath.h"
#include "VmdfFormat.h"

#include <memory>
#include <string>
#include <vector>

namespace exchange {

// Zeroed nanopb structs with the engine's *_init defaults applied.
void Shape_init(vmdf::Shape* p);
void VertexStreamInfo_init(vmdf::VertexStreamInfo* p);
void Mesh_init(vmdf::Mesh* p);
void Bone_init(vmdf::Bone* p);
void Skeleton_init(vmdf::Skeleton* p);
void Material_init(vmdf::Material* p);
void ModelHeader_init(vmdf::ModelHeader* p);

// A typed column array (positions, normals, indices, ...).
class Stream
{
public:
	template <typename T> void allocate(uint32_t length, uint32_t colNum)
	{
		m_uLength = length;
		m_uColumnNum = colNum;
		m_uElementSize = sizeof(T);
		m_data.assign((size_t)sizeof(T) * length, 0);
		m_bAllocated = true;
	}
	void* GetStreamArrayPtr() { return m_data.data(); }
	const void* GetStreamArrayPtr() const { return m_data.data(); }
	uint32_t GetSize() const { return (uint32_t)m_data.size(); }
	uint32_t GetLength() const { return m_uLength; }
	uint32_t GetColumnNum() const { return m_uColumnNum; }
	uint32_t GetSingleByteSize() const { return m_uColumnNum * m_uElementSize; }
	uint32_t GetElementSize() const { return m_uElementSize; }
	bool IsAllocated() const { return m_bAllocated; }

private:
	std::vector<uint8_t> m_data;
	uint32_t m_uElementSize = 0;
	uint32_t m_uLength = 0;
	uint32_t m_uColumnNum = 0;
	bool m_bAllocated = false;
};

struct PrimitiveInfo
{
	uint32_t indexStreamRefIndex = UINT32_MAX;
	uint32_t indexStreamFormatSize = UINT32_MAX;
	uint32_t rootBoneIndex = 0;
	char rootBoneName[40] = {};
	vmdf::SkinningType eSkinType = vmdf::SkinningType_NO_SKINNING;
	uint32_t adjacencyStreamRefIndex = UINT32_MAX;
	uint32_t lodLevel = 0;
};

class Shape
{
public:
	Shape() { Shape_init(&m_shape); }

	uint32_t GetVertexAttributeMask() const;
	vmdf::SkinningType GetSkinningType() const { return m_primitiveInfo.eSkinType; }

	vmdf::Vector3f GetAABBMin() const { return m_aabbMin; }
	vmdf::Vector3f GetAABBMax() const { return m_aabbMax; }
	void SetAABB(const vmdf::Vector3f& mn, const vmdf::Vector3f& mx) { m_aabbMin = mn; m_aabbMax = mx; }

	uint32_t SearchStream(uint32_t attrType) const;
	uint32_t GetPositionStreamRefIndex() const { return SearchStream(vmdf::VertexAttribute_POSITION); }
	bool HasVertexAlpha() const;
	uint32_t GetBoneIndexCount() const;

	PrimitiveInfo& GetPrimitiveInfo() { return m_primitiveInfo; }
	const PrimitiveInfo& GetPrimitiveInfo() const { return m_primitiveInfo; }

	void AddVertexStreamInfo(const vmdf::VertexStreamInfo& info) { m_vertexStreamInfos.push_back(info); }
	const vmdf::VertexStreamInfo& GetVertexStreamInfo(uint32_t i) const { return m_vertexStreamInfos.at(i); }
	vmdf::VertexStreamInfo* GetVertexStreamInfoOfType(uint32_t attribType);
	uint32_t GetVertexStreamNum() const { return (uint32_t)m_vertexStreamInfos.size(); }
	uint32_t SearchSingleAttribute(uint32_t attrType) const;

	vmdf::Shape& pb() { return m_shape; }
	const vmdf::Shape& pb() const { return m_shape; }

private:
	vmdf::Shape m_shape;
	PrimitiveInfo m_primitiveInfo;
	std::vector<vmdf::VertexStreamInfo> m_vertexStreamInfos;
	vmdf::Vector3f m_aabbMin = { 0, 0, 0 }, m_aabbMax = { 0, 0, 0 };
};

class Mesh
{
public:
	Mesh() { Mesh_init(&m_mesh); }
	void SetName(const char* p);
	void SetShapeRefIndex(uint32_t i) { m_mesh.shapeRefIndex = i; }
	uint32_t GetShapeRefIndex() const { return m_mesh.shapeRefIndex; }
	void SetMaterialRefName(const char* p);
	void SetMaterialRefIndex(uint32_t v) { m_mesh.materialRefIndex = v; }
	uint32_t GetMaterialRefIndex() const { return m_mesh.materialRefIndex; }
	vmdf::Mesh& pb() { return m_mesh; }
	const vmdf::Mesh& pb() const { return m_mesh; }

private:
	vmdf::Mesh m_mesh;
};

class Skeleton
{
public:
	Skeleton() { Skeleton_init(&m_skeleton); }
	vmdf::Skeleton& pb() { return m_skeleton; }
	const vmdf::Skeleton& pb() const { return m_skeleton; }
	std::vector<vmdf::Bone>& Bones() { return m_bones; }
	const std::vector<vmdf::Bone>& Bones() const { return m_bones; }

	uint32_t SearchBone(const char* name) const;
	void ReverseCoordinate();
	void FillOutBindPoseMatrix(vmdf::Bone& bone);

private:
	vmdf::Matrix4x4 GetParentMatrix(vmdf::Bone& bone);
	vmdf::Skeleton m_skeleton;
	std::vector<vmdf::Bone> m_bones;
};

class Material
{
public:
	Material() { Material_init(&m_material); }

	vmdf::Material& pb() { return m_material; }
	const vmdf::Material& pb() const { return m_material; }

	void InitCustomMaterial(const std::string& effectPath, const char* szEffectName, const std::vector<std::string>& defines);

	template <class T> void SetVariable(const char* szName, T var, uint32_t uIndex = 0)
	{
		SetVariableArray(szName, &var, 1, uIndex);
	}
	template <class T> void SetVariableArray(const char* szName, const T* var, uint32_t uCount = 1, uint32_t uIndex = 0)
	{
		for (uint32_t pass = 0; pass < vmdf::Material_RenderPass_COUNT; pass++)
		{
			for (uint32_t i = 0; i < m_materialDef[pass].GetConstantSetCount(); i++)
			{
				m_materialDef[pass].OverrideDefault(i, szName, GetConstantSetData(pass, i), var, (uint32_t)sizeof(T) * uCount, (uint32_t)sizeof(T) * uIndex);
			}
		}
	}
	template <class T> bool GetVariable(const char* szName, T* pVar, uint32_t uIndex = 0)
	{
		for (uint32_t i = 0; i < m_materialDef[0].GetConstantSetCount(); i++)
		{
			if (m_materialDef[0].GetVariable(i, GetConstantSetData(0, i), szName, pVar, (uint32_t)sizeof(T), (uint32_t)sizeof(T) * uIndex))
				return true;
		}
		return false;
	}

	MaterialDefinition& GetCustomFX(uint32_t uPass) { return m_materialDef[uPass]; }
	void* GetConstantSetData(uint32_t uPass, uint32_t uSet);

private:
	void AddConstantSet(const char* szName, uint32_t uPass, uint32_t uSize);

	MaterialDefinition m_materialDef[vmdf::Material_RenderPass_COUNT];
	vmdf::Material m_material;
};

} // namespace exchange

// Whole model (Ayataka's Cmdl).
class Cmdl
{
public:
	struct Light
	{
		std::string parentBone;
		std::string name;
		vmdf::Vector3f position = { 0, 0, 0 };
		// usg::LightSpec subset written to the skeleton XML
		uint32_t kind = vmdf::LightKind_DIRECTIONAL;
		bool bShadow = false;
		vmdf::Color ambient = { { 0, 0, 0, 1 } }, diffuse = { { 0, 0, 0, 1 } }, specular = { { 0, 0, 0, 1 } };
		vmdf::Vector3f direction = { 0, 0, 0 };
		bool attenEnabled = false;
		float attenNear = 0.0f, attenFar = 0.0f;
		float innerAngle = 0.0f, outerAngle = 0.0f;
	};
	struct Camera
	{
		std::string parentBone;
		std::string name;
		vmdf::Vector3f position = { 0, 0, 0 };
		vmdf::Vector3f rotate = { 0, 0, 0 };
		float fov = 0.0f, nearPlane = 0.0f, farPlane = 0.0f;
	};

	std::vector<std::unique_ptr<exchange::Shape>> shapes;
	std::vector<std::unique_ptr<exchange::Material>> materials;
	std::vector<std::unique_ptr<exchange::Mesh>> meshes;
	std::vector<std::unique_ptr<exchange::Stream>> streams;
	std::vector<Light> lights;
	std::vector<Camera> cameras;
	std::vector<uint8_t> rigidBones, smoothSkinBones;
	std::unique_ptr<exchange::Skeleton> skeleton;
	std::string name;

	uint32_t GetShapeNum() const { return (uint32_t)shapes.size(); }
	uint32_t GetMaterialNum() const { return (uint32_t)materials.size(); }
	uint32_t GetMeshNum() const { return (uint32_t)meshes.size(); }
	uint32_t GetStreamNum() const { return (uint32_t)streams.size(); }
	exchange::Shape* GetShapePtr(uint32_t i) const { return shapes.at(i).get(); }
	exchange::Material* GetMaterialPtr(uint32_t i) const { return materials.at(i).get(); }
	exchange::Mesh* GetMeshPtr(uint32_t i) const { return meshes.at(i).get(); }
	exchange::Stream* GetStreamPtr(uint32_t i) const { return streams.at(i).get(); }
	exchange::Skeleton* GetSkeleton() const { return skeleton.get(); }

	// Mirror X (right-handed FBX -> the engine's left-handed space)
	void ReverseCoordinate();
	void FlipUV();

private:
	void ReverseCoordinateInt(exchange::Shape* pShape);
	void ReverseCoordinateInt(exchange::Stream& stream);
};

// Dependency (.d) file writer (Ayataka's DependencyTracker).
class DependencyTracker
{
public:
	void StartRecord(const std::string& dependencyFile, const std::string& outputFile);
	void LogDependency(const std::string& dependency);
	bool ExportDependencies() const;
	const std::vector<std::string>& GetDependencies() const { return m_dependencies; }

private:
	std::string m_dependencyFile;
	std::string m_outputFile;
	std::vector<std::string> m_dependencies;
};

#endif // FBX2VMDF_EXCHANGE_H
