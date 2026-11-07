/****************************************************************************
//	fbx-to-vmdf
//	ufbx-backed scene import with FBX SDK-compatible geometry processing.
*****************************************************************************/
#include "FbxScene.h"
#include "Common.h"

#include "ufbx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace fbx {

// ------------------------------------------------------------ DOM helpers
namespace {

const ufbx_dom_node* child(const ufbx_dom_node* n, const char* name)
{
	return n ? ufbx_dom_find(n, name) : nullptr;
}

std::string domString(const ufbx_dom_node* n)
{
	if (!n || n->values.count == 0)
		return std::string();
	const ufbx_dom_value& v = n->values.data[0];
	if (v.type == UFBX_DOM_VALUE_STRING)
		return std::string(v.value_str.data, v.value_str.length);
	return std::string();
}

std::vector<double> domDoubles(const ufbx_dom_node* n)
{
	std::vector<double> out;
	if (!n || n->values.count == 0)
		return out;
	const ufbx_dom_value& v = n->values.data[0];
	switch (v.type)
	{
	case UFBX_DOM_VALUE_ARRAY_F64:
	{
		ufbx_double_list l = ufbx_dom_as_double_list(n);
		out.assign(l.data, l.data + l.count);
		break;
	}
	case UFBX_DOM_VALUE_ARRAY_F32:
	{
		ufbx_float_list l = ufbx_dom_as_float_list(n);
		out.reserve(l.count);
		for (size_t i = 0; i < l.count; i++)
			out.push_back((double)l.data[i]);
		break;
	}
	case UFBX_DOM_VALUE_ARRAY_I32:
	{
		ufbx_int32_list l = ufbx_dom_as_int32_list(n);
		for (size_t i = 0; i < l.count; i++)
			out.push_back((double)l.data[i]);
		break;
	}
	case UFBX_DOM_VALUE_ARRAY_I64:
	{
		ufbx_int64_list l = ufbx_dom_as_int64_list(n);
		for (size_t i = 0; i < l.count; i++)
			out.push_back((double)l.data[i]);
		break;
	}
	case UFBX_DOM_VALUE_NUMBER:
		for (size_t i = 0; i < n->values.count; i++)
			out.push_back(n->values.data[i].value_float);
		break;
	default:
		break;
	}
	return out;
}

std::vector<int> domInts(const ufbx_dom_node* n)
{
	std::vector<int> out;
	if (!n || n->values.count == 0)
		return out;
	const ufbx_dom_value& v = n->values.data[0];
	switch (v.type)
	{
	case UFBX_DOM_VALUE_ARRAY_I32:
	{
		ufbx_int32_list l = ufbx_dom_as_int32_list(n);
		out.assign(l.data, l.data + l.count);
		break;
	}
	case UFBX_DOM_VALUE_ARRAY_I64:
	{
		ufbx_int64_list l = ufbx_dom_as_int64_list(n);
		for (size_t i = 0; i < l.count; i++)
			out.push_back((int)l.data[i]);
		break;
	}
	case UFBX_DOM_VALUE_NUMBER:
		for (size_t i = 0; i < n->values.count; i++)
			out.push_back((int)n->values.data[i].value_int);
		break;
	default:
	{
		std::vector<double> d = domDoubles(n);
		for (double x : d)
			out.push_back((int)x);
		break;
	}
	}
	return out;
}

Mapping parseMapping(const std::string& s)
{
	if (s == "ByPolygonVertex") return eByPolygonVertex;
	if (s == "ByVertice" || s == "ByVertex" || s == "ByControlPoint") return eByControlPoint;
	if (s == "ByPolygon") return eByPolygon;
	if (s == "ByEdge") return eByEdge;
	if (s == "AllSame") return eAllSame;
	return eNone;
}

Reference parseReference(const std::string& s)
{
	if (s == "IndexToDirect" || s == "Index_to_Direct") return eIndexToDirect;
	if (s == "Index") return eIndex;
	return eDirect;
}

// Reads every <elementName> child of a Geometry node, in TypedIndex order.
std::vector<LayerElement> readLayers(const ufbx_dom_node* geom, const char* elementName, const char* dataName, const char* indexName, int comps)
{
	std::vector<std::pair<int64_t, LayerElement>> found;
	for (size_t i = 0; i < geom->children.count; i++)
	{
		const ufbx_dom_node* c = geom->children.data[i];
		if (std::string(c->name.data, c->name.length) != elementName)
			continue;
		LayerElement le;
		le.comps = comps;
		le.name = domString(child(c, "Name"));
		le.mapping = parseMapping(domString(child(c, "MappingInformationType")));
		le.reference = parseReference(domString(child(c, "ReferenceInformationType")));
		if (comps > 0)
			le.direct = domDoubles(child(c, dataName));
		if (indexName)
			le.index = domInts(child(c, indexName));
		if (comps == 0)
		{
			// material indices: the "direct" data is the index list
			le.index = domInts(child(c, dataName));
		}
		int64_t typedIndex = c->values.count > 0 ? c->values.data[0].value_int : (int64_t)found.size();
		found.push_back(std::make_pair(typedIndex, le));
	}
	std::stable_sort(found.begin(), found.end(), [](const std::pair<int64_t, LayerElement>& a, const std::pair<int64_t, LayerElement>& b) { return a.first < b.first; });
	std::vector<LayerElement> out;
	for (auto& f : found)
		out.push_back(f.second);
	return out;
}

fbxm::Vec4 V(const ufbx_vec3& v, double w = 0.0) { return fbxm::Vec4(v.x, v.y, v.z, w); }

fbxm::AMatrix fromUfbx(const ufbx_matrix& m)
{
	fbxm::AMatrix r;
	for (int j = 0; j < 4; j++)
	{
		r.d[j][0] = m.cols[j].x;
		r.d[j][1] = m.cols[j].y;
		r.d[j][2] = m.cols[j].z;
		r.d[j][3] = j == 3 ? 1.0 : 0.0;
	}
	return r;
}

// A Matrix property stored as 16 doubles (TransformLink, Pose Matrix)
bool domMatrix(const ufbx_dom_node* n, fbxm::AMatrix& out)
{
	std::vector<double> d = domDoubles(n);
	if (d.size() != 16)
		return false;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			out.d[i][j] = d[(size_t)(i * 4 + j)];
	return true;
}

} // namespace

