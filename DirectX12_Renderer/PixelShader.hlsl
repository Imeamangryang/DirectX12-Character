cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
};

struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float4 color : COLOR;
};

float4 PS(VS_OUTPUT input) : SV_TARGET
{
	return input.color;
}