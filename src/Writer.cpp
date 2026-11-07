/****************************************************************************
//	fbx-to-vmdf
//	Adapted from Ayataka's cmdl/CmdlBinaryStore.cpp and common/
//	ModelConverterBase.cpp (Usagi Engine, Copyright (c) Vitei, Inc.
//	2013-2018, MIT licence). Big-endian output (-be) is not supported.
*****************************************************************************/
#include "Writer.h"

#include "meshoptimizer.h"
#include "pugixml.hpp"

#include <algorithm>

namespace {

// Ayataka's common/Vector.h Vector3 (sqrtf length) for the bounds maths
struct V3
{
	float x, y, z;
	float calcLength() const { return det::sqrtf((x * x) + (y * y) + (z * z)); }
};
inline V3 add(const vmdf::Vector3f& a, const vmdf::Vector3f& b) { return V3{ a.x + b.x, a.y + b.y, a.z + b.z }; }

void calcBoundingSphereOfEntireModel(vmdf::ModelHeader* pHeader, const Cmdl& cmdl)
{
	vmdf::Vector3f vMin = cmdl.GetShapePtr(0)->GetAABBMin();
	vmdf::Vector3f vMax = cmdl.GetShapePtr(0)->GetAABBMax();
	for (uint32_t i = 1; i < cmdl.GetShapeNum(); ++i)
	{
		const exchange::Shape* pShape = cmdl.GetShapePtr(i);
		vMin.x = std::min(vMin.x, pShape->GetAABBMin().x);
		vMin.y = std::min(vMin.y, pShape->GetAABBMin().y);
		vMin.z = std::min(vMin.z, pShape->GetAABBMin().z);
		vMax.x = std::max(vMax.x, pShape->GetAABBMax().x);
		vMax.y = std::max(vMax.y, pShape->GetAABBMax().y);
		vMax.z = std::max(vMax.z, pShape->GetAABBMax().z);
	}
	V3 sum = add(vMax, vMin);
	V3 vCenter{ sum.x / 2, sum.y / 2, sum.z / 2 };
	pHeader->boundingSphere.center = usgm::V3(vCenter.x, vCenter.y, vCenter.z);
	V3 vRadius{ vMax.x - vCenter.x, vMax.y - vCenter.y, vMax.z - vCenter.z };
	pHeader->boundingSphere.radius = vRadius.calcLength();
}

size_t calcStoredBinarySize(const Cmdl& cmdl, size_t alignment)
{
	size_t wholeSize = calcAlignedSize(sizeof(vmdf::ModelHeader), alignment);
	size_t alignedShapeSectionSize = calcAlignedSize(sizeof(vmdf::Shape), alignment);
	for (uint32_t n = 0; n < cmdl.GetShapeNum(); ++n)
	{
		const exchange::Shape* pShape = cmdl.GetShapePtr(n);
		wholeSize += alignedShapeSectionSize;
		const exchange::PrimitiveInfo& primInfo = pShape->GetPrimitiveInfo();
		const exchange::Stream* pIndexStream = cmdl.GetStreamPtr(primInfo.indexStreamRefIndex);
		size_t indexStreamSize = (size_t)pIndexStream->GetLength() * primInfo.indexStreamFormatSize;
		wholeSize += calcAlignedSize(indexStreamSize, alignment) * 2; // index + adjacency
		size_t vertexStreamStride = 0;
		for (uint32_t i = 0; i < pShape->GetVertexStreamNum(); ++i)
		{
			vertexStreamStride += cmdl.GetStreamPtr(pShape->GetVertexStreamInfo(i).refIndex)->GetSingleByteSize();
		}
		const exchange::Stream* pPositionStream = cmdl.GetStreamPtr(pShape->GetPositionStreamRefIndex());
		uint32_t uVertexCount = pPositionStream->GetLength() / pPositionStream->GetColumnNum();
		vertexStreamStride = calcAlignedSize(vertexStreamStride, 4);
		wholeSize += calcAlignedSize(vertexStreamStride * uVertexCount, alignment);
	}
	wholeSize += sizeof(vmdf::Material) * cmdl.GetMaterialNum();
	wholeSize += sizeof(vmdf::Mesh) * cmdl.GetMeshNum();
	if (cmdl.GetSkeleton())
	{
		wholeSize += sizeof(vmdf::Skeleton);
		wholeSize += cmdl.GetSkeleton()->pb().bonesNum * sizeof(vmdf::Bone);
	}
	return wholeSize;
}

template <typename T> void copyIndexStream(uint8_t* p, const exchange::Stream* pIndexStream)
{
	T* pTemp = (T*)p;
	const uint32_t* src = (const uint32_t*)pIndexStream->GetStreamArrayPtr();
	for (uint32_t i = 0; i < pIndexStream->GetLength(); ++i)
	{
		pTemp[i] = (T)src[i];
	}
}

size_t storeShape(uint8_t* base, size_t off, const Cmdl& cmdl, const exchange::Shape* pShape, size_t alignment)
{
	vmdf::Shape out = pShape->pb();

	const exchange::PrimitiveInfo& primInfo = pShape->GetPrimitiveInfo();
	const exchange::Stream* pIndexStream = cmdl.GetStreamPtr(primInfo.indexStreamRefIndex);
	const exchange::Stream* pPositionStream = cmdl.GetStreamPtr(pShape->GetPositionStreamRefIndex());
	const uint32_t vertexNum = pPositionStream->GetLength() / pPositionStream->GetColumnNum();

	uint32_t sizeAligned = (uint32_t)calcAlignedSize((size_t)pIndexStream->GetLength() * primInfo.indexStreamFormatSize, alignment);
	out.primitive.indexStream.sizeAligned = sizeAligned;
	out.primitive.indexStream.formatSize = primInfo.indexStreamFormatSize;
	out.primitive.indexStream.indexNum = pIndexStream->GetLength();
	out.primitive.adjacencyStream.sizeAligned = sizeAligned;
	out.primitive.rootBone = primInfo.rootBoneIndex;
	out.primitive.lodLevel = primInfo.lodLevel;
	out.attributeMask = pShape->GetVertexAttributeMask();
	out.skinningType = (uint32_t)pShape->GetSkinningType();

	uint32_t vertexStreamStep = 0;
	out.streamInfo_count = 0;
	const uint32_t streamNum = pShape->GetVertexStreamNum();
	if (streamNum > vmdf::Shape::streamInfo_max_count)
	{
		fatal("shape has %u vertex streams, the format holds %u", streamNum, (uint32_t)vmdf::Shape::streamInfo_max_count);
	}
	for (uint32_t i = 0; i < streamNum; ++i)
	{
		const vmdf::VertexStreamInfo& info = pShape->GetVertexStreamInfo(i);
		if (info.attribute != vmdf::VertexAttribute_NONE)
		{
			vertexStreamStep += cmdl.GetStreamPtr(info.refIndex)->GetSingleByteSize();
			out.streamInfo[out.streamInfo_count] = info;
			out.streamInfo_count++;
		}
	}
	vertexStreamStep = (uint32_t)calcAlignedSize(vertexStreamStep, 4);
	size_t vertexStreamSize = (size_t)vertexStreamStep * vertexNum;
	out.vertexStreamSizeAligned = (uint32_t)calcAlignedSize(vertexStreamSize, alignment);
	out.vertexNum = vertexNum;
	out.streamOffset = (uint32_t)calcAlignedSize(sizeof(vmdf::Shape), alignment);

	memcpy(base + off, &out, sizeof(out));
	off += calcAlignedSize(sizeof(vmdf::Shape), alignment);

	// Index stream, then the adjacency stream. Ayataka allocated the
	// adjacency stream and never filled it (heap garbage); it is zero here.
	uint8_t* pIndices = base + off;
	switch (primInfo.indexStreamFormatSize)
	{
	case 1: copyIndexStream<uint8_t>(pIndices, pIndexStream); break;
	case 2: copyIndexStream<uint16_t>(pIndices, pIndexStream); break;
	case 4: copyIndexStream<uint32_t>(pIndices, pIndexStream); break;
	default: fatal("bad index format size %u", primInfo.indexStreamFormatSize);
	}
	off += sizeAligned;
	off += sizeAligned;

	// Interleaved vertex stream
	uint8_t* pOutputVerts = base + off;
	uint8_t* pDst = pOutputVerts;
	for (uint32_t i = 0; i < streamNum; ++i)
	{
		const vmdf::VertexStreamInfo& info = pShape->GetVertexStreamInfo(i);
		const exchange::Stream* pStream = cmdl.GetStreamPtr(info.refIndex);
		uint32_t itemSize = pStream->GetSingleByteSize();
		const uint8_t* pSrc = (const uint8_t*)pStream->GetStreamArrayPtr();
		uint8_t* d = pDst;
		for (uint32_t v = 0; v < vertexNum; ++v)
		{
			memcpy(d, pSrc, itemSize);
			d += vertexStreamStep;
			pSrc += itemSize;
		}
		pDst += itemSize;
	}

	if (primInfo.indexStreamFormatSize == sizeof(uint32_t))
	{
		uint32_t* idx = (uint32_t*)pIndices;
		size_t indexCount = pIndexStream->GetLength();
		meshopt_optimizeVertexCache(idx, idx, indexCount, vertexNum);
		const float kThreshold = 1.01f; // allow up to 1% worse ACMR to get more reordering opportunities for overdraw
		meshopt_optimizeOverdraw(idx, idx, indexCount, (const float*)pOutputVerts, vertexNum, sizeof(vmdf::Vector3f), kThreshold);
		meshopt_optimizeVertexFetch(pOutputVerts, idx, indexCount, pOutputVerts, vertexNum, vertexStreamStep);
	}

	off += out.vertexStreamSizeAligned;
	return off;
}

} // namespace

