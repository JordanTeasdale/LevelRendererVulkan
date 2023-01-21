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
// TODO: Part 4g
// TODO: Part 2i
// TODO: Part 3e
[[vk::push_constant]]
cbuffer MESH_INDEX
{
    uint mesh_ID;
    uint world_ID;
};
// TODO: Part 4a
struct OUTPUT_TO_RASTERIZER
{
    float4 posH : SV_POSITION; // homogenous projection space
    float3 nrmW : NORMAL; // normal in world space (for lighting)
    float3 posW : WORLD; // position in world space (for lighting)
    float2 uvC : UV; // uv cooridinate for textures
};
// TODO: Part 1f
struct VERTEX
{
    float3 pos : POSITION;
    float3 uvw : COLOR;
    float3 nrm : NORMAL;
};
struct VERTEX_OUT
{
    float4 pos : SV_POSITION;
    float2 uv : COLOR;
    float3 nrm : NORMAL;
};
// TODO: Part 4b
OUTPUT_TO_RASTERIZER main(VERTEX inputVertex, int ID : SV_InstanceID)
{
    OUTPUT_TO_RASTERIZER output;
    // TODO: Part 1h
	//inputVertex.pos[2] += 0.75;
    //inputVertex.pos[1] -= 0.75;
	// TODO: Part 2i
    output.posW = mul(inputVertex.pos, SceneData[0].matricies[world_ID + ID]);
    output.posH = mul(float4(output.posW, 1), SceneData[0].viewMatrix);
    output.posH = mul(output.posH, SceneData[0].projectionMatrix);
    output.nrmW = mul(inputVertex.nrm, SceneData[0].matricies[world_ID + ID]);
    output.uvC = inputVertex.uvw;
		// TODO: Part 4e
	// TODO: Part 4b
		// TODO: Part 4e
    return output;
}