/****************************************************************************
//	fbx-to-vmdf
//	Structural .vmdf inspection: --dump and --compare.
*****************************************************************************/
#include "Compare.h"
#include "Common.h"
#include "VmdfFormat.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace {

struct ShapeInfo
{
	size_t offset = 0;
	vmdf::Shape s = {};
	size_t indexOffset = 0, adjacencyOffset = 0, vertexOffset = 0;
	uint32_t stride = 0;
	std::vector<std::pair<std::string, std::pair<uint32_t, uint32_t>>> layout; // hint -> (offset, columns|type<<16)
};

struct Parsed
{
	vmdf::ModelHeader header = {};
	std::vector<ShapeInfo> shapes;
	std::vector<size_t> materialOffsets;
	std::vector<size_t> meshOffsets;
	size_t skeletonOffset = 0;
	uint32_t boneCount = 0;
	size_t end = 0;
	std::string error;
};

uint32_t elementSize(uint32_t t)
{
	switch (t)
	{
	case vmdf::VE_BYTE:
	case vmdf::VE_UBYTE: return 1;
	case vmdf::VE_SHORT:
	case vmdf::VE_USHORT: return 2;
	default: return 4;
	}
}

template <class T> bool readAt(const std::vector<uint8_t>& d, size_t off, T& out)
{
	if (off + sizeof(T) > d.size())
		return false;
	memcpy(&out, d.data() + off, sizeof(T));
	return true;
}

bool parse(const std::vector<uint8_t>& d, Parsed& p)
{
	if (!readAt(d, 0, p.header))
	{
		p.error = "file too small for a model header";
		return false;
	}
	const vmdf::ModelHeader& h = p.header;
	size_t align = h.alignment ? h.alignment : 16;
	if (align & (align - 1))
	{
		p.error = "bad alignment";
		return false;
	}
	size_t off = h.shapeOffset;
	for (uint32_t i = 0; i < h.shapeNum; i++)
	{
		ShapeInfo si;
		si.offset = off;
		if (!readAt(d, off, si.s))
		{
			p.error = "truncated shape";
			return false;
		}
		off += si.s.streamOffset;
		si.indexOffset = off;
		off += si.s.primitive.indexStream.sizeAligned;
		si.adjacencyOffset = off;
		off += si.s.primitive.adjacencyStream.sizeAligned;
		si.vertexOffset = off;
		uint32_t stride = 0;
		for (uint32_t k = 0; k < si.s.streamInfo_count && k < vmdf::Shape::streamInfo_max_count; k++)
		{
			const vmdf::VertexStreamInfo& vi = si.s.streamInfo[k];
			std::string hint(vi.usageHint, strnlen(vi.usageHint, sizeof(vi.usageHint)));
			si.layout.push_back(std::make_pair(hint, std::make_pair(stride, vi.columns | (vi.elementType << 16))));
			stride += vi.columns * elementSize(vi.elementType);
		}
		si.stride = (uint32_t)calcAlignedSize(stride, 4);
		off += si.s.vertexStreamSizeAligned;
		if (off > d.size())
		{
			p.error = "truncated shape data";
			return false;
		}
		p.shapes.push_back(si);
	}
	off = h.materialOffset;
	for (uint32_t i = 0; i < h.materialNum; i++, off += sizeof(vmdf::Material))
		p.materialOffsets.push_back(off);
	off = h.meshOffset;
	for (uint32_t i = 0; i < h.meshNum; i++, off += sizeof(vmdf::Mesh))
		p.meshOffsets.push_back(off);
	p.end = off;
	if (h.skeletonOffset)
	{
		vmdf::Skeleton sk;
		if (!readAt(d, h.skeletonOffset, sk))
		{
			p.error = "truncated skeleton";
			return false;
		}
		p.skeletonOffset = h.skeletonOffset;
		p.boneCount = sk.bonesNum;
		p.end = h.skeletonOffset + sizeof(vmdf::Skeleton) + (size_t)sk.bonesNum * sizeof(vmdf::Bone);
	}
	if (p.end > d.size())
	{
		p.error = "truncated file";
		return false;
	}
	return true;
}

void clearRange(std::vector<uint8_t>& mask, size_t off, size_t len)
{
	for (size_t i = off; i < off + len && i < mask.size(); i++)
		mask[i] = 0;
}

