/****************************************************************************
//	fbx-to-vmdf
//	The ufbx-facing layer. It loads an .fbx with ufbx (keeping the raw FBX
//	DOM) and presents the scene the way Ayataka saw it through the FBX SDK:
//	a node tree with SDK attribute types and transform evaluation, and
//	meshes with their layer elements exactly as stored in the file (mapping
//	and reference modes, direct and index arrays). It also implements the
//	SDK geometry operations Ayataka runs: Triangulate, SplitMeshesPerMaterial
//	and GenerateTangentsData. None of this uses Autodesk code; the SDK's
//	behaviour was matched by black-box comparison (tools/sdkprobe).
*****************************************************************************/
#ifndef FBX2VMDF_FBXSCENE_H
#define FBX2VMDF_FBXSCENE_H

#include "FbxMath.h"

#include <memory>
#include <string>
#include <vector>

struct ufbx_scene;
struct ufbx_node;
struct ufbx_mesh;
struct ufbx_material;

namespace fbx {

// FbxNodeAttribute::EType values Ayataka distinguishes
enum AttrType
{
	eUnknown = 0, eNull, eMarker, eSkeleton, eMesh, eNurbs, ePatch, eCamera, eCameraStereo, eCameraSwitcher, eLight
};

enum Mapping { eNone = 0, eByControlPoint, eByPolygonVertex, eByPolygon, eByEdge, eAllSame };
enum Reference { eDirect = 0, eIndex, eIndexToDirect };

// One FbxLayerElement: values as doubles, `comps` per entry.
struct LayerElement
{
	std::string name;
	Mapping mapping = eNone;
	Reference reference = eDirect;
	int comps = 0;
	std::vector<double> direct;
	std::vector<int> index;

	size_t DirectCount() const { return comps ? direct.size() / (size_t)comps : 0; }
	// FbxLayerElementArrayTemplate::GetAt: out-of-range reads give zeros
	void GetDirect(int i, double* out) const;
	// Resolve the value for a polygon corner like Ayataka's GetNormal() etc.
	bool Get(int controlPoint, int polygonVertex, double* out) const;
};

struct SkinCluster
{
	struct Node* link = nullptr;
	std::vector<int> indices;
	std::vector<double> weights;
	fbxm::AMatrix transformLink;
};

struct Skin
{
	bool rigid = false; // FbxSkin::eRigid
	std::vector<SkinCluster> clusters;
};

struct Mesh
{
	std::vector<fbxm::Vec4> controlPoints;
	std::vector<int> polygonStart; // polygonStart[p]..polygonStart[p+1]
	std::vector<int> polygonVertices;
	std::vector<LayerElement> normals, binormals, tangents, uvs, colors, materials;
	std::vector<Skin> skins;
	int deformerCount = 0; // skins + blend shapes + caches

	int GetPolygonCount() const { return (int)polygonStart.size() - (polygonStart.empty() ? 0 : 1); }
	int GetPolygonSize(int p) const { return polygonStart[p + 1] - polygonStart[p]; }
	int GetPolygonVertex(int p, int v) const { return polygonVertices[polygonStart[p] + v]; }
	int GetPolygonVertexIndex(int p) const { return polygonStart[p]; }
	int GetTextureUVIndex(int p, int v) const;

	void Triangulate();
	// Returns one mesh per used material, or an empty list when no split is needed.
	std::vector<std::unique_ptr<Mesh>> SplitPerMaterial() const;
	bool GenerateTangentsData(int uvSet, bool overwrite);
};

struct Material
{
	std::string name;
	bool isLambert = false;
	bool isPhong = false;
	// Lambert
	double ambient[3] = { 0.2, 0.2, 0.2 }, diffuse[3] = { 0.8, 0.8, 0.8 }, diffuseFactor = 1.0;
	double emissive[3] = { 0, 0, 0 }, emissiveFactor = 1.0;
	double transparentColor[3] = { 0, 0, 0 }, transparencyFactor = 0.0;
	// Phong
	double specular[3] = { 0.2, 0.2, 0.2 }, specularFactor = 1.0, shininess = 20.0;
	double reflection[3] = { 0, 0, 0 }, reflectionFactor = 1.0;