std::vector<uint8_t> StoreCmdl(const Cmdl& cmdl, size_t alignment)
{
	size_t size = calcStoredBinarySize(cmdl, alignment);
	std::vector<uint8_t> buf(size, 0);
	uint8_t* base = buf.data();

	vmdf::ModelHeader header;
	exchange::ModelHeader_init(&header);
	header.alignment = (uint32_t)alignment;
	if (cmdl.name.size() >= sizeof(header.name))
	{
		fatal("model name %s is too long", cmdl.name.c_str());
	}
	setField(header.name, cmdl.name);
	header.shapeNum = cmdl.GetShapeNum();
	header.materialNum = cmdl.GetMaterialNum();
	header.meshNum = cmdl.GetMeshNum();
	if (cmdl.rigidBones.size() > vmdf::ModelHeader::rigidBoneIndices_max_count || cmdl.smoothSkinBones.size() > vmdf::ModelHeader::skinnedBoneIndices_max_count)
	{
		fatal("too many bone indices");
	}
	header.rigidBoneIndices_count = (uint32_t)cmdl.rigidBones.size();
	header.skinnedBoneIndices_count = (uint32_t)cmdl.smoothSkinBones.size();
	for (uint32_t i = 0; i < header.rigidBoneIndices_count; i++)
	{
		header.rigidBoneIndices[i] = cmdl.rigidBones[i];
	}
	for (uint32_t i = 0; i < header.skinnedBoneIndices_count; i++)
	{
		header.skinnedBoneIndices[i] = cmdl.smoothSkinBones[i];
	}

	size_t off = calcAlignedSize(sizeof(vmdf::ModelHeader), alignment);
	header.shapeOffset = (uint32_t)off;
	for (uint32_t n = 0; n < cmdl.GetShapeNum(); ++n)
	{
		off = storeShape(base, off, cmdl, cmdl.GetShapePtr(n), alignment);
	}
	header.materialOffset = (uint32_t)off;
	for (uint32_t i = 0; i < cmdl.GetMaterialNum(); ++i)
	{
		memcpy(base + off, &cmdl.GetMaterialPtr(i)->pb(), sizeof(vmdf::Material));
		off += sizeof(vmdf::Material);
	}
	header.meshOffset = (uint32_t)off;
	for (uint32_t i = 0; i < cmdl.GetMeshNum(); ++i)
	{
		memcpy(base + off, &cmdl.GetMeshPtr(i)->pb(), sizeof(vmdf::Mesh));
		off += sizeof(vmdf::Mesh);
	}
	if (cmdl.GetSkeleton())
	{
		header.skeletonOffset = (uint32_t)off;
		memcpy(base + off, &cmdl.GetSkeleton()->pb(), sizeof(vmdf::Skeleton));
		off += sizeof(vmdf::Skeleton);
		for (const vmdf::Bone& bone : cmdl.GetSkeleton()->Bones())
		{
			memcpy(base + off, &bone, sizeof(vmdf::Bone));
			off += sizeof(vmdf::Bone);
		}
	}
	if (cmdl.GetShapeNum() > 0)
	{
		calcBoundingSphereOfEntireModel(&header, cmdl);
	}
	memcpy(base, &header, sizeof(header));
	if (off != size)
	{
		fatal("internal error: stored %zu bytes, expected %zu", off, size);
	}
	return buf;
}