// clear the bytes after the terminator of a fixed char field
void clearStringTail(const std::vector<uint8_t>& d, std::vector<uint8_t>& mask, size_t off, size_t len)
{
	size_t n = 0;
	while (n < len && off + n < d.size() && d[off + n] != 0)
		n++;
	if (n < len)
		clearRange(mask, off + n + 1, len - n - 1);
}

struct Region
{
	size_t off, len;
	std::string name;
	char kind; // 'f' floats, 'u' uint32s, 's' string, 'b' bytes
};

void addRegions(const Parsed& p, std::vector<Region>& r)
{
	r.push_back({ 0, 8, "header.version/alignment", 'u' });
	r.push_back({ 8, 64, "header.name", 's' });
	r.push_back({ 72, 16, "header.boundingSphere", 'f' });
	r.push_back({ 88, 36, "header.counts/offsets", 'u' });
	r.push_back({ 124, 1024, "header.rigidBoneIndices", 'u' });
	r.push_back({ 1148, 1024, "header.skinnedBoneIndices", 'u' });
	for (size_t i = 0; i < p.shapes.size(); i++)
	{
		const ShapeInfo& si = p.shapes[i];
		std::string n = "shape" + std::to_string(i);
		size_t o = si.offset;
		r.push_back({ o, 32, n + ".primitive", 'u' });
		for (int k = 0; k < vmdf::Shape::streamInfo_max_count; k++)
		{
			size_t b = o + offsetof(vmdf::Shape, streamInfo) + (size_t)k * sizeof(vmdf::VertexStreamInfo);
			r.push_back({ b, 16, n + ".streamInfo[" + std::to_string(k) + "]", 'u' });
			r.push_back({ b + 16, 4, n + ".streamInfo[" + std::to_string(k) + "].scaling", 'f' });
			r.push_back({ b + 20, 40, n + ".streamInfo[" + std::to_string(k) + "].usageHint", 's' });
		}
		r.push_back({ o + offsetof(vmdf::Shape, vertexStreamSizeAligned), 8, n + ".vertexStreamSizeAligned/vertexNum", 'u' });
		r.push_back({ o + offsetof(vmdf::Shape, vertexAlphaScale), 4, n + ".vertexAlphaScale", 'f' });
		r.push_back({ o + offsetof(vmdf::Shape, boundingSphere), 16, n + ".boundingSphere", 'f' });
		r.push_back({ o + offsetof(vmdf::Shape, singleAttributes_count), 4 + 4 * sizeof(vmdf::SingleAttribute), n + ".singleAttributes", 'b' });
		r.push_back({ o + offsetof(vmdf::Shape, attributeMask), 8, n + ".attributeMask/skinningType", 'u' });
		r.push_back({ si.indexOffset, si.s.primitive.indexStream.sizeAligned, n + ".indexStream", 'u' });
		r.push_back({ si.adjacencyOffset, si.s.primitive.adjacencyStream.sizeAligned, n + ".adjacencyStream", 'u' });
		r.push_back({ si.vertexOffset, si.s.vertexStreamSizeAligned, n + ".vertexStream", 'f' });
	}
	for (size_t i = 0; i < p.materialOffsets.size(); i++)
	{
		std::string n = "material" + std::to_string(i);
		size_t o = p.materialOffsets[i];
		r.push_back({ o, 40, n + ".materialName", 's' });
		for (int t = 0; t < vmdf::Material::textures_max_count; t++)
		{
			size_t b = o + offsetof(vmdf::Material, textures) + (size_t)t * sizeof(vmdf::Texture);
			std::string tn = n + ".textures[" + std::to_string(t) + "]";
			r.push_back({ b, 40, tn + ".textureName", 's' });
			r.push_back({ b + 40, 20, tn + ".wrap/filters", 'u' });
			r.push_back({ b + 60, 4, tn + ".lodBias", 'f' });
			r.push_back({ b + 64, 4, tn + ".lodMinLevel", 'u' });
			r.push_back({ b + 68, 40, tn + ".textureHint", 's' });
			r.push_back({ b + 108, 4, tn + ".anisoLevel", 'u' });
		}
		r.push_back({ o + offsetof(vmdf::Material, rasterizer), sizeof(vmdf::Rasterizer), n + ".rasterizer", 'u' });
		r.push_back({ o + offsetof(vmdf::Material, textureCoordinators_count), 4, n + ".textureCoordinators_count", 'u' });
		for (int t = 0; t < vmdf::Material::textureCoordinators_max_count; t++)
		{
			size_t b = o + offsetof(vmdf::Material, textureCoordinators) + (size_t)t * sizeof(vmdf::TextureCoordinator);
			std::string tn = n + ".textureCoordinators[" + std::to_string(t) + "]";
			r.push_back({ b, 12, tn + ".method/source/refCamera", 'u' });
			r.push_back({ b + 12, 20, tn + ".scale/rotate/translate", 'f' });
		}
		r.push_back({ o + offsetof(vmdf::Material, attribute), 4, n + ".translucencyKind", 'u' });
		r.push_back({ o + offsetof(vmdf::Material, constantData), sizeof(vmdf::Material::constantData), n + ".constantData", 'f' });
		for (int ps = 0; ps < vmdf::Material::renderPasses_max_count; ps++)
		{
			size_t b = o + offsetof(vmdf::Material, renderPasses) + (size_t)ps * sizeof(vmdf::RenderPassData);
			std::string pn = n + ".renderPasses[" + std::to_string(ps) + "]";
			r.push_back({ b, 64, pn + ".effectName", 's' });
			for (int c = 0; c < vmdf::RenderPassData::constants_max_count; c++)
			{
				size_t cb = b + 64 + (size_t)c * sizeof(vmdf::ConstantData);
				r.push_back({ cb, 40, pn + ".constants[" + std::to_string(c) + "].bufferName", 's' });
				r.push_back({ cb + 40, 8, pn + ".constants[" + std::to_string(c) + "].offset/size", 'u' });
			}
			r.push_back({ b + 352, 4, pn + ".constants_count", 'u' });
		}
		r.push_back({ o + offsetof(vmdf::Material, constantIndexes), 24, n + ".constantIndexes", 'u' });
	}
	for (size_t i = 0; i < p.meshOffsets.size(); i++)
	{
		std::string n = "mesh" + std::to_string(i);
		size_t o = p.meshOffsets[i];
		r.push_back({ o, 40, n + ".name", 's' });
		r.push_back({ o + 40, 8, n + ".shapeRefIndex/shadow", 'u' });
		r.push_back({ o + 48, 40, n + ".materialRefName", 's' });
		r.push_back({ o + 88, 12, n + ".materialRefIndex/priority/visible", 'u' });
	}
	if (p.skeletonOffset)
	{
		size_t o = p.skeletonOffset;
		r.push_back({ o, 4, "skeleton.bonesNum", 'u' });
		r.push_back({ o + 4, 40, "skeleton.rootBoneName", 's' });
		r.push_back({ o + 44, 8, "skeleton.rootBoneIndex/isTransAnimEnabled", 'u' });
		o += sizeof(vmdf::Skeleton);
		for (uint32_t b = 0; b < p.boneCount; b++, o += sizeof(vmdf::Bone))
		{
			std::string n = "bone" + std::to_string(b);
			r.push_back({ o, 40, n + ".name", 's' });
			r.push_back({ o + 40, 40, n + ".parentName", 's' });
			r.push_back({ o + 80, 12, n + ".parentIndex/rendering/billboard", 'u' });
			r.push_back({ o + 92, 12, n + ".scale", 'f' });
			r.push_back({ o + 104, 12, n + ".rotate", 'f' });
			r.push_back({ o + 116, 12, n + ".translate", 'f' });
			r.push_back({ o + 128, 64, n + ".transform", 'f' });
			r.push_back({ o + 192, 16, n + ".boundingSphere", 'f' });
			r.push_back({ o + 208, 64, n + ".bindPoseTrans", 'f' });
			r.push_back({ o + 272, 64, n + ".invBindPoseTrans", 'f' });
		}
	}
	std::sort(r.begin(), r.end(), [](const Region& a, const Region& b) { return a.off < b.off; });
}

