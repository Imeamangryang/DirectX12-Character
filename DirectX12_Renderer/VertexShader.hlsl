#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

#pragma enable_d3d11_debug_symbols

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float3 worldpos : POSITION;
    float3 normal : NORMAL;
    float2 tex : TEXCOORD;
};

struct VS_INPUT
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 tex : TEXCOORD;
};


VS_OUTPUT VS(VS_INPUT input) {
	VS_OUTPUT output = (VS_OUTPUT)0.0f;

    float4 worldpos = mul(float4(input.pos, 1.0f), gWorld);
    output.worldpos = worldpos.xyz;

    output.normal = mul(input.normal, (float3x3)gWorld);

    output.pos = mul(worldpos, gViewProj);
    
    float4 tex = mul(float4(input.tex, 0.0f, 1.0f), gTexTransform);
    output.tex = mul(tex, gMatTransform).xy;

	return output;
}