// ------------------------------------------------------------ LayerElement
void LayerElement::GetDirect(int i, double* out) const
{
	if (i < 0 || (size_t)i >= DirectCount())
	{
		for (int c = 0; c < comps; c++)
			out[c] = 0.0;
		return;
	}
	for (int c = 0; c < comps; c++)
		out[c] = direct[(size_t)i * (size_t)comps + (size_t)c];
}

bool LayerElement::Get(int controlPoint, int polygonVertex, double* out) const
{
	int idx;
	if (mapping == eByControlPoint)
		idx = controlPoint;
	else if (mapping == eByPolygonVertex)
		idx = polygonVertex;
	else
		return false;
	if (reference == eIndexToDirect)
		idx = (idx >= 0 && (size_t)idx < index.size()) ? index[(size_t)idx] : -1;
	else if (reference != eDirect)
		return false;
	GetDirect(idx, out);
	return true;
}

// -------------------------------------------------------------------- Mesh
int Mesh::GetTextureUVIndex(int p, int v) const
{
	if (uvs.empty())
		return -1;
	const LayerElement& uv = uvs[0];
	int corner = polygonStart[p] + v;
	if (uv.mapping == eByPolygonVertex && uv.reference == eIndexToDirect)
		return (corner >= 0 && (size_t)corner < uv.index.size()) ? uv.index[(size_t)corner] : -1;
	if (uv.mapping == eByControlPoint && uv.reference == eIndexToDirect)
		return GetPolygonVertex(p, v);
	return -1;
}