bool buildMask(const std::vector<uint8_t>& d, const Parsed& p, std::vector<uint8_t>& mask)
{
	mask.assign(d.size(), 1);
	const vmdf::ModelHeader& h = p.header;
	clearStringTail(d, mask, offsetof(vmdf::ModelHeader, name), 64);
	clearRange(mask, offsetof(vmdf::ModelHeader, rigidBoneIndices) + 4 * (size_t)std::min<uint32_t>(h.rigidBoneIndices_count, 255), 4 * (255 - (size_t)std::min<uint32_t>(h.rigidBoneIndices_count, 255)));
	clearRange(mask, offsetof(vmdf::ModelHeader, skinnedBoneIndices) + 4 * (size_t)std::min<uint32_t>(h.skinnedBoneIndices_count, 255), 4 * (255 - (size_t)std::min<uint32_t>(h.skinnedBoneIndices_count, 255)));
	for (const ShapeInfo& si : p.shapes)
	{
		size_t o = si.offset;
		for (uint32_t k = 0; k < vmdf::Shape::streamInfo_max_count; k++)
		{
			size_t b = o + offsetof(vmdf::Shape, streamInfo) + k * sizeof(vmdf::VertexStreamInfo);
			if (k >= si.s.streamInfo_count)
				clearRange(mask, b, sizeof(vmdf::VertexStreamInfo));
			else
				clearStringTail(d, mask, b + offsetof(vmdf::VertexStreamInfo, usageHint), 40);
		}
		for (uint32_t k = 0; k < vmdf::Shape::singleAttributes_max_count; k++)
		{
			size_t b = o + offsetof(vmdf::Shape, singleAttributes) + k * sizeof(vmdf::SingleAttribute);
			if (k >= si.s.singleAttributes_count)
				clearRange(mask, b, sizeof(vmdf::SingleAttribute));
			else
				clearStringTail(d, mask, b + offsetof(vmdf::SingleAttribute, usageHint), 40);
		}
		clearRange(mask, si.adjacencyOffset, si.s.primitive.adjacencyStream.sizeAligned);
	}
	for (size_t mo : p.materialOffsets)
	{
		vmdf::Material m;
		readAt(d, mo, m);
		clearStringTail(d, mask, mo, 40);
		uint32_t named = 0;
		for (int t = 0; t < vmdf::Material::textures_max_count; t++)
		{
			size_t b = mo + offsetof(vmdf::Material, textures) + (size_t)t * sizeof(vmdf::Texture);
			clearStringTail(d, mask, b, 40);
			clearStringTail(d, mask, b + offsetof(vmdf::Texture, textureHint), 40);
			if (m.textures[t].textureName[0])
				named++;
		}
		for (uint32_t t = 0; t < vmdf::Material::textureCoordinators_max_count; t++)
		{
			size_t b = mo + offsetof(vmdf::Material, textureCoordinators) + t * sizeof(vmdf::TextureCoordinator);
			if (t >= named)
				clearRange(mask, b, sizeof(vmdf::TextureCoordinator));
			else
				clearRange(mask, b + offsetof(vmdf::TextureCoordinator, refCamera), 4); // never set
		}
		// constant data: only what the passes' constant sets cover
		std::vector<uint8_t> used(sizeof(m.constantData), 0);
		for (int ps = 0; ps < vmdf::Material::renderPasses_max_count; ps++)
		{
			const vmdf::RenderPassData& rp = m.renderPasses[ps];
			size_t b = mo + offsetof(vmdf::Material, renderPasses) + (size_t)ps * sizeof(vmdf::RenderPassData);
			clearStringTail(d, mask, b, 64);
			uint32_t count = std::min<uint32_t>(rp.constants_count, vmdf::RenderPassData::constants_max_count);
			for (uint32_t c = 0; c < vmdf::RenderPassData::constants_max_count; c++)
			{
				size_t cb = b + offsetof(vmdf::RenderPassData, constants) + c * sizeof(vmdf::ConstantData);
				if (c >= count)
				{
					clearRange(mask, cb, sizeof(vmdf::ConstantData));
					continue;
				}
				clearStringTail(d, mask, cb, 40);
				for (uint32_t x = rp.constants[c].uOffset; x < rp.constants[c].uOffset + rp.constants[c].uSize && x < used.size(); x++)
					used[x] = 1;
			}
		}
		for (size_t x = 0; x < used.size(); x++)
			if (!used[x])
				mask[mo + offsetof(vmdf::Material, constantData) + x] = 0;
		clearRange(mask, mo + offsetof(vmdf::Material, constantIndexes), sizeof(m.constantIndexes));
	}
	for (size_t o : p.meshOffsets)
	{
		clearStringTail(d, mask, o, 40);
		clearStringTail(d, mask, o + offsetof(vmdf::Mesh, materialRefName), 40);
	}
	if (p.skeletonOffset)
	{
		size_t o = p.skeletonOffset;
		clearStringTail(d, mask, o + offsetof(vmdf::Skeleton, rootBoneName), 40);
		o += sizeof(vmdf::Skeleton);
		for (uint32_t b = 0; b < p.boneCount; b++, o += sizeof(vmdf::Bone))
		{
			clearStringTail(d, mask, o, 40);
			clearStringTail(d, mask, o + 40, 40);
		}
	}
	return true;
}

