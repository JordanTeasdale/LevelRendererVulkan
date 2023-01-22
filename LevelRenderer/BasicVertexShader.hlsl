// TODO: 2i
#pragma	pack_matrix(row_major)
// an ultra simple hlsl vertex shader
// TODO: Part 2b
typedef
struct _OBJ_ATTRIBUTES_
{
    float3 Kd; // diffuse reflectivity
    float d; // dissolve (transparency) 
    float3 Ks; // specular reflectivity
    float Ns; // specular exponent
    float3 Ka; // ambient reflectivity
    float sharpness; // local reflection map sharpness
    float3 Tf; // transmission filter
    float Ni; // optical density (index of refraction)
    float3 Ke; // emissive reflectivity
    uint illum; // illumination model
} OBJ_ATTRIBUTES;
#define MAX_SUBMESH_PER_DRAW 1054 // we can change this if desired
struct SHADER_MODEL_DATA
{
		//gloabally shared model data
    float3 sunDirection, sunColor; // lighting info
    float3 sunAmbient, cameraPos;
    matrix viewMatrix, projectionMatrix; // viewing info
		// per sub-mesh transform and material data
    matrix matricies[MAX_SUBMESH_PER_DRAW]; // world space transforms
    OBJ_ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW]; // color/texture of surface
};
StructuredBuffer<SHADER_MODEL_DATA> SceneData;

[[vk::push_constant]]
cbuffer MESH_INDEX
{
    uint mesh_ID;
    uint world_ID;
};

struct OUTPUT_TO_RASTERIZER
{
    float4 posH : SV_POSITION; // homogenous projection space
    float3 nrmW : NORMAL; // normal in world space (for lighting)
    float3 posW : WORLD; // position in world space (for lighting)
    float2 uvC : UV; // uv cooridinate for textures
};

struct VERTEX
{
    float3 pos : POSITION;
    float3 uvw : COLOR;
    float3 nrm : NORMAL;
};
OUTPUT_TO_RASTERIZER main(VERTEX inputVertex, int ID : SV_InstanceID)
{
    OUTPUT_TO_RASTERIZER output;
    output.posW = mul(float4(inputVertex.pos, 1), SceneData[0].matricies[world_ID + ID]).xyz;
    output.posH = mul(float4(output.posW, 1), SceneData[0].viewMatrix);
    output.posH = mul(output.posH, SceneData[0].projectionMatrix);
    output.nrmW = mul(inputVertex.nrm, SceneData[0].matricies[world_ID + ID]);
    output.uvC = inputVertex.uvw;
    
    return output;
}