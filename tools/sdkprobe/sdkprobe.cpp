// sdkprobe: research harness, NOT part of fbx-to-vmdf.
//
// Loads an FBX through the Autodesk FBX SDK exactly the way Ayataka does
// (axis/unit conversion, Triangulate, SplitMeshesPerMaterial,
// GenerateTangentsData) and prints, per mesh and per triangle corner, the
// vertex elements Ayataka would gather (FbxLoad::AddMesh), as hex floats.
// fbx-to-vmdf's `--probe` mode prints the same format from its ufbx path, so
// the two can be diffed. Only needed to reverse-engineer SDK behaviour; it is
// built by hand against a local FBX SDK and never by the project's CMake.
//
//   c++ -std=c++17 -O1 sdkprobe.cpp -I$FBXSDK/include -L$FBXSDK/lib/release \
//       -lfbxsdk -lxml2 -lz -ldl -lpthread -o sdkprobe
#include <fbxsdk.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

static bool g_doubles = false;
static uint32_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static void pf(const char* tag, const float* v, int n)
{
	printf(" %s", tag);
	for (int i = 0; i < n; i++) printf(" %08x", fbits(v[i]));
}

static bool IsBone(FbxScene* scene, FbxNode* pNode)
{
	if (pNode->GetNodeAttribute() && pNode->GetNodeAttribute()->GetAttributeType())
	{
		FbxNodeAttribute::EType t = pNode->GetNodeAttribute()->GetAttributeType();
		if (t == FbxNodeAttribute::eSkeleton || t == FbxNodeAttribute::eNull || t == FbxNodeAttribute::eMarker || pNode->GetParent() == scene->GetRootNode())
		{
			if (t == FbxNodeAttribute::eMesh && ((FbxMesh*)pNode->GetNodeAttribute())->GetDeformerCount() > 0)
				return false;
			return true;
		}
	}
	return false;
}

static void pm(const char* tag, const FbxAMatrix& m)
{
	printf("%s", tag);
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double d = m.Get(i, j); uint64_t u; memcpy(&u, &d, 8); printf(" %016llx", (unsigned long long)u); }
	printf("\n");
}

static FbxAMatrix Combined(FbxNode* pNode, FbxNode* pParent)
{
	if (pParent)
	{
		FbxAMatrix mWorld = pNode->EvaluateGlobalTransform();
		FbxAMatrix mSkel = pParent->EvaluateGlobalTransform();
		return mSkel.Inverse() * mWorld;
	}
	FbxAMatrix g;
	g.SetT(pNode->GetGeometricTranslation(FbxNode::eSourcePivot));
	g.SetR(pNode->GetGeometricRotation(FbxNode::eSourcePivot));
	g.SetS(pNode->GetGeometricScaling(FbxNode::eSourcePivot));
	return pNode->EvaluateGlobalTransform() * g;
}

template <class E, class V>
static bool getVec(E* el, int cp, int corner, V& out)
{
	if (!el) return false;
	int idx = -1;
	if (el->GetMappingMode() == FbxGeometryElement::eByControlPoint) idx = cp;
	else if (el->GetMappingMode() == FbxGeometryElement::eByPolygonVertex) idx = corner;
	else return false;
	if (el->GetReferenceMode() == FbxGeometryElement::eIndexToDirect) idx = el->GetIndexArray().GetAt(idx);
	else if (el->GetReferenceMode() != FbxGeometryElement::eDirect) return false;
	out = el->GetDirectArray().GetAt(idx);
	return true;
}

static const char* mapName(int m) { static const char* n[] = { "none", "cp", "poly", "pv", "edge", "same" }; return (m >= 0 && m < 6) ? n[m] : "?"; }
static const char* refName(int r) { static const char* n[] = { "direct", "index", "i2d" }; return (r >= 0 && r < 3) ? n[r] : "?"; }