std::string hexFloat(const uint8_t* p)
{
	float f;
	uint32_t u;
	memcpy(&f, p, 4);
	memcpy(&u, p, 4);
	char buf[64];
	snprintf(buf, sizeof(buf), "%.9g(%08x)", (double)f, u);
	return buf;
}

std::string fieldString(const uint8_t* p, size_t n)
{
	return std::string((const char*)p, strnlen((const char*)p, n));
}

// Geometry equivalence of one shape: same triangles (each a tuple of
// vertex records, rotation-normalised), in any order.
struct GeomReport
{
	bool sameTriangles = false;
	size_t matched = 0, unmatched = 0;
	std::map<std::string, double> maxDiff;
};

std::vector<std::string> triangleKeys(const std::vector<uint8_t>& d, const ShapeInfo& si, bool positionOnly)
{
	std::vector<std::string> out;
	if (si.s.primitive.indexStream.formatSize != 4)
		return out;
	const uint32_t* idx = (const uint32_t*)(d.data() + si.indexOffset);
	uint32_t n = si.s.primitive.indexStream.indexNum;
	size_t recLen = positionOnly ? 12 : si.stride;
	for (uint32_t t = 0; t + 2 < n; t += 3)
	{
		std::string v[3];
		for (int k = 0; k < 3; k++)
		{
			uint32_t vi = idx[t + (uint32_t)k];
			if (vi >= si.s.vertexNum)
				return std::vector<std::string>();
			v[k].assign((const char*)d.data() + si.vertexOffset + (size_t)vi * si.stride, recLen);
		}
		int first = 0;
		for (int k = 1; k < 3; k++)
			if (v[k] < v[first])
				first = k;
		out.push_back(v[first] + v[(first + 1) % 3] + v[(first + 2) % 3]);
	}
	return out;
}

