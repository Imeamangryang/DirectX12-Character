struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
};

struct VS_INPUT
{
	float3 pos : POSITION;
	float4 color : COLOR;
};

cbuffer cbPerObject : register(b0)
{
	float4x4 gWorld; 
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
};

VS_OUTPUT VS(VS_INPUT input) {
	VS_OUTPUT output;

    float4 posw = mul(float4(input.pos, 1.0f), gWorld);
    output.pos = mul(posw, gViewProj);
	output.color = input.color;

	return output;
}