static void DumpMesh(FbxScene* scene, FbxNode* pNode, FbxMesh* mesh, bool genTangents, bool force)
{
	FbxNode* pParent = nullptr;
	if (mesh->GetDeformerCount(FbxDeformer::eSkin) == 0)
	{
		pParent = pNode;
		while (pParent && !IsBone(scene, pParent)) pParent = pParent->GetParent();
	}
	FbxAMatrix xf = Combined(pNode, pParent);
	FbxAMatrix nxf = xf.Inverse().Transpose();
	printf("MESH %s polys=%d cps=%d uvlayers=%d elUV=%d elN=%d elT=%d elB=%d elC=%d elMat=%d parent=%s\n", pNode->GetName(), mesh->GetPolygonCount(), mesh->GetControlPointsCount(),
		mesh->GetUVLayerCount(), mesh->GetElementUVCount(), mesh->GetElementNormalCount(), mesh->GetElementTangentCount(), mesh->GetElementBinormalCount(), mesh->GetElementVertexColorCount(),
		mesh->GetElementMaterialCount(), pParent ? pParent->GetName() : "-");
	pm("XF", xf);
	pm("NXF", nxf);
	if (genTangents && (mesh->GetElementTangentCount() < 1 || force))
	{
		bool ok = mesh->GenerateTangentsData(0, force);
		printf("GENTAN %d\n", ok ? 1 : 0);
	}
	for (int i = 0; i < mesh->GetElementNormalCount(); i++) printf("LAYER N%d %s %s\n", i, mapName(mesh->GetElementNormal(i)->GetMappingMode()), refName(mesh->GetElementNormal(i)->GetReferenceMode()));
	for (int i = 0; i < mesh->GetElementTangentCount(); i++) printf("LAYER T%d %s %s\n", i, mapName(mesh->GetElementTangent(i)->GetMappingMode()), refName(mesh->GetElementTangent(i)->GetReferenceMode()));
	for (int i = 0; i < mesh->GetElementBinormalCount(); i++) printf("LAYER B%d %s %s\n", i, mapName(mesh->GetElementBinormal(i)->GetMappingMode()), refName(mesh->GetElementBinormal(i)->GetReferenceMode()));
	for (int i = 0; i < mesh->GetElementUVCount(); i++) printf("LAYER UV%d %s %s %s\n", i, mesh->GetElementUV(i)->GetName(), mapName(mesh->GetElementUV(i)->GetMappingMode()), refName(mesh->GetElementUV(i)->GetReferenceMode()));
	for (int i = 0; i < mesh->GetElementVertexColorCount(); i++) printf("LAYER C%d %s %s\n", i, mapName(mesh->GetElementVertexColor(i)->GetMappingMode()), refName(mesh->GetElementVertexColor(i)->GetReferenceMode()));
	for (int i = 0; i < mesh->GetElementMaterialCount(); i++) printf("LAYER M%d %s %s\n", i, mapName(mesh->GetElementMaterial(i)->GetMappingMode()), refName(mesh->GetElementMaterial(i)->GetReferenceMode()));
	FbxStringList uvNames; mesh->GetUVSetNames(uvNames);
	int corner = 0;
	for (int t = 0; t < mesh->GetPolygonCount(); t++)
	{
		int ps = mesh->GetPolygonSize(t);
		for (int v = 0; v < ps; v++, corner++)
		{
			int cp = mesh->GetPolygonVertex(t, v);
			printf("V %d %d cp=%d", t, v, cp);
			FbxVector4 p = xf.MultT(mesh->GetControlPointAt(cp));
			float pos[3] = { (float)p[0], (float)p[1], (float)p[2] };
			pf("P", pos, 3);
			FbxVector4 n;
			if (getVec(mesh->GetElementNormal(0), cp, corner, n))
			{
				float f[3] = { (float)n[0], (float)n[1], (float)n[2] };
				FbxVector4 vec(f[0], f[1], f[2], 0.0);
				vec = nxf.MultT(vec); vec.Normalize();
				float o[3] = { (float)vec[0], (float)vec[1], (float)vec[2] };
				pf("Nraw", f, 3); pf("N", o, 3);
			}
			if (getVec(mesh->GetElementBinormal(0), cp, corner, n))
			{
				float f[3] = { (float)n[0], (float)n[1], (float)n[2] };
				FbxVector4 vec(f[0], f[1], f[2], 0.0);
				vec = nxf.MultT(vec); vec.Normalize();
				float o[3] = { (float)vec[0], (float)vec[1], (float)vec[2] };
				pf("Braw", f, 3); pf("B", o, 3);
			}
			if (getVec(mesh->GetElementTangent(0), cp, corner, n))
			{
				float f[3] = { (float)n[0], (float)n[1], (float)n[2] };
				FbxVector4 vec(f[0], f[1], f[2], 0.0);
				vec = nxf.MultT(vec); vec.Normalize();
				float o[3] = { (float)vec[0], (float)vec[1], (float)vec[2] };
				pf("Traw", f, 3); pf("T", o, 3);
			}
			FbxColor c;
			if (getVec(mesh->GetElementVertexColor(0), cp, corner, c))
			{
				float f[4] = { (float)c.mRed, (float)c.mGreen, (float)c.mBlue, (float)c.mAlpha };
				pf("C", f, 4);
			}
			for (int k = 0; k < mesh->GetElementUVCount(); k++)
			{
				FbxVector2 uv;
				if (getVec(mesh->GetElementUV(k), cp, corner, uv))
				{
					float f[2] = { (float)uv[0], (float)uv[1] };
					pf(k == 0 ? "UV0" : "UVn", f, 2);
				}
			}
			int uvi0 = mesh->GetTextureUVIndex(t, v);
			printf(" uvi0=%d", uvi0);
			if (g_doubles)
			{
				auto pd = [](const char* tag, const double* d, int n) { printf(" %s", tag); for (int i = 0; i < n; i++) printf(" %a", d[i]); };
				FbxVector4 cpd = mesh->GetControlPointAt(cp);
				pd("PD", cpd.mData, 3);
				FbxVector4 x;
				if (getVec(mesh->GetElementNormal(0), cp, corner, x)) pd("ND", x.mData, 4);
				if (getVec(mesh->GetElementTangent(0), cp, corner, x)) pd("TD", x.mData, 4);
				if (getVec(mesh->GetElementBinormal(0), cp, corner, x)) pd("BD", x.mData, 4);
				FbxVector2 uvd;
				if (getVec(mesh->GetElementUV(0), cp, corner, uvd)) pd("UVD", uvd.mData, 2);
				if (mesh->GetElementVertexColor(0))
				{
					FbxColor c0 = mesh->GetElementVertexColor(0)->GetDirectArray().GetAt(0);
					double cd[4] = { c0.mRed, c0.mGreen, c0.mBlue, c0.mAlpha };
					pd("C0D", cd, 4);
				}
			}
			printf("\n");
		}
	}
}