GeomReport compareGeometry(const std::vector<uint8_t>& a, const ShapeInfo& sa, const std::vector<uint8_t>& b, const ShapeInfo& sb)
{
	GeomReport r;
	if (sa.stride != sb.stride || sa.layout != sb.layout)
		return r;
	std::vector<std::string> ka = triangleKeys(a, sa, false), kb = triangleKeys(b, sb, false);
	std::sort(ka.begin(), ka.end());
	std::sort(kb.begin(), kb.end());
	r.sameTriangles = !ka.empty() && ka == kb;
	if (r.sameTriangles)
	{
		r.matched = ka.size();
		return r;
	}
	// match by position triples and measure attribute differences
	std::vector<std::string> pa = triangleKeys(a, sa, true), pb = triangleKeys(b, sb, true);
	std::vector<std::string> fa = triangleKeys(a, sa, false), fb = triangleKeys(b, sb, false);
	if (pa.size() != fa.size() || pb.size() != fb.size())
		return r;
	std::multimap<std::string, size_t> byPos;
	for (size_t i = 0; i < pb.size(); i++)
		byPos.insert(std::make_pair(pb[i], i));
	for (size_t i = 0; i < pa.size(); i++)
	{
		auto it = byPos.find(pa[i]);
		if (it == byPos.end())
		{
			r.unmatched++;
			continue;
		}
		const std::string& ta = fa[i];
		const std::string& tb = fb[it->second];
		byPos.erase(it);
		r.matched++;
		for (int k = 0; k < 3; k++)
		{
			for (const auto& l : sa.layout)
			{
				uint32_t cols = l.second.second & 0xffff, type = l.second.second >> 16;
				for (uint32_t c = 0; c < cols; c++)
				{
					size_t off = (size_t)k * sa.stride + l.second.first + c * elementSize(type);
					double va, vb;
					if (type == vmdf::VE_FLOAT)
					{
						float x, y;
						memcpy(&x, ta.data() + off, 4);
						memcpy(&y, tb.data() + off, 4);
						va = x;
						vb = y;
					}
					else
					{
						va = (uint8_t)ta[off];
						vb = (uint8_t)tb[off];
					}
					double diff = std::fabs(va - vb);
					double& m = r.maxDiff[l.first];
					if (diff > m)
						m = diff;
				}
			}
		}
	}
	r.unmatched += byPos.size();
	return r;
}

} // namespace

