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
	float4x4 gWorldViewProj;
};

VS_OUTPUT VS(VS_INPUT input) {
	VS_OUTPUT output;

	output.pos = mul(float4(input.pos, 1.0f), gWorldViewProj);
	output.color = input.color;

	return output;
}