static void Recurse(FbxScene* scene, FbxNode* pNode, bool gen, bool force)
{
	if (pNode->GetNodeAttribute() && pNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
	{
		printf("NODE %s attrs=%d mats=%d\n", pNode->GetName(), pNode->GetNodeAttributeCount(), pNode->GetMaterialCount());
		for (int m = 0; m < pNode->GetMaterialCount(); m++) printf("MAT %d %s %s\n", m, pNode->GetMaterial(m)->GetName(), pNode->GetMaterial(m)->GetClassId().GetName());
		for (int a = 0; a < pNode->GetNodeAttributeCount(); a++)
		{
			if (pNode->GetNodeAttributeByIndex(a)->GetAttributeType() == FbxNodeAttribute::eMesh)
			{
				FbxMesh* m = (FbxMesh*)pNode->GetNodeAttributeByIndex(a);
				if (m->GetPolygonCount() > 0) { printf("ATTR %d\n", a); DumpMesh(scene, pNode, m, gen, force); }
			}
		}
	}
	for (int i = 0; i < pNode->GetChildCount(); i++) Recurse(scene, pNode->GetChild(i), gen, force);
}

static void DumpNodes(FbxNode* n, int depth)
{
	FbxNodeAttribute* a = n->GetNodeAttribute();
	printf("TREE %*s%s type=%d\n", depth * 2, "", n->GetName(), a ? (int)a->GetAttributeType() : -1);
	FbxAMatrix g = n->EvaluateGlobalTransform();
	pm("  GLOBAL", g);
	FbxAMatrix l = n->EvaluateLocalTransform();
	pm("  LOCAL", l);
	FbxVector4 r = g.GetR(), s = g.GetS(), t = g.GetT();
	printf("  GR %a %a %a GS %a %a %a GT %a %a %a\n", r[0], r[1], r[2], s[0], s[1], s[2], t[0], t[1], t[2]);
	for (int i = 0; i < n->GetChildCount(); i++) DumpNodes(n->GetChild(i), depth + 1);
}

int main(int argc, char** argv)
{
	if (argc < 2) { fprintf(stderr, "usage: sdkprobe file.fbx [--raw] [--force-tan] [--tree]\n"); return 1; }
	bool raw = false, force = false, tree = false, notan = false;
	for (int i = 2; i < argc; i++) { if (!strcmp(argv[i], "--raw")) raw = true; if (!strcmp(argv[i], "--force-tan")) force = true; if (!strcmp(argv[i], "--tree")) tree = true; if (!strcmp(argv[i], "--no-tan")) notan = true; if (!strcmp(argv[i], "--doubles")) g_doubles = true; }
	FbxManager* mgr = FbxManager::Create();
	FbxIOSettings* ios = FbxIOSettings::Create(mgr, IOSROOT);
	mgr->SetIOSettings(ios);
	FbxScene* scene = FbxScene::Create(mgr, "s");
	FbxImporter* imp = FbxImporter::Create(mgr, "");
	if (!imp->Initialize(argv[1])) { fprintf(stderr, "open failed\n"); return 1; }
	imp->Import(scene);
	FbxAxisSystem ax = scene->GetGlobalSettings().GetAxisSystem();
	int us; FbxAxisSystem::EUpVector up = ax.GetUpVector(us);
	int fs; FbxAxisSystem::EFrontVector fr = ax.GetFrontVector(fs);
	printf("AXIS up=%d sign=%d front=%d fsign=%d coord=%d opengl=%d unit_cm=%a\n", (int)up, us, (int)fr, fs, (int)ax.GetCoorSystem(), ax == FbxAxisSystem::OpenGL ? 1 : 0, scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor());
	if (!raw)
	{
		FbxGeometryConverter conv(mgr);
		scene->GetGlobalSettings().SetOriginalSystemUnit(FbxSystemUnit::m);
		FbxAxisSystem::OpenGL.ConvertScene(scene);
		FbxSystemUnit::m.ConvertScene(scene);
		conv.Triangulate(scene, true);
		conv.SplitMeshesPerMaterial(scene, true);
	}
	if (tree) DumpNodes(scene->GetRootNode(), 0);
	Recurse(scene, scene->GetRootNode(), !raw && !notan, force);
	return 0;
}