const char* VerdictName(CompareVerdict v)
{
	switch (v)
	{
	case CompareVerdict::Identical: return "IDENTICAL";
	case CompareVerdict::EquivalentMasked: return "EQUIVALENT (only never-initialised bytes differ)";
	case CompareVerdict::EquivalentGeometry: return "EQUIVALENT (same triangles, different vertex/index order)";
	case CompareVerdict::Different: return "DIFFERENT";
	default: return "UNREADABLE";
	}
}

bool DefinedByteMask(const std::vector<uint8_t>& data, std::vector<uint8_t>& mask)
{
	Parsed p;
	if (!parse(data, p))
		return false;
	return buildMask(data, p, mask);
}

CompareVerdict CompareVmdf(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& ref, FILE* out, int maxLines)
{
	if (ours == ref)
	{
		fprintf(out, "compare: %s (%zu bytes)\n", VerdictName(CompareVerdict::Identical), ours.size());
		return CompareVerdict::Identical;
	}
	Parsed pa, pb;
	if (!parse(ours, pa) || !parse(ref, pb))
	{
		fprintf(out, "compare: can't parse (%s%s)\n", pa.error.c_str(), pb.error.c_str());
		return CompareVerdict::Unreadable;
	}
	std::vector<uint8_t> ma, mb;
	buildMask(ours, pa, ma);
	buildMask(ref, pb, mb);

	int lines = 0;
	auto line = [&](const std::string& s) {
		if (lines++ < maxLines)
			fprintf(out, "  %s\n", s.c_str());
	};

	bool structureSame = ours.size() == ref.size() && pa.shapes.size() == pb.shapes.size() && pa.materialOffsets.size() == pb.materialOffsets.size() &&
	                     pa.meshOffsets.size() == pb.meshOffsets.size() && pa.boneCount == pb.boneCount;
	if (!structureSame)
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "structure differs: size %zu vs %zu, shapes %zu vs %zu, materials %zu vs %zu, meshes %zu vs %zu, bones %u vs %u", ours.size(), ref.size(), pa.shapes.size(),
		         pb.shapes.size(), pa.materialOffsets.size(), pb.materialOffsets.size(), pa.meshOffsets.size(), pb.meshOffsets.size(), pa.boneCount, pb.boneCount);
		line(buf);
		for (size_t i = 0; i < std::min(pa.shapes.size(), pb.shapes.size()); i++)
		{
			const ShapeInfo &sa = pa.shapes[i], &sb = pb.shapes[i];
			snprintf(buf, sizeof(buf), "shape%zu: vertices %u vs %u, indices %u vs %u, streams %u vs %u", i, sa.s.vertexNum, sb.s.vertexNum, sa.s.primitive.indexStream.indexNum,
			         sb.s.primitive.indexStream.indexNum, sa.s.streamInfo_count, sb.s.streamInfo_count);
			line(buf);
			if (sa.layout == sb.layout)
			{
				GeomReport g = compareGeometry(ours, sa, ref, sb);
				std::string s = "shape" + std::to_string(i) + ": triangles matched by position " + std::to_string(g.matched) + ", unmatched " + std::to_string(g.unmatched);
				for (const auto& md : g.maxDiff)
				{
					char b2[96];
					snprintf(b2, sizeof(b2), ", max |d%s| %.3g", md.first.c_str(), md.second);
					s += b2;
				}
				line(s);
			}
		}
		fprintf(out, "compare: %s\n", VerdictName(CompareVerdict::Different));
		return CompareVerdict::Different;
	}

	std::vector<Region> regions;
	addRegions(pa, regions);
	bool anyDefined = false;
	bool geometryOnly = true;
	std::vector<bool> shapeGeomDiffers(pa.shapes.size(), false);
	for (const Region& r : regions)
	{
		size_t diffs = 0, firstDiff = SIZE_MAX;
		for (size_t i = r.off; i < r.off + r.len && i < ours.size(); i++)
		{
			if (ma[i] && mb[i] && ours[i] != ref[i])
			{
				diffs++;
				if (firstDiff == SIZE_MAX)
					firstDiff = i;
			}
		}
		if (!diffs)
			continue;
		anyDefined = true;
		bool isGeom = r.name.find(".indexStream") != std::string::npos || r.name.find(".vertexStream") != std::string::npos;
		if (isGeom)
		{
			size_t s = (size_t)atoi(r.name.c_str() + 5);
			if (s < shapeGeomDiffers.size())
				shapeGeomDiffers[s] = true;
		}
		else
		{
			geometryOnly = false;
		}
		std::string detail = r.name + ": " + std::to_string(diffs) + " byte(s) differ";
		if (r.kind == 's')
		{
			detail += " ('" + fieldString(ours.data() + r.off, r.len) + "' vs '" + fieldString(ref.data() + r.off, r.len) + "')";
		}
		else if (!isGeom)
		{
			size_t w = r.off + ((firstDiff - r.off) & ~(size_t)3);
			if (r.kind == 'f')
				detail += " (first at +" + std::to_string(w - r.off) + ": " + hexFloat(ours.data() + w) + " vs " + hexFloat(ref.data() + w) + ")";
			else
			{
				uint32_t ua, ub;
				memcpy(&ua, ours.data() + w, 4);
				memcpy(&ub, ref.data() + w, 4);
				detail += " (first at +" + std::to_string(w - r.off) + ": " + std::to_string(ua) + " vs " + std::to_string(ub) + ")";
			}
		}
		line(detail);
	}
	if (!anyDefined)
	{
		size_t dontCare = 0;
		for (size_t i = 0; i < ours.size(); i++)
			if (ours[i] != ref[i])
				dontCare++;
		fprintf(out, "compare: %s (%zu never-initialised bytes differ)\n", VerdictName(CompareVerdict::EquivalentMasked), dontCare);
		return CompareVerdict::EquivalentMasked;
	}
	bool allGeomSame = true;
	for (size_t s = 0; s < pa.shapes.size(); s++)
	{
		if (!shapeGeomDiffers[s])
			continue;
		GeomReport g = compareGeometry(ours, pa.shapes[s], ref, pb.shapes[s]);
		if (g.sameTriangles)
		{
			line("shape" + std::to_string(s) + ": same " + std::to_string(g.matched) + " triangles in a different order");
		}
		else
		{
			allGeomSame = false;
			std::string str = "shape" + std::to_string(s) + ": triangles matched by position " + std::to_string(g.matched) + ", unmatched " + std::to_string(g.unmatched);
			for (const auto& md : g.maxDiff)
			{
				char b2[96];
				snprintf(b2, sizeof(b2), ", max |d%s| %.3g", md.first.c_str(), md.second);
				str += b2;
			}
			line(str);
		}
	}
	CompareVerdict v = (geometryOnly && allGeomSame) ? CompareVerdict::EquivalentGeometry : CompareVerdict::Different;
	if (lines > maxLines)
		fprintf(out, "  ... %d more\n", lines - maxLines);
	fprintf(out, "compare: %s\n", VerdictName(v));
	return v;
}