// -------------------------------------------------------- hierarchy XML
namespace {
std::string f6(float v) { return formatFixed((double)v, 6); }
std::string f2(float v) { return formatFixed((double)v, 2); }
std::string v3str(const vmdf::Vector3f& v) { return f6(v.x) + " " + f6(v.y) + " " + f6(v.z); }
// pugixml 1.2 writes floats with "%g"; keep that, but locale-free
std::string g6(double v)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%g", v);
	for (char* p = buf; *p; ++p)
		if (*p == ',')
			*p = '.';
	return buf;
}
} // namespace

bool ExportBoneHierarchy(const Cmdl& cmdl, const std::string& path)
{
	pugi::xml_document doc;
	pugi::xml_node hierarchyNode = doc.append_child("hierarchy");
	const exchange::Skeleton* pSkeleton = cmdl.GetSkeleton();
	pugi::xml_node boneSet = hierarchyNode.append_child("bone_array");
	boneSet.append_attribute("length").set_value((unsigned int)pSkeleton->Bones().size());

	for (uint32_t i = 0; i < pSkeleton->Bones().size(); i++)
	{
		const vmdf::Bone& b = pSkeleton->Bones()[i];
		pugi::xml_node bone = boneSet.append_child("bone");
		bone.append_attribute("index").set_value((unsigned int)i);
		bone.append_attribute("name").set_value(b.name);
		bone.append_attribute("parent_name").set_value(b.parentName);
		bone.append_attribute("needed_rendering").set_value(b.isNeededRendering ? "true" : "false");
		const char* billboard = "none";
		switch (b.billboardMode)
		{
		case vmdf::Bone_BillboardMode_OFF: billboard = "none"; break;
		case vmdf::Bone_BillboardMode_WORLD: billboard = "world"; break;
		case vmdf::Bone_BillboardMode_WORLDVIEWPOINT: billboard = "worldviewpint"; break; // sic
		case vmdf::Bone_BillboardMode_SCREEN: billboard = "screen"; break;
		case vmdf::Bone_BillboardMode_SCREENVIEWPOINT: billboard = "screenviewpoint"; break;
		case vmdf::Bone_BillboardMode_YAXIAL: billboard = "yaxial"; break;
		case vmdf::Bone_BillboardMode_YAXIALVIEWPOINT: billboard = "yaxialviewpoint"; break;
		}
		bone.append_attribute("billboard").set_value(billboard);
		bone.append_attribute("translate").set_value(v3str(b.translate).c_str());
		bone.append_attribute("rotate").set_value((v3str(b.rotate) + " " + f6(1.0f)).c_str());
		bone.append_attribute("scale").set_value(v3str(b.scale).c_str());
	}

	if (!cmdl.lights.empty())
	{
		pugi::xml_node lightSet = hierarchyNode.append_child("light_array");
		lightSet.append_attribute("length").set_value((unsigned int)cmdl.lights.size());
		for (const Cmdl::Light& l : cmdl.lights)
		{
			pugi::xml_node light = lightSet.append_child("light");
			light.append_attribute("name").set_value(l.name.c_str());
			light.append_attribute("parent_name").set_value(l.parentBone.c_str());
			const char* type = "point";
			switch (l.kind)
			{
			case vmdf::LightKind_DIRECTIONAL: type = "directional"; break;
			case vmdf::LightKind_SPOT: type = "spot"; break;
			case vmdf::LightKind_POINT: type = "point"; break;
			case vmdf::LightKind_PROJECTION: type = "projection"; break;
			}
			light.append_attribute("type").set_value(type);
			light.append_attribute("position").set_value(v3str(l.position).c_str());
			light.append_attribute("direction").set_value(v3str(l.direction).c_str());
			light.append_attribute("has_shadow").set_value(l.bShadow);
			auto c3 = [](const vmdf::Color& c) { return f2(c.rgba[0]) + " " + f2(c.rgba[1]) + " " + f2(c.rgba[2]); };
			light.append_attribute("ambient").set_value(c3(l.ambient).c_str());
			light.append_attribute("diffuse").set_value(c3(l.diffuse).c_str());
			light.append_attribute("specular").set_value(c3(l.specular).c_str());
			light.append_attribute("atten_enabled").set_value(l.attenEnabled);
			light.append_attribute("atten_start").set_value(g6(l.attenNear).c_str());
			light.append_attribute("atten_end").set_value(g6(l.attenFar).c_str());
			light.append_attribute("inner_angle").set_value(g6(l.innerAngle).c_str());
			light.append_attribute("outer_angle").set_value(g6(l.outerAngle).c_str());
		}
	}

	if (!cmdl.cameras.empty())
	{
		pugi::xml_node cameraSet = hierarchyNode.append_child("camera_array");
		cameraSet.append_attribute("length").set_value((unsigned int)cmdl.cameras.size());
		for (const Cmdl::Camera& c : cmdl.cameras)
		{
			pugi::xml_node camera = cameraSet.append_child("camera");
			camera.append_attribute("name").set_value((c.name + ".Camera").c_str()); // avoid bone name clashes
			camera.append_attribute("parent_name").set_value(c.parentBone.c_str());
			camera.append_attribute("position").set_value(v3str(c.position).c_str());
			camera.append_attribute("rotate").set_value(v3str(c.rotate).c_str());
			camera.append_attribute("fov").set_value(g6(c.fov).c_str());
			camera.append_attribute("near").set_value(g6(c.nearPlane).c_str());
			camera.append_attribute("far").set_value(g6(c.farPlane).c_str());
		}
	}

	return doc.save_file(path.c_str());
}
