#version 430 core 

#define bool uint 
#define char uint 

layout(R32F, binding = 0) uniform image3D o_VoxelVolume;
layout(R32F, binding = 1) uniform image3D o_Temperature;
layout(R32F, binding = 2) uniform image3D o_SaturationMap;

in vec3 g_WorldPosition;
in vec3 g_VolumePosition;
//in vec3 g_Normal;

in vec2 g_UV;

uniform int u_VolumeSize;
uniform float u_CoverageSizeF;
uniform float u_Temperature;
uniform float u_TAlpha;

uniform vec3 u_VoxelGridCenterF;

float remap(float x, float a, float b, float c, float d)
{
    return (((x - a) / (b - a)) * (d - c)) + c;
}

void main() {

	float Size = u_CoverageSizeF;
	float HalfExtent = Size / 2.0f;

	vec3 Clipspace = g_WorldPosition / HalfExtent; 

	vec3 Voxel = Clipspace;

	Voxel = Voxel * 0.5f + 0.5f;

	if (Voxel == clamp(Voxel, 0.0f, 1.0f)) {
		ivec3 VoxelSpaceCoord = ivec3(Voxel * float(u_VolumeSize));
		imageStore(o_VoxelVolume, VoxelSpaceCoord, vec4(u_TAlpha));
		imageStore(o_Temperature, VoxelSpaceCoord, vec4(u_Temperature));
		imageStore(o_SaturationMap, VoxelSpaceCoord, vec4(u_Temperature));
	}
}