bool DumpVmdf(const std::vector<uint8_t>& d, FILE* out, bool verbose)
{
	Parsed p;
	if (!parse(d, p))
	{
		fprintf(out, "can't parse: %s\n", p.error.c_str());
		return false;
	}
	const vmdf::ModelHeader& h = p.header;
	fprintf(out, "model '%s' alignment %u size %zu\n", fieldString((const uint8_t*)h.name, 64).c_str(), h.alignment, d.size());
	fprintf(out, "  sphere (%.6g %.6g %.6g) r %.6g\n", h.boundingSphere.center.x, h.boundingSphere.center.y, h.boundingSphere.center.z, h.boundingSphere.radius);
	fprintf(out, "  shapes %u materials %u meshes %u rigid %u skinned %u\n", h.shapeNum, h.materialNum, h.meshNum, h.rigidBoneIndices_count, h.skinnedBoneIndices_count);
	for (size_t i = 0; i < p.shapes.size(); i++)
	{
		const ShapeInfo& si = p.shapes[i];
		fprintf(out, "  shape%zu: %u vertices, %u indices, root bone %u, lod %u, skinning %u, mask 0x%x, sphere (%.6g %.6g %.6g) r %.6g\n", i, si.s.vertexNum,
		        si.s.primitive.indexStream.indexNum, si.s.primitive.rootBone, si.s.primitive.lodLevel, si.s.skinningType, si.s.attributeMask, si.s.boundingSphere.center.x,
		        si.s.boundingSphere.center.y, si.s.boundingSphere.center.z, si.s.boundingSphere.radius);
		for (uint32_t k = 0; k < si.s.streamInfo_count; k++)
		{
			const vmdf::VertexStreamInfo& vi = si.s.streamInfo[k];
			fprintf(out, "    stream %u '%s' attr %u cols %u type %u\n", k, fieldString((const uint8_t*)vi.usageHint, 40).c_str(), vi.attribute, vi.columns, vi.elementType);
		}
		if (verbose)
		{
			for (uint32_t v = 0; v < si.s.vertexNum && v < 8; v++)
			{
				fprintf(out, "     v%u", v);
				for (const auto& l : si.layout)
				{
					uint32_t cols = l.second.second & 0xffff, type = l.second.second >> 16;
					fprintf(out, " %s=", l.first.c_str());
					for (uint32_t c = 0; c < cols; c++)
					{
						const uint8_t* q = d.data() + si.vertexOffset + (size_t)v * si.stride + l.second.first + c * elementSize(type);
						if (type == vmdf::VE_FLOAT)
						{
							float f;
							memcpy(&f, q, 4);
							fprintf(out, "%s%.6g", c ? "," : "", f);
						}
						else
							fprintf(out, "%s%u", c ? "," : "", *q);
					}
				}
				fprintf(out, "\n");
			}
		}
	}
	for (size_t i = 0; i < p.materialOffsets.size(); i++)
	{
		vmdf::Material m;
		readAt(d, p.materialOffsets[i], m);
		fprintf(out, "  material%zu '%s' translucency %u cull %u blend %u\n", i, fieldString((const uint8_t*)m.materialName, 40).c_str(), m.attribute.translucencyKind,
		        m.rasterizer.cullFace, m.rasterizer.blendEnabled);
		for (int t = 0; t < vmdf::Material::textures_max_count; t++)
		{
			const vmdf::Texture& tx = m.textures[t];
			fprintf(out, "    tex%d '%s' hint '%s' wrap %u/%u filter %u/%u/%u aniso %u\n", t, fieldString((const uint8_t*)tx.textureName, 40).c_str(),
			        fieldString((const uint8_t*)tx.textureHint, 40).c_str(), tx.wrapS, tx.wrapT, tx.minFilter, tx.magFilter, tx.mipFilter, tx.anisoLevel);
		}
		for (int ps = 0; ps < vmdf::Material::renderPasses_max_count; ps++)
		{
			const vmdf::RenderPassData& rp = m.renderPasses[ps];
			fprintf(out, "    pass%d '%s'", ps, fieldString((const uint8_t*)rp.effectName, 64).c_str());
			for (uint32_t c = 0; c < rp.constants_count && c < 6; c++)
				fprintf(out, " [%s @%u +%u]", fieldString((const uint8_t*)rp.constants[c].bufferName, 40).c_str(), rp.constants[c].uOffset, rp.constants[c].uSize);
			fprintf(out, "\n");
		}
	}
	for (size_t i = 0; i < p.meshOffsets.size(); i++)
	{
		vmdf::Mesh m;
		readAt(d, p.meshOffsets[i], m);
		fprintf(out, "  mesh%zu '%s' shape %u material '%s' (%u)\n", i, fieldString((const uint8_t*)m.name, 40).c_str(), m.shapeRefIndex,
		        fieldString((const uint8_t*)m.materialRefName, 40).c_str(), m.materialRefIndex);
	}
	if (p.skeletonOffset)
	{
		vmdf::Skeleton sk;
		readAt(d, p.skeletonOffset, sk);
		fprintf(out, "  skeleton: %u bones, root '%s'\n", sk.bonesNum, fieldString((const uint8_t*)sk.rootBoneName, 40).c_str());
		size_t o = p.skeletonOffset + sizeof(vmdf::Skeleton);
		for (uint32_t b = 0; b < sk.bonesNum; b++, o += sizeof(vmdf::Bone))
		{
			vmdf::Bone bone;
			readAt(d, o, bone);
			fprintf(out, "    bone%u '%s' parent '%s' (%d) rendering %u t (%.6g %.6g %.6g) r (%.6g %.6g %.6g) s (%.6g %.6g %.6g)\n", b, fieldString((const uint8_t*)bone.name, 40).c_str(),
			        fieldString((const uint8_t*)bone.parentName, 40).c_str(), (int)bone.parentIndex, bone.isNeededRendering, bone.translate.x, bone.translate.y, bone.translate.z,
			        bone.rotate.x, bone.rotate.y, bone.rotate.z, bone.scale.x, bone.scale.y, bone.scale.z);
		}
	}
	return true;
}
