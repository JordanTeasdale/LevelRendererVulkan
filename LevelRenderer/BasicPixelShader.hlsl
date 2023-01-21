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
};
struct OUTPUT_TO_RASTERIZER
{
    float4 posH : SV_POSITION; // homogenous projection space
    float3 nrmW : NORMAL; // normal in world space (for lighting)
    float3 posW : WORLD; // position in world space (for lighting)
    float2 uvC : UV; // uv cooridinate for textures
};
// an ultra simple hlsl pixel shader
// TODO: Part 4b
float4 main(OUTPUT_TO_RASTERIZER inputVertex) : SV_TARGET
{
	//return float4(0.75f ,0, 0, 0); // TODO: Part 1a
	// TODO: Part 3a
    float4 matColor = float4(SceneData[0].materials[mesh_ID].Kd, 1);
	// TODO: Part 4c
    // Diffuse and Ambient lights
    float3 normalizedNRM = normalize(inputVertex.nrmW);
    float lightRatio = saturate(dot(normalize(-SceneData[0].sunDirection), normalizedNRM));
    float3 directColor = (lightRatio * SceneData[0].sunColor);
    float3 indirectColor = SceneData[0].sunAmbient * SceneData[0].materials[mesh_ID].Ka;
    // Specular Light
    float3 viewDir = normalize(SceneData[0].cameraPos - inputVertex.posW);
    float3 halfVec = normalize(normalize(-SceneData[0].sunDirection) + viewDir);
    float intensity = max(pow(saturate(dot(inputVertex.nrmW, halfVec)), SceneData[0].materials[mesh_ID].Ns), 0);
    float4 reflectedLight = intensity * float4(SceneData[0].materials[mesh_ID].Ks, 1);
    
    //float4 finalColor = diffuseColor + reflectedLight;
    //return float4(saturate(directColor + indirectColor), 1);
    return (float4(saturate(directColor + indirectColor), 1) * matColor) + reflectedLight;
	// TODO: Part 4g (half-vector or reflect method your choice)
    //return finalColor;

}