namespace {

double dist2(const fbxm::Vec4& a, const fbxm::Vec4& b)
{
	double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

// Local corner orders of the triangles a polygon becomes.
void triangulatePolygon(const Mesh& m, int p, std::vector<int>& out)
{
	int n = m.GetPolygonSize(p);
	out.clear();
	if (n == 3)
	{
		out = { 0, 1, 2 };
		return;
	}
	if (n == 4)
	{
		const fbxm::Vec4& p0 = m.controlPoints[(size_t)m.GetPolygonVertex(p, 0)];
		const fbxm::Vec4& p1 = m.controlPoints[(size_t)m.GetPolygonVertex(p, 1)];
		const fbxm::Vec4& p2 = m.controlPoints[(size_t)m.GetPolygonVertex(p, 2)];
		const fbxm::Vec4& p3 = m.controlPoints[(size_t)m.GetPolygonVertex(p, 3)];
		// split along the shorter diagonal (0-2 on a tie)
		if (dist2(p1, p3) < dist2(p0, p2))
			out = { 1, 2, 3, 1, 3, 0 };
		else
			out = { 0, 1, 2, 0, 2, 3 };
		return;
	}
	for (int i = 1; i + 1 < n; i++)
	{
		out.push_back(0);
		out.push_back(i);
		out.push_back(i + 1);
	}
}

void remapPolygonVertexLayer(LayerElement& le, const std::vector<int>& cornerMap)
{
	if (le.mapping == eByPolygonVertex)
	{
		if (le.reference == eIndexToDirect)
		{
			std::vector<int> idx;
			idx.reserve(cornerMap.size());
			for (int c : cornerMap)
				idx.push_back(c >= 0 && (size_t)c < le.index.size() ? le.index[(size_t)c] : -1);
			le.index.swap(idx);
		}
		else
		{
			std::vector<double> d;
			d.reserve(cornerMap.size() * (size_t)le.comps);
			std::vector<double> tmp((size_t)std::max(le.comps, 1));
			for (int c : cornerMap)
			{
				le.GetDirect(c, tmp.data());
				d.insert(d.end(), tmp.begin(), tmp.begin() + le.comps);
			}
			le.direct.swap(d);
		}
	}
}

void remapPolygonLayer(LayerElement& le, const std::vector<int>& polyMap)
{
	if (le.mapping == eByPolygon)
	{
		if (le.comps == 0)
		{
			std::vector<int> idx;
			for (int p : polyMap)
				idx.push_back(p >= 0 && (size_t)p < le.index.size() ? le.index[(size_t)p] : 0);
			le.index.swap(idx);
		}
		else if (le.reference == eIndexToDirect)
		{
			std::vector<int> idx;
			for (int p : polyMap)
				idx.push_back(p >= 0 && (size_t)p < le.index.size() ? le.index[(size_t)p] : -1);
			le.index.swap(idx);
		}
		else
		{
			std::vector<double> d;
			std::vector<double> tmp((size_t)le.comps);
			for (int p : polyMap)
			{
				le.GetDirect(p, tmp.data());
				d.insert(d.end(), tmp.begin(), tmp.end());
			}
			le.direct.swap(d);
		}
	}
}

} // namespace

void Mesh::Triangulate()
{
	bool allTriangles = true;
	for (int p = 0; p < GetPolygonCount(); p++)
	{
		if (GetPolygonSize(p) != 3)
		{
			allTriangles = false;
			break;
		}
	}
	if (allTriangles)
		return;

	std::vector<int> newStart, newVerts, cornerMap, polyMap, local;
	newStart.push_back(0);
	for (int p = 0; p < GetPolygonCount(); p++)
	{
		triangulatePolygon(*this, p, local);
		for (size_t t = 0; t + 2 < local.size(); t += 3)
		{
			for (int k = 0; k < 3; k++)
			{
				int lv = local[t + (size_t)k];
				newVerts.push_back(GetPolygonVertex(p, lv));
				cornerMap.push_back(polygonStart[p] + lv);
			}
			newStart.push_back((int)newVerts.size());
			polyMap.push_back(p);
		}
	}
	for (auto* layers : { &normals, &binormals, &tangents, &uvs, &colors, &materials })
	{
		for (LayerElement& le : *layers)
		{
			remapPolygonVertexLayer(le, cornerMap);
			remapPolygonLayer(le, polyMap);
		}
	}
	polygonStart.swap(newStart);
	polygonVertices.swap(newVerts);
}

std::vector<std::unique_ptr<Mesh>> Mesh::SplitPerMaterial() const
{
	std::vector<std::unique_ptr<Mesh>> out;
	if (materials.empty())
		return out;
	const LayerElement& mat = materials[0];
	if (mat.mapping != eByPolygon)
		return out;
	std::vector<int> used;
	for (int p = 0; p < GetPolygonCount(); p++)
	{
		int m = (size_t)p < mat.index.size() ? mat.index[(size_t)p] : 0;
		if (std::find(used.begin(), used.end(), m) == used.end())
			used.push_back(m);
	}
	std::sort(used.begin(), used.end());
	if (used.size() <= 1)
		return out;
	for (int m : used)
	{
		std::unique_ptr<Mesh> sub(new Mesh());
		sub->skins = skins;
		sub->deformerCount = deformerCount;
		std::vector<int> cpMap(controlPoints.size(), -1), cpList, cornerMap, polyMap;
		sub->polygonStart.push_back(0);
		for (int p = 0; p < GetPolygonCount(); p++)
		{
			int pm = (size_t)p < mat.index.size() ? mat.index[(size_t)p] : 0;
			if (pm != m)
				continue;
			for (int v = 0; v < GetPolygonSize(p); v++)
			{
				int cp = GetPolygonVertex(p, v);
				if (cpMap[(size_t)cp] < 0)
				{
					cpMap[(size_t)cp] = (int)cpList.size();
					cpList.push_back(cp);
				}
				sub->polygonVertices.push_back(cpMap[(size_t)cp]);
				cornerMap.push_back(polygonStart[p] + v);
			}
			sub->polygonStart.push_back((int)sub->polygonVertices.size());
			polyMap.push_back(p);
		}
		for (int cp : cpList)
			sub->controlPoints.push_back(controlPoints[(size_t)cp]);
		auto copyLayers = [&](const std::vector<LayerElement>& src, std::vector<LayerElement>& dst, size_t maxLayers) {
			for (size_t i = 0; i < src.size() && i < maxLayers; i++)
			{
				LayerElement le = src[i];
				if (le.mapping == eByControlPoint)
				{
					if (le.reference == eIndexToDirect)
					{
						std::vector<int> idx;
						for (int cp : cpList)
							idx.push_back((size_t)cp < le.index.size() ? le.index[(size_t)cp] : -1);
						le.index.swap(idx);
					}
					else
					{
						std::vector<double> d, tmp((size_t)le.comps);
						for (int cp : cpList)
						{
							le.GetDirect(cp, tmp.data());
							d.insert(d.end(), tmp.begin(), tmp.end());
						}
						le.direct.swap(d);
					}
				}
				if (le.mapping == eByPolygonVertex)
				{
					// The SDK writes polygon-vertex layers of the new meshes
					// out per corner: index i -> entry i of a copied array.
					std::vector<double> d, tmp((size_t)std::max(le.comps, 1));
					std::vector<int> idx;
					for (size_t c = 0; c < cornerMap.size(); c++)
					{
						double v[4] = { 0, 0, 0, 0 };
						int src = cornerMap[c];
						int cp = polygonVertices[(size_t)src];
						le.Get(cp, src, v);
						d.insert(d.end(), v, v + le.comps);
						idx.push_back((int)c);
					}
					le.direct.swap(d);
					le.index.swap(idx);
					le.reference = eIndexToDirect;
				}
				remapPolygonLayer(le, polyMap);
				dst.push_back(le);
			}
		};
		copyLayers(normals, sub->normals, normals.size());
		copyLayers(uvs, sub->uvs, uvs.size());
		copyLayers(colors, sub->colors, colors.size());
		LayerElement me;
		me.mapping = eAllSame;
		me.reference = eIndexToDirect;
		me.comps = 0;
		me.index.push_back(m);
		sub->materials.push_back(me);
		// The SDK doesn't carry tangents over to the new meshes: it
		// generates one tangent/binormal layer from the first UV set, which
		// is now per corner, so nothing is averaged across triangles.
		if (!tangents.empty() || !binormals.empty())
			sub->GenerateTangentsData(0, true);
		out.push_back(std::move(sub));
	}
	return out;
}

// GenerateTangentsData: per triangle UV gradients, orthogonalised against
// each corner's normal, averaged over the corners that share a control
// point and a UV index, then orthogonalised again.
bool Mesh::GenerateTangentsData(int uvSet, bool overwrite)
{
	if (!overwrite && !tangents.empty())
		return false;
	if ((size_t)uvSet >= uvs.size() || normals.empty())
		return false;
	const LayerElement& uv = uvs[(size_t)uvSet];
	const LayerElement& nrm = normals[0];
	const int corners = (int)polygonVertices.size();

	struct Acc
	{
		double t[3] = { 0, 0, 0 };
		double b[3] = { 0, 0, 0 };
	};
	// control point, UV index, handedness class, normal (bit pattern)
	typedef std::array<int64_t, 6> Key;
	std::map<Key, Acc> groups;
	std::vector<Key> cornerKey((size_t)corners);
	static const int handMode = getenv("FBX2VMDF_TANGENT_HAND") ? atoi(getenv("FBX2VMDF_TANGENT_HAND")) : 3;
	static const int normalKeyMode = getenv("FBX2VMDF_TANGENT_NKEY") ? atoi(getenv("FBX2VMDF_TANGENT_NKEY")) : 1;
	std::vector<std::array<double, 3>> cornerNormal((size_t)corners);
	std::vector<int> cornerHandOf((size_t)corners, 1);

	auto uvKey = [&](int cp, int corner) -> int {
		if (uv.mapping == eByPolygonVertex)
			return uv.reference == eIndexToDirect ? ((size_t)corner < uv.index.size() ? uv.index[(size_t)corner] : -1) : corner;
		if (uv.mapping == eByControlPoint)
			return uv.reference == eIndexToDirect ? ((size_t)cp < uv.index.size() ? uv.index[(size_t)cp] : -1) : cp;
		return corner;
	};

	for (int p = 0; p < GetPolygonCount(); p++)
	{
		int n = GetPolygonSize(p);
		if (n != 3)
			continue;
		double P[3][3], T[3][2], N[3][4];
		int cps[3];
		for (int k = 0; k < 3; k++)
		{
			int corner = polygonStart[p] + k;
			cps[k] = GetPolygonVertex(p, k);
			const fbxm::Vec4& cp = controlPoints[(size_t)cps[k]];
			P[k][0] = cp[0]; P[k][1] = cp[1]; P[k][2] = cp[2];
			double uvv[2] = { 0, 0 };
			uv.Get(cps[k], corner, uvv);
			T[k][0] = uvv[0]; T[k][1] = uvv[1];
			double nn[4] = { 0, 0, 0, 0 };
			nrm.Get(cps[k], corner, nn);
			N[k][0] = nn[0]; N[k][1] = nn[1]; N[k][2] = nn[2];
		}
		// Per corner: the triangle's UV gradient (from that corner),
		// orthogonalised once against the corner's normal and normalised.
		double O[3][3], hsum = 0.0;
		for (int k = 0; k < 3; k++)
		{
			int i0 = k, i1 = (k + 1) % 3, i2 = (k + 2) % 3;
			double q1[3] = { P[i1][0] - P[i0][0], P[i1][1] - P[i0][1], P[i1][2] - P[i0][2] };
			double q2[3] = { P[i2][0] - P[i0][0], P[i2][1] - P[i0][1], P[i2][2] - P[i0][2] };
			double s1 = T[i1][0] - T[i0][0], t1 = T[i1][1] - T[i0][1];
			double s2 = T[i2][0] - T[i0][0], t2 = T[i2][1] - T[i0][1];
			double det = s1 * t2 - s2 * t1;
			double r = det != 0.0 ? 1.0 / det : 0.0;
			double sd[3], td[3];
			for (int c = 0; c < 3; c++)
			{
				sd[c] = (q1[c] * t2 - q2[c] * t1) * r;
				td[c] = (q2[c] * s1 - q1[c] * s2) * r;
			}
			// No UV area (det == 0): the gradient is zero, so the triangle adds
			// nothing to its corners (the SDK agrees when all three UVs are
			// equal, but makes up a frame by an unknown rule when they lie on a
			// line).
			const double* nn = N[k];
			double dp = nn[0] * sd[0] + nn[1] * sd[1] + nn[2] * sd[2];
			double* o = O[k];
			o[0] = sd[0] - nn[0] * dp;
			o[1] = sd[1] - nn[1] * dp;
			o[2] = sd[2] - nn[2] * dp;
			double len = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
			if (len > 0.0)
			{
				o[0] /= len; o[1] /= len; o[2] /= len;
			}
			// handedness contribution: (N x T) . bitangent direction
			double cx = nn[1] * o[2] - nn[2] * o[1], cy = nn[2] * o[0] - nn[0] * o[2], cz = nn[0] * o[1] - nn[1] * o[0];
			hsum += cx * td[0] + cy * td[1] + cz * td[2];
		}
		// one handedness per triangle, from the sum over its corners
		int faceHand = hsum >= 0.0 ? 1 : -1;
		for (int k = 0; k < 3; k++)
		{
			int corner = polygonStart[p] + k;
			int64_t nb[3] = { 0, 0, 0 };
			if (normalKeyMode == 1)
				memcpy(nb, N[k], sizeof(nb));
			else if (normalKeyMode == 2)
			{
				// the normal's index in its layer
				int ni = nrm.mapping == eByControlPoint ? cps[k] : corner;
				if (nrm.reference == eIndexToDirect)
					ni = (ni >= 0 && (size_t)ni < nrm.index.size()) ? nrm.index[(size_t)ni] : -1;
				nb[0] = ni;
			}
			Key key = { cps[k], uvKey(cps[k], corner), handMode == 0 ? 0 : faceHand, nb[0], nb[1], nb[2] };
			Acc& a = groups[key];
			for (int c = 0; c < 3; c++)
				a.t[c] += O[k][c];
			a.b[0] += faceHand; // handedness votes of the group
			cornerKey[(size_t)corner] = key;
			cornerHandOf[(size_t)corner] = faceHand;
			cornerNormal[(size_t)corner] = { N[k][0], N[k][1], N[k][2] };
		}
	}

	LayerElement tan, bin;
	tan.name = bin.name = uv.name;
	tan.mapping = bin.mapping = eByPolygonVertex;
	tan.reference = bin.reference = eDirect;
	tan.comps = bin.comps = 4;
	tan.direct.assign((size_t)corners * 4, 0.0);
	bin.direct.assign((size_t)corners * 4, 0.0);
	for (int c = 0; c < corners; c++)
	{
		const Acc& a = groups[cornerKey[(size_t)c]];
		const auto& nn = cornerNormal[(size_t)c];
		// the sum of the per-face unit tangents, normalised (no second
		// orthogonalisation: the face tangents already are)
		double t[3] = { a.t[0], a.t[1], a.t[2] };
		double len = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
		if (len > 0.0)
		{
			t[0] /= len; t[1] /= len; t[2] /= len;
		}
		double hand = (double)cornerHandOf[(size_t)c];
		// binormal: normalise(T x N), flipped by the triangle's handedness
		double b[3] = { t[1] * nn[2] - t[2] * nn[1], t[2] * nn[0] - t[0] * nn[2], t[0] * nn[1] - t[1] * nn[0] };
		double blen = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
		if (blen > 0.0)
		{
			b[0] /= blen; b[1] /= blen; b[2] /= blen;
		}
		double* T = &tan.direct[(size_t)c * 4];
		double* B = &bin.direct[(size_t)c * 4];
		T[0] = t[0]; T[1] = t[1]; T[2] = t[2]; T[3] = hand;
		B[0] = -hand * b[0]; B[1] = -hand * b[1]; B[2] = -hand * b[2]; B[3] = 1.0;
	}
	tangents.clear();
	binormals.clear();
	tangents.push_back(tan);
	binormals.push_back(bin);
	return true;
}

// -------------------------------------------------------------------- Node
fbxm::AMatrix Node::EvaluateLocalTransform() const
{
	using fbxm::AMatrix;
	auto rot = [&](const fbxm::Vec4& r, int order) {
		// Euler rotation in the given FBX order (0 = XYZ)
		AMatrix rx = AMatrix::Rotation(fbxm::Vec4(r[0], 0, 0));
		AMatrix ry = AMatrix::Rotation(fbxm::Vec4(0, r[1], 0));
		AMatrix rz = AMatrix::Rotation(fbxm::Vec4(0, 0, r[2]));
		switch (order)
		{
		case 1: return ry * rz * rx; // XZY
		case 2: return rx * rz * ry; // YZX
		case 3: return rz * rx * ry; // YXZ
		case 4: return rx * ry * rz; // ZXY
		case 5: return ry * rx * rz; // ZYX
		default: return AMatrix::Rotation(r); // XYZ
		}
	};
	AMatrix T = AMatrix::Translation(lclT);
	AMatrix Roff = AMatrix::Translation(rotOffset);
	AMatrix Rp = AMatrix::Translation(rotPivot);
	AMatrix RpInv = AMatrix::Translation(fbxm::Vec4(-rotPivot[0], -rotPivot[1], -rotPivot[2]));
	AMatrix Soff = AMatrix::Translation(sclOffset);
	AMatrix Sp = AMatrix::Translation(sclPivot);
	AMatrix SpInv = AMatrix::Translation(fbxm::Vec4(-sclPivot[0], -sclPivot[1], -sclPivot[2]));
	AMatrix S = AMatrix::Scaling(lclS);
	AMatrix R = rot(lclR, rotationOrder);
	bool simple = rotOffset[0] == 0 && rotOffset[1] == 0 && rotOffset[2] == 0 && rotPivot[0] == 0 && rotPivot[1] == 0 && rotPivot[2] == 0 &&
	              sclOffset[0] == 0 && sclOffset[1] == 0 && sclOffset[2] == 0 && sclPivot[0] == 0 && sclPivot[1] == 0 && sclPivot[2] == 0;
	bool noPrePost = !rotationActive || (preR[0] == 0 && preR[1] == 0 && preR[2] == 0 && postR[0] == 0 && postR[1] == 0 && postR[2] == 0);
	if (simple && noPrePost && rotationOrder == 0)
	{
		return AMatrix::TRS(lclT, lclR, lclS);
	}
	AMatrix Rpre, RpostInv;
	if (rotationActive)
	{
		Rpre = AMatrix::Rotation(preR);
		RpostInv = AMatrix::Rotation(postR).Inverse();
	}
	return T * Roff * Rp * Rpre * R * RpostInv * RpInv * Soff * Sp * S * SpInv;
}

fbxm::AMatrix Node::EvaluateGlobalTransform() const
{
	fbxm::AMatrix local = EvaluateLocalTransform();
	if (!parent)
		return local;
	return parent->EvaluateGlobalTransform() * local;
}

// ------------------------------------------------------------------- Scene
Scene::~Scene()
{
	if (m_scene)
		ufbx_free_scene(m_scene);
}

bool Scene::Load(const std::string& path, std::string& error)
{
	ufbx_load_opts opts = {};
	opts.retain_dom = true;
	opts.ignore_missing_external_files = true;
	opts.generate_missing_normals = false;
	opts.evaluate_skinning = false;
	ufbx_error err;
	m_scene = ufbx_load_file(path.c_str(), &opts, &err);
	if (!m_scene)
	{
		char buf[1024];
		ufbx_format_error(buf, sizeof(buf), &err);
		error = buf;
		return false;
	}

	const ufbx_props& gs = m_scene->settings.props;
	m_axis.upAxis = (int)ufbx_find_int(&gs, "UpAxis", 1);
	m_axis.upSign = (int)ufbx_find_int(&gs, "UpAxisSign", 1);
	m_axis.frontAxis = (int)ufbx_find_int(&gs, "FrontAxis", 2);
	m_axis.frontSign = (int)ufbx_find_int(&gs, "FrontAxisSign", 1);
	m_axis.coordAxis = (int)ufbx_find_int(&gs, "CoordAxis", 0);
	m_axis.coordSign = (int)ufbx_find_int(&gs, "CoordAxisSign", 1);
	m_axis.unitScaleFactor = ufbx_find_real(&gs, "UnitScaleFactor", 1.0);
	m_hasAnimation = m_scene->anim_curves.count > 0;

	m_root = Convert(m_scene->root_node, nullptr);
	m_root->name = "RootNode";
	// meshes after every node exists, so skin clusters can link to bones
	for (auto& nm : m_nodeMap)
	{
		for (size_t a = 0; a < nm.first->all_attribs.count; a++)
		{
			const ufbx_element* e = nm.first->all_attribs.data[a];
			if (e->type == UFBX_ELEMENT_MESH)
				nm.second->meshes.push_back(ConvertMesh((const ufbx_mesh*)e));
		}
	}

	for (size_t i = 0; i < m_scene->poses.count; i++)
	{
		const ufbx_pose* pose = m_scene->poses.data[i];
		if (!pose->is_bind_pose)
			continue;
		BindPose bp;
		// prefer the raw 16-double matrices from the DOM
		const ufbx_dom_node* dom = pose->element.dom_node;
		std::vector<const ufbx_dom_node*> poseNodes;
		if (dom)
		{
			for (size_t c = 0; c < dom->children.count; c++)
			{
				const ufbx_dom_node* pn = dom->children.data[c];
				if (std::string(pn->name.data, pn->name.length) == "PoseNode")
					poseNodes.push_back(pn);
			}
		}
		for (size_t b = 0; b < pose->bone_poses.count; b++)
		{
			const ufbx_bone_pose& bpose = pose->bone_poses.data[b];
			Node* node = nullptr;
			for (auto& nm : m_nodeMap)
				if (nm.first == bpose.bone_node)
					node = nm.second;
			if (!node)
				continue;
			fbxm::AMatrix m = fromUfbx(bpose.bone_to_world);
			if (b < poseNodes.size())
			{
				fbxm::AMatrix raw;
				if (domMatrix(child(poseNodes[b], "Matrix"), raw))
					m = raw;
			}
			bp.entries.push_back(std::make_pair(node, m));
		}
		m_bindPoses.push_back(bp);
	}
	return true;
}

Material* Scene::ConvertMaterial(const ufbx_material* mat)
{
	for (auto& mm : m_materialMap)
		if (mm.first == mat)
			return mm.second;
	std::unique_ptr<Material> m(new Material());
	m->name = std::string(mat->element.name.data, mat->element.name.length);
	std::string model(mat->shading_model_name.data, mat->shading_model_name.length);
	for (char& c : model)
		c = (char)tolower((unsigned char)c);
	// FbxSurfacePhong derives from FbxSurfaceLambert; anything else read
	// by the SDK as a plain surface material
	m->isPhong = model == "phong";
	m->isLambert = m->isPhong || model == "lambert";
	const ufbx_props& p = mat->props;
	auto v3 = [&](const char* name, double* out) {
		ufbx_prop* prop = ufbx_find_prop(&p, name);
		if (prop)
		{
			out[0] = prop->value_vec3.x;
			out[1] = prop->value_vec3.y;
			out[2] = prop->value_vec3.z;
		}
	};
	auto r1 = [&](const char* name, double& out) {
		ufbx_prop* prop = ufbx_find_prop(&p, name);
		if (prop)
			out = prop->value_real;
	};
	v3("AmbientColor", m->ambient);
	v3("DiffuseColor", m->diffuse);
	r1("DiffuseFactor", m->diffuseFactor);
	v3("EmissiveColor", m->emissive);
	r1("EmissiveFactor", m->emissiveFactor);
	v3("TransparentColor", m->transparentColor);
	r1("TransparencyFactor", m->transparencyFactor);
	v3("SpecularColor", m->specular);
	r1("SpecularFactor", m->specularFactor);
	r1("ShininessExponent", m->shininess);
	v3("ReflectionColor", m->reflection);
	r1("ReflectionFactor", m->reflectionFactor);

	for (size_t i = 0; i < mat->textures.count; i++)
	{
		const ufbx_material_texture& mt = mat->textures.data[i];
		const ufbx_texture* tex = mt.texture;
		if (!tex)
			continue;
		Material::Texture t;
		t.property = std::string(mt.material_prop.data, mt.material_prop.length);
		t.layered = tex->type == UFBX_TEXTURE_LAYERED;
		t.relativeFileName = std::string(tex->relative_filename.data, tex->relative_filename.length);
		t.wrapU = (int)ufbx_find_int(&tex->props, "WrapModeU", 0);
		t.wrapV = (int)ufbx_find_int(&tex->props, "WrapModeV", 0);
		ufbx_vec3 one = { 1, 1, 1 }, zero = { 0, 0, 0 };
		ufbx_vec3 s = ufbx_find_vec3(&tex->props, "Scaling", one);
		ufbx_vec3 tr = ufbx_find_vec3(&tex->props, "Translation", zero);
		ufbx_vec3 r = ufbx_find_vec3(&tex->props, "Rotation", zero);
		t.scale[0] = s.x; t.scale[1] = s.y;
		t.translation[0] = tr.x; t.translation[1] = tr.y;
		t.rotation = r.x;
		m->textures.push_back(t);
	}
	Material* raw = m.get();
	m_materialMap.push_back(std::make_pair(mat, raw));
	m_materials.push_back(std::move(m));
	return raw;
}

std::unique_ptr<Mesh> Scene::ConvertMesh(const ufbx_mesh* mesh)
{
	std::unique_ptr<Mesh> m(new Mesh());
	const ufbx_dom_node* geom = mesh->element.dom_node;
	if (!geom)
		fatal("internal error: mesh without retained FBX data");
	std::vector<double> verts = domDoubles(child(geom, "Vertices"));
	for (size_t i = 0; i + 2 < verts.size(); i += 3)
		m->controlPoints.push_back(fbxm::Vec4(verts[i], verts[i + 1], verts[i + 2], 1.0));
	std::vector<int> pvi = domInts(child(geom, "PolygonVertexIndex"));
	m->polygonStart.push_back(0);
	for (int v : pvi)
	{
		if (v < 0)
		{
			m->polygonVertices.push_back(~v);
			m->polygonStart.push_back((int)m->polygonVertices.size());
		}
		else
		{
			m->polygonVertices.push_back(v);
		}
	}
	if (m->polygonStart.size() == 1)
		m->polygonStart.clear();
	m->normals = readLayers(geom, "LayerElementNormal", "Normals", "NormalsIndex", 3);
	m->binormals = readLayers(geom, "LayerElementBinormal", "Binormals", "BinormalsIndex", 3);
	m->tangents = readLayers(geom, "LayerElementTangent", "Tangents", "TangentsIndex", 3);
	m->uvs = readLayers(geom, "LayerElementUV", "UV", "UVIndex", 2);
	m->colors = readLayers(geom, "LayerElementColor", "Colors", "ColorIndex", 4);
	m->materials = readLayers(geom, "LayerElementMaterial", "Materials", nullptr, 0);

	m->deformerCount = (int)(mesh->skin_deformers.count + mesh->blend_deformers.count + mesh->cache_deformers.count);
	for (size_t s = 0; s < mesh->skin_deformers.count; s++)
	{
		const ufbx_skin_deformer* sd = mesh->skin_deformers.data[s];
		Skin skin;
		std::string method = sd->element.dom_node ? domString(child(sd->element.dom_node, "SkinningType")) : std::string();
		// FbxSkin defaults to eRigid when the file doesn't say
		skin.rigid = method.empty() || method == "Rigid";
		for (size_t c = 0; c < sd->clusters.count; c++)
		{
			const ufbx_skin_cluster* cl = sd->clusters.data[c];
			SkinCluster sc;
			for (auto& nm : m_nodeMap)
				if (nm.first == cl->bone_node)
					sc.link = nm.second;
			const ufbx_dom_node* cdom = cl->element.dom_node;
			sc.indices = domInts(child(cdom, "Indexes"));
			sc.weights = domDoubles(child(cdom, "Weights"));
			if (!domMatrix(child(cdom, "TransformLink"), sc.transformLink))
				sc.transformLink = fromUfbx(cl->bind_to_world);
			skin.clusters.push_back(sc);
		}
		m->skins.push_back(skin);
	}
	return m;
}

Node* Scene::Convert(const ufbx_node* n, Node* parent)
{
	std::unique_ptr<Node> node(new Node());
	Node* raw = node.get();
	m_nodes.push_back(std::move(node));
	m_nodeMap.push_back(std::make_pair(n, raw));
	raw->parent = parent;
	raw->name = std::string(n->element.name.data, n->element.name.length);
	switch (n->attrib_type)
	{
	case UFBX_ELEMENT_MESH: raw->attrType = eMesh; break;
	case UFBX_ELEMENT_EMPTY: raw->attrType = eNull; break;
	case UFBX_ELEMENT_BONE: raw->attrType = eSkeleton; break;
	case UFBX_ELEMENT_MARKER: raw->attrType = eMarker; break;
	case UFBX_ELEMENT_LIGHT: raw->attrType = eLight; break;
	case UFBX_ELEMENT_CAMERA: raw->attrType = eCamera; break;
	case UFBX_ELEMENT_STEREO_CAMERA: raw->attrType = eCameraStereo; break;
	case UFBX_ELEMENT_CAMERA_SWITCHER: raw->attrType = eCameraSwitcher; break;
	case UFBX_ELEMENT_NURBS_SURFACE: raw->attrType = eNurbs; break;
	default: raw->attrType = eUnknown; break;
	}

	const ufbx_props& p = n->props;
	ufbx_vec3 zero = { 0, 0, 0 }, one = { 1, 1, 1 };
	raw->lclT = V(ufbx_find_vec3(&p, "Lcl Translation", zero));
	raw->lclR = V(ufbx_find_vec3(&p, "Lcl Rotation", zero));
	raw->lclS = V(ufbx_find_vec3(&p, "Lcl Scaling", one));
	raw->preR = V(ufbx_find_vec3(&p, "PreRotation", zero));
	raw->postR = V(ufbx_find_vec3(&p, "PostRotation", zero));
	raw->rotOffset = V(ufbx_find_vec3(&p, "RotationOffset", zero));
	raw->rotPivot = V(ufbx_find_vec3(&p, "RotationPivot", zero));
	raw->sclOffset = V(ufbx_find_vec3(&p, "ScalingOffset", zero));
	raw->sclPivot = V(ufbx_find_vec3(&p, "ScalingPivot", zero));
	raw->geoT = V(ufbx_find_vec3(&p, "GeometricTranslation", zero));
	raw->geoR = V(ufbx_find_vec3(&p, "GeometricRotation", zero));
	raw->geoS = V(ufbx_find_vec3(&p, "GeometricScaling", one));
	raw->rotationOrder = (int)ufbx_find_int(&p, "RotationOrder", 0);
	raw->rotationActive = ufbx_find_int(&p, "RotationActive", 0) != 0;

	for (size_t i = 0; i < n->materials.count; i++)
		raw->materials.push_back(ConvertMaterial(n->materials.data[i]));

	if (n->light)
	{
		raw->light.reset(new Light());
		const ufbx_props& lp = n->light->props;
		raw->light->type = (int)ufbx_find_int(&lp, "LightType", 0);
		ufbx_vec3 c = ufbx_find_vec3(&lp, "Color", one);
		raw->light->color[0] = c.x; raw->light->color[1] = c.y; raw->light->color[2] = c.z;
		raw->light->innerAngle = ufbx_find_real(&lp, "InnerAngle", 0.0);
		raw->light->outerAngle = ufbx_find_real(&lp, "OuterAngle", 45.0);
		raw->light->enableNearAttenuation = ufbx_find_int(&lp, "EnableNearAttenuation", 0) != 0;
		raw->light->enableFarAttenuation = ufbx_find_int(&lp, "EnableFarAttenuation", 0) != 0;
		raw->light->nearAttenuationStart = ufbx_find_real(&lp, "NearAttenuationStart", 0.0);
		raw->light->farAttenuationEnd = ufbx_find_real(&lp, "FarAttenuationEnd", 0.0);
		raw->light->decayStart = ufbx_find_real(&lp, "DecayStart", 1.0);
		raw->light->decayType = (int)ufbx_find_int(&lp, "DecayType", 0);
	}
	if (n->camera)
	{
		raw->camera.reset(new Camera());
		const ufbx_props& cp = n->camera->props;
		raw->camera->fieldOfViewY = ufbx_find_real(&cp, "FieldOfViewY", 25.0);
		raw->camera->nearPlane = ufbx_find_real(&cp, "NearPlane", 10.0);
		raw->camera->farPlane = ufbx_find_real(&cp, "FarPlane", 4000.0);
	}

	for (size_t i = 0; i < n->children.count; i++)
		raw->children.push_back(Convert(n->children.data[i], raw));
	return raw;
}

void Scene::Triangulate()
{
	for (auto& n : m_nodes)
		for (auto& m : n->meshes)
			m->Triangulate();
}

void Scene::SplitMeshesPerMaterial()
{
	for (auto& n : m_nodes)
	{
		std::vector<std::unique_ptr<Mesh>> out;
		bool changed = false;
		for (auto& m : n->meshes)
		{
			std::vector<std::unique_ptr<Mesh>> parts = m->SplitPerMaterial();
			if (parts.empty())
			{
				out.push_back(std::move(m));
			}
			else
			{
				changed = true;
				for (auto& part : parts)
					out.push_back(std::move(part));
			}
		}
		(void)changed;
		n->meshes.swap(out);
	}
}

} // namespace fbx
