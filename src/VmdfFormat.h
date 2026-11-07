/****************************************************************************
//	fbx-to-vmdf
//	Explicit-layout mirror of the Usagi engine's nanopb-generated model
//	structs (Engine/Scene/Model/{Model,Shape,Mesh,Skeleton}.pb.h,
//	Engine/Graphics/Materials/Material.pb.h, Engine/Graphics/Device/
//	StateEnums.pb.h, Engine/Resource/CustomEffectDecl.h), as the engine
//	reads them straight out of a .vmdf / .cfx file.
//
//	Every struct is made of 4-byte little-endian fields (uint32, int32,
//	float) and char arrays whose sizes are multiples of 4, so there is no
//	implicit padding on any supported ABI; the static_asserts below pin
//	the sizes and a few offsets against the engine's own layout (built
//	with PB_FIELD_32BIT, so pb_size_t is uint32). The file format is the
//	in-memory image of these structs on a little-endian host; fbx-to-vmdf
//	refuses to build for a big-endian target rather than byte swap.
*****************************************************************************/
#ifndef FBX2VMDF_VMDF_FORMAT_H
#define FBX2VMDF_VMDF_FORMAT_H

#include <cstddef>
#include <cstdint>

namespace vmdf {

// usg maths types as stored (Engine/Maths/*.h, Engine/Graphics/Color.h)
struct Vector2f { float x, y; };
struct Vector3f { float x, y, z; };
struct Vector4f { float x, y, z, w; };
struct Matrix4x4 { float M[4][4]; };
struct Color { float rgba[4]; };

// ---------------------------------------------------------------- enums
enum VertexAttribute : uint32_t
{
	VertexAttribute_NONE = 0,
	VertexAttribute_POSITION = 1,
	VertexAttribute_NORMAL = 2,
	VertexAttribute_UV = 3,
	VertexAttribute_TANGENT = 4,
	VertexAttribute_COLOR = 5,
	VertexAttribute_BONE_INDEX = 6,
	VertexAttribute_BONE_WEIGHT = 7,
	VertexAttribute_BINORMAL = 8
};

enum SkinningType : uint32_t
{
	SkinningType_NO_SKINNING = 0,
	SkinningType_SMOOTH_SKINNING = 1,
	SkinningType_RIGID_SKINNING = 2
};

// usg::VertexElementType (Engine/Graphics/RenderConsts.h)
enum VertexElementType : uint32_t
{
	VE_BYTE = 0, VE_UBYTE, VE_SHORT, VE_USHORT, VE_FLOAT, VE_INT, VE_UINT, VE_INVALID
};

// usg::ConstantType
enum ConstantType : uint32_t
{
	CT_MATRIX_44 = 0, CT_MATRIX_43, CT_VECTOR_4, CT_VECTOR_3, CT_VECTOR_2, CT_FLOAT, CT_INT,
	CT_VECTOR2I, CT_VECTOR4I, CT_VECTOR4U, CT_BOOL, CT_STRUCT, CT_COUNT
};

// CPU-side sizes (usg::g_uConstantSize)
static const uint32_t g_uConstantSize[CT_COUNT] = { 64, 48, 16, 12, 8, 4, 4, 8, 16, 16, 1, 0xFFFFFFFFu };

enum Bone_BillboardMode : uint32_t
{
	Bone_BillboardMode_OFF = 0, Bone_BillboardMode_WORLD, Bone_BillboardMode_WORLDVIEWPOINT, Bone_BillboardMode_SCREEN,
	Bone_BillboardMode_SCREENVIEWPOINT, Bone_BillboardMode_YAXIAL, Bone_BillboardMode_YAXIALVIEWPOINT
};

enum Translucency_Type : uint32_t
{
	Translucency_Type_TRANSLUCENCY_OPAQUE = 0, Translucency_Type_TRANSLUCENCY_TRANSLUCENT, Translucency_Type_TRANSLUCENCY_ADDITIVE, Translucency_Type_TRANSLUCENCY_SUBTRACTIVE
};

enum Rasterizer_Attribute : uint32_t
{
	Rasterizer_Attribute_ALPHA_TEST_ENABLE = 0, Rasterizer_Attribute_DEPTH_TEST_ENABLE, Rasterizer_Attribute_DEPTH_TEST_WRITE, Rasterizer_Attribute_DEPTH_TEST_MASK
};

enum Material_RenderPass : uint32_t
{
	Material_RenderPass_DEFAULT = 0, Material_RenderPass_DEFERRED, Material_RenderPass_TRANSPARENT, Material_RenderPass_DEPTH, Material_RenderPass_OMNI_DEPTH,
	Material_RenderPass_COUNT
};

enum TextureCoordinator_MappingMethod : uint32_t
{
	TextureCoordinator_MappingMethod_UV_COORDINATE = 0, TextureCoordinator_MappingMethod_PROJECTION, TextureCoordinator_MappingMethod_CUBE_ENV, TextureCoordinator_MappingMethod_SPHERE_ENV
};

// usg render state enums (Engine/Graphics/Device/StateEnums.pb.h, RenderConsts.h)
enum BlendFunc : uint32_t
{
	BLEND_FUNC_ZERO = 0, BLEND_FUNC_ONE, BLEND_FUNC_SRC_COLOR, BLEND_FUNC_ONE_MINUS_SRC_COLOR, BLEND_FUNC_DST_COLOR, BLEND_FUNC_ONE_MINUS_DST_COLOR,
	BLEND_FUNC_SRC_ALPHA, BLEND_FUNC_ONE_MINUS_SRC_ALPHA, BLEND_FUNC_DST_ALPHA, BLEND_FUNC_ONE_MINUS_DST_ALPHA, BLEND_FUNC_CONSTANT_COLOR,
	BLEND_FUNC_ONE_MINUS_CONSTANT_COLOR, BLEND_FUNC_CONSTANT_ALPHA, BLEND_FUNC_ONE_MINUS_CONSTANT_ALPHA, BLEND_FUNC_SRC_ALPHA_SATURATE
};
enum BlendEquation : uint32_t { BLEND_EQUATION_ADD = 0, BLEND_EQUATION_SUBTRACT, BLEND_EQUATION_REVERSE_SUBTRACT, BLEND_EQUATION_MIN, BLEND_EQUATION_MAX };
enum AlphaTest : uint32_t { ALPHA_TEST_NEVER = 0, ALPHA_TEST_ALWAYS, ALPHA_TEST_EQUAL, ALPHA_TEST_NOTEQUAL, ALPHA_TEST_LESS, ALPHA_TEST_LEQUAL, ALPHA_TEST_GREATER, ALPHA_TEST_GEQUAL };
enum StencilTest : uint32_t { STENCIL_TEST_NEVER = 0, STENCIL_TEST_ALWAYS, STENCIL_TEST_EQUAL, STENCIL_TEST_NOTEQUAL, STENCIL_TEST_LESS, STENCIL_TEST_LEQUAL, STENCIL_TEST_GREATER, STENCIL_TEST_GEQUAL };
enum StencilOp : uint32_t { STENCIL_OP_KEEP = 0, STENCIL_OP_ZERO, STENCIL_OP_REPLACE, STENCIL_OP_INCR, STENCIL_OP_DECR, STENCIL_OP_INVERT, STENCIL_OP_INCR_WRAP, STENCIL_OP_DECR_WRAP };
enum CullFace : uint32_t { CULL_FACE_NONE = 0, CULL_FACE_BACK, CULL_FACE_FRONT };
enum DepthTest : uint32_t { DEPTH_TEST_NEVER = 0, DEPTH_TEST_ALWAYS, DEPTH_TEST_EQUAL, DEPTH_TEST_NOTEQUAL, DEPTH_TEST_LESS, DEPTH_TEST_LEQUAL, DEPTH_TEST_GREATER, DEPTH_TEST_GEQUAL };
enum SamplerFilter : uint32_t { SAMP_FILTER_POINT = 0, SAMP_FILTER_LINEAR };
enum MipFilter : uint32_t { MIP_FILTER_POINT = 0, MIP_FILTER_LINEAR };
enum AnisoLevel : uint32_t { ANISO_LEVEL_1 = 0, ANISO_LEVEL_2, ANISO_LEVEL_4, ANISO_LEVEL_8, ANISO_LEVEL_16 };
enum SamplerWrap : uint32_t { SAMP_WRAP_REPEAT = 0, SAMP_WRAP_MIRROR, SAMP_WRAP_CLAMP };
enum TextureDimension : uint32_t { TD_TEXTURE1D = 0, TD_TEXTURE2D, TD_TEXTURE3D, TD_TEXTURECUBE, TD_TEXTURE2DARRAY };
enum LightKind : uint32_t { LightKind_DIRECTIONAL = 0, LightKind_POINT = 1, LightKind_SPOT = 2, LightKind_PROJECTION = 3 };

// usg::ShaderTypeFlags and constant buffer bindings
enum ShaderTypeFlags : uint32_t
{
	SHADER_FLAG_VERTEX = 0x1, SHADER_FLAG_GEOMETRY = 0x2, SHADER_FLAG_PIXEL = 0x4, SHADER_FLAG_TCONTROL = 0x8, SHADER_FLAG_TEVAL = 0x10
};

// usg::ShaderConstantType (constant buffer bindings)
enum ShaderConstantType : uint32_t
{
	SHADER_CONSTANT_GLOBAL = 0, SHADER_CONSTANT_MATERIAL, SHADER_CONSTANT_MATERIAL_1, SHADER_CONSTANT_CUSTOM_0, SHADER_CONSTANT_CUSTOM_1,
	SHADER_CONSTANT_CUSTOM_2, SHADER_CONSTANT_CUSTOM_3, SHADER_CONSTANT_LIGHTING, SHADER_CONSTANT_SHADOW
};

static const uint32_t RENDER_MASK_NO_MODEL_LIGHT = 0x00001000;

// ------------------------------------------------------------- structs
struct Sphere
{
	Vector3f center;
	float radius;
};

struct ModelHeader
{
	enum { rigidBoneIndices_max_count = 255, skinnedBoneIndices_max_count = 255 };
	uint32_t version;
	uint32_t alignment;
	char name[64];
	Sphere boundingSphere;
	uint32_t shapeNum;
	uint32_t materialNum;
	uint32_t meshNum;
	uint32_t shapeOffset;
	uint32_t materialOffset;
	uint32_t meshOffset;
	uint32_t skeletonOffset;
	uint32_t rigidBoneCount;
	uint32_t skinnedBoneCount;
	uint32_t rigidBoneIndices_count;
	uint32_t rigidBoneIndices[rigidBoneIndices_max_count];
	uint32_t skinnedBoneIndices_count;
	uint32_t skinnedBoneIndices[skinnedBoneIndices_max_count];
};

struct AdjacencyStream { uint32_t sizeAligned; };
struct IndexStream { uint32_t formatSize; uint32_t sizeAligned; uint32_t indexNum; };

struct SingleAttribute
{
	uint32_t attribute;
	Vector4f value;
	uint32_t columns;
	char usageHint[40];
};

struct VertexStreamInfo
{
	uint32_t refIndex;
	uint32_t columns;
	uint32_t attribute;
	uint32_t elementType;
	float scaling;
	char usageHint[40];
};

struct Primitive
{
	IndexStream indexStream;
	AdjacencyStream adjacencyStream;
	uint32_t rootBone;
	uint32_t lodLevel;
};

struct Shape
{
	enum { streamInfo_max_count = 9, singleAttributes_max_count = 4 };
	uint32_t streamOffset;
	Primitive primitive;
	uint32_t streamInfo_count;
	VertexStreamInfo streamInfo[streamInfo_max_count];
	uint32_t vertexStreamSizeAligned;
	uint32_t vertexNum;
	float vertexAlphaScale;
	Sphere boundingSphere;
	uint32_t singleAttributes_count;
	SingleAttribute singleAttributes[singleAttributes_max_count];
	uint32_t attributeMask;
	uint32_t skinningType;
};

struct Mesh
{
	char name[40];
	uint32_t shapeRefIndex;
	uint32_t shapeShadowRefIndex;
	char materialRefName[40];
	uint32_t materialRefIndex;
	uint32_t renderPriority;
	uint32_t isVisible;
};

struct Bone
{
	char name[40];
	char parentName[40];
	uint32_t parentIndex;
	uint32_t isNeededRendering;
	uint32_t billboardMode;
	Vector3f scale;
	Vector3f rotate;
	Vector3f translate;
	Matrix4x4 transform;
	Sphere boundingSphere;
	Matrix4x4 bindPoseTrans;
	Matrix4x4 invBindPoseTrans;
};

struct Skeleton
{
	uint32_t bonesNum;
	char rootBoneName[40];
	uint32_t rootBoneIndex;
	uint32_t isTransAnimEnabled;
};

struct ConstantData
{
	char bufferName[40];
	uint32_t uOffset;
	uint32_t uSize;
};

struct MaterialAttribute { uint32_t translucencyKind; };

struct StencilState
{
	uint32_t isEnable;
	uint32_t func;
	uint32_t ref;
	uint32_t readMask;
	uint32_t writeMask;
	uint32_t failOperation;
	uint32_t zFailOperation;
	uint32_t passOperation;
};

struct Texture
{
	char textureName[40];
	uint32_t wrapS;
	uint32_t wrapT;
	uint32_t magFilter;
	uint32_t minFilter;
	uint32_t mipFilter;
	float lodBias;
	uint32_t lodMinLevel;
	char textureHint[40];
	uint32_t anisoLevel;
};

struct TextureCoordinator
{
	uint32_t method;
	uint32_t sourceCoordinate;
	uint32_t refCamera;
	Vector2f scale;
	float rotate;
	Vector2f translate;
};

struct AlphaStateGroup
{
	uint32_t rgbSrcFunc;
	uint32_t rgbDestFunc;
	uint32_t rgbOp;
	uint32_t alphaSrcFunc;
	uint32_t alphaDestFunc;
	uint32_t alphaOp;
	Color constColor;
	uint32_t alphaTestFunc;
	float alphaTestReference;
};

struct Rasterizer
{
	uint32_t attribute;
	uint32_t depthTestFunc;
	uint32_t blendEnabled;
	AlphaStateGroup alphaState;
	uint32_t cullFace;
	uint32_t isPolygonOffsetEnable;
	int32_t polygonOffsetUnit;
	StencilState stencilTest;
	uint32_t colorMask;
};

struct RenderPassData
{
	enum { constants_max_count = 6 };
	char effectName[64];
	ConstantData constants[constants_max_count];
	uint32_t constants_count;
};

struct Material
{
	enum { textures_max_count = 7, textureCoordinators_max_count = 7, constantData_max_count = 768, renderPasses_max_count = 5, constantIndexes_max_count = 6 };
	char materialName[40];
	Texture textures[textures_max_count];
	Rasterizer rasterizer;
	uint32_t textureCoordinators_count;
	TextureCoordinator textureCoordinators[textureCoordinators_max_count];
	MaterialAttribute attribute;
	uint32_t constantData[constantData_max_count];
	RenderPassData renderPasses[renderPasses_max_count];
	int32_t constantIndexes[constantIndexes_max_count];
};

// Engine/Resource/CustomEffectDecl.h (the .cfx custom effect format)
namespace CustomEffectDecl {
struct Header
{
	uint32_t uAttributeOffset;
	uint32_t uAttributeCount;
	uint32_t uSamplerOffset;
	uint32_t uSamplerCount;
	uint32_t uConstantSetDeclOffset;
	uint32_t uConstantSetCount;
};
struct Sampler
{
	char hint[32];
	char texName[64];
	uint32_t eTexType;
	uint32_t uIndex;
	uint32_t uShaderSets;
	uint32_t eFilterMin;
	uint32_t eFilterMag;
	uint32_t eFilterMip;
	uint32_t eAnisoLevel;
	float LodBias;
	uint32_t LodMinLevel;
};
struct Attribute
{
	char name[32];
	char hint[32];
	uint8_t defaultData[64];
	uint32_t uIndex;
	uint32_t eConstantType;
	uint32_t uCount;
	uint32_t uVBIndex;
};
struct Constant
{
	char szName[32];
	uint32_t uNameHash;
	uint32_t eConstantType;
	uint32_t uiCount;
	uint32_t uiOffset;
};
struct ConstantSet
{
	char szName[32];
	uint32_t uConstants;
	uint32_t uDeclOffset;
	uint32_t uDataOffset;
	uint32_t uDataSize;
	uint32_t uShaderSets;
	uint32_t uBinding;
};
} // namespace CustomEffectDecl

// Sizes as compiled by the engine (x64 MSVC and clang/gcc agree: all 4-byte fields)
static_assert(sizeof(Sphere) == 16, "Sphere layout");
static_assert(sizeof(ModelHeader) == 2172, "ModelHeader layout");
static_assert(offsetof(ModelHeader, shapeNum) == 88, "ModelHeader layout");
static_assert(offsetof(ModelHeader, rigidBoneIndices_count) == 124, "ModelHeader layout");
static_assert(offsetof(ModelHeader, skinnedBoneIndices_count) == 1148, "ModelHeader layout");
static_assert(sizeof(VertexStreamInfo) == 60, "VertexStreamInfo layout");
static_assert(sizeof(SingleAttribute) == 64, "SingleAttribute layout");
static_assert(sizeof(Primitive) == 24, "Primitive layout");
static_assert(sizeof(Shape) == 868, "Shape layout");
static_assert(offsetof(Shape, vertexStreamSizeAligned) == 572, "Shape layout");
static_assert(sizeof(Mesh) == 100, "Mesh layout");
static_assert(sizeof(Bone) == 336, "Bone layout");
static_assert(sizeof(Skeleton) == 52, "Skeleton layout");
static_assert(sizeof(Texture) == 112, "Texture layout");
static_assert(sizeof(AlphaStateGroup) == 48, "AlphaStateGroup layout");
static_assert(sizeof(Rasterizer) == 108, "Rasterizer layout");
static_assert(sizeof(TextureCoordinator) == 32, "TextureCoordinator layout");
static_assert(sizeof(RenderPassData) == 356, "RenderPassData layout");
static_assert(sizeof(Material) == 6040, "Material layout");
static_assert(offsetof(Material, constantData) == 1164, "Material layout");
static_assert(sizeof(CustomEffectDecl::Header) == 24, "CustomEffectDecl::Header layout");
static_assert(sizeof(CustomEffectDecl::Sampler) == 132, "CustomEffectDecl::Sampler layout");
static_assert(sizeof(CustomEffectDecl::Attribute) == 144, "CustomEffectDecl::Attribute layout");
static_assert(sizeof(CustomEffectDecl::Constant) == 48, "CustomEffectDecl::Constant layout");
static_assert(sizeof(CustomEffectDecl::ConstantSet) == 56, "CustomEffectDecl::ConstantSet layout");

} // namespace vmdf

#endif // FBX2VMDF_VMDF_FORMAT_H