	struct Texture
	{
		std::string property;     // FBX material property it is connected to
		std::string relativeFileName;
		int wrapU = 0, wrapV = 0; // 0 repeat, 1 clamp
		double scale[2] = { 1.0, 1.0 }, translation[2] = { 0.0, 0.0 }, rotation = 0.0;
		bool layered = false;
	};
	std::vector<Texture> textures; // in SDK property iteration order
};

struct Light
{
	int type = 0; // FbxLight::EType: 0 point, 1 directional, 2 spot, 3 area, 4 volume
	double color[3] = { 1, 1, 1 };
	double innerAngle = 0.0, outerAngle = 45.0;
	bool enableNearAttenuation = false, enableFarAttenuation = false;
	double nearAttenuationStart = 0.0, farAttenuationEnd = 0.0, decayStart = 1.0;
	int decayType = 0; // 0 none, 1 linear, 2 quadratic, 3 cubic
};

struct Camera
{
	double fieldOfViewY = 25.0, nearPlane = 10.0, farPlane = 4000.0;
};

struct Node
{
	std::string name;
	Node* parent = nullptr;
	std::vector<Node*> children;
	AttrType attrType = eUnknown;            // default attribute
	std::vector<std::unique_ptr<Mesh>> meshes; // mesh attributes (several after a split)
	std::vector<Material*> materials;
	std::unique_ptr<Light> light;
	std::unique_ptr<Camera> camera;

	// static transform properties
	fbxm::Vec4 lclT, lclR, lclS = fbxm::Vec4(1, 1, 1);
	fbxm::Vec4 preR, postR, rotOffset, rotPivot, sclOffset, sclPivot;
	fbxm::Vec4 geoT, geoR, geoS = fbxm::Vec4(1, 1, 1);
	int rotationOrder = 0;
	bool rotationActive = false;

	fbxm::AMatrix EvaluateLocalTransform() const;
	fbxm::AMatrix EvaluateGlobalTransform() const;
	fbxm::Vec4 GetGeometricTranslation() const { return geoT; }
	fbxm::Vec4 GetGeometricRotation() const { return geoR; }
	fbxm::Vec4 GetGeometricScaling() const { return geoS; }
	int GetMaterialCount() const { return (int)materials.size(); }
	Material* GetMaterial(int i) const { return materials[i]; }
	int GetChildCount() const { return (int)children.size(); }
	Node* GetChild(int i) const { return children[i]; }
};

struct BindPose
{
	std::vector<std::pair<Node*, fbxm::AMatrix>> entries;
	int Find(const Node* n) const
	{
		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].first == n)
				return (int)i;
		return -1;
	}
};

struct AxisInfo
{
	int upAxis = 1, upSign = 1, frontAxis = 2, frontSign = 1, coordAxis = 0, coordSign = 1;
	double unitScaleFactor = 1.0; // centimetres per unit
	bool IsOpenGL() const { return upAxis == 1 && upSign == 1 && frontAxis == 2 && frontSign == 1 && coordAxis == 0 && coordSign == 1; }
};

class Scene
{
public:
	~Scene();
	bool Load(const std::string& path, std::string& error);

	Node* GetRootNode() { return m_root; }
	const std::vector<BindPose>& BindPoses() const { return m_bindPoses; }
	const AxisInfo& Axis() const { return m_axis; }
	bool HasAnimation() const { return m_hasAnimation; }
	std::string name;

	// FbxGeometryConverter equivalents, applied to every mesh attribute
	void Triangulate();
	void SplitMeshesPerMaterial();

private:
	Node* Convert(const ufbx_node* n, Node* parent);
	std::unique_ptr<Mesh> ConvertMesh(const ufbx_mesh* mesh);
	Material* ConvertMaterial(const ufbx_material* mat);

	ufbx_scene* m_scene = nullptr;
	Node* m_root = nullptr;
	std::vector<std::unique_ptr<Node>> m_nodes;
	std::vector<std::unique_ptr<Material>> m_materials;
	std::vector<std::pair<const ufbx_material*, Material*>> m_materialMap;
	std::vector<std::pair<const ufbx_node*, Node*>> m_nodeMap;
	std::vector<BindPose> m_bindPoses;
	AxisInfo m_axis;
	bool m_hasAnimation = false;
};

} // namespace fbx

#endif // FBX2VMDF_FBXSCENE_H
