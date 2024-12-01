#version 430 core
#define COMPUTE
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

layout(R32F, binding = 0) uniform image3D o_Texture;

uniform float u_ClearValue = 0.0f;

void main() { 
	imageStore(o_Texture, ivec3(gl_GlobalInvocationID.xyz), vec4(u_ClearValue));
}