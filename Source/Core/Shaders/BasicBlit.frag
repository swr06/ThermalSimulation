#version 330 core

layout (location = 0) out vec4 o_Color;

uniform sampler2D u_Input;
uniform sampler3D u_Volume;
uniform sampler2D u_Depth;

uniform int u_Frame;

uniform vec3 u_ViewerPosition;
uniform mat4 u_InverseView;
uniform mat4 u_InverseProjection;
uniform mat4 u_Projection;
uniform mat4 u_ViewProjection;
uniform mat4 u_View;
uniform vec2 u_Dims;

uniform int u_VoxelRange;
uniform int u_VoxelVolSize;

uniform bool u_Ye;
uniform bool u_Skibidi;

in vec2 v_TexCoords;

vec3 WorldPosFromDepth(float depth, vec2 txc)
{
    float z = depth * 2.0 - 1.0;
    vec4 ClipSpacePosition = vec4(txc * 2.0 - 1.0, z, 1.0);
    vec4 ViewSpacePosition = u_InverseProjection * ClipSpacePosition;
    ViewSpacePosition /= ViewSpacePosition.w;
    vec4 WorldPos = u_InverseView * ViewSpacePosition;
    return WorldPos.xyz;
}

// Voxelization
vec3 TransformToVoxelSpace(vec3 WorldPosition) {
	WorldPosition = WorldPosition - vec3(0.,5.,0.);
	float Size = float(u_VoxelRange);
	float HalfExtent = Size / 2.0f;
	vec3 ScaledPos = WorldPosition / HalfExtent;
	vec3 Voxel = ScaledPos;
	Voxel = Voxel * 0.5f + 0.5f;
	return (Voxel * float(u_VoxelVolSize));
}

bool InsideVolume(vec3 p) { float e = float(u_VoxelVolSize); return abs(p.x) < e && abs(p.y) < e && abs(p.z) < e ; } 

// Voxelization
bool DDA(vec3 origin, vec3 direction, int dist, out vec3 normal, out vec3 world_pos)
{
	const vec3 BLOCKNORMALS[6] = vec3[](vec3(1.0, 0.0, 0.0),vec3(-1.0, 0.0, 0.0),vec3(0.0, 1.0, 0.0),vec3(0.0, -1.0, 0.0),vec3(0.0, 0.0, 1.0),vec3(0.0, 0.0, -1.0));

	origin = TransformToVoxelSpace(origin);

	
	world_pos = origin;

	vec3 Temp;
	vec3 VoxelCoord; 
	vec3 FractPosition;

	Temp.x = direction.x > 0.0 ? 1.0 : 0.0;
	Temp.y = direction.y > 0.0 ? 1.0 : 0.0;
	Temp.z = direction.z > 0.0 ? 1.0 : 0.0;
	vec3 plane = floor(world_pos + Temp);

	for (int x = 0; x < dist; x++)
	{
		if (!InsideVolume(world_pos)) {
			break;
		}

		vec3 Next = (plane - world_pos) / direction;
		int side = 0;

		if (Next.x < min(Next.y, Next.z)) {
			world_pos += direction * Next.x;
			world_pos.x = plane.x;
			plane.x += sign(direction.x);
			side = 0;
		}

		else if (Next.y < Next.z) {
			world_pos += direction * Next.y;
			world_pos.y = plane.y;
			plane.y += sign(direction.y);
			side = 1;
		}

		else {
			world_pos += direction * Next.z;
			world_pos.z = plane.z;
			plane.z += sign(direction.z);
			side = 2;
		}

		VoxelCoord = (plane - Temp);
		int Side = ((side + 1) * 2) - 1;
		if (side == 0) {
			if (world_pos.x - VoxelCoord.x > 0.5){
				Side = 0;
			}
		}

		else if (side == 1){
			if (world_pos.y - VoxelCoord.y > 0.5){
				Side = 2;
			}
		}

		else {
			if (world_pos.z - VoxelCoord.z > 0.5){
				Side = 4;
			}
		}

		normal = BLOCKNORMALS[Side];
		int data = int(texelFetch(u_Volume, ivec3(VoxelCoord.xyz), 0).x>0.1);

		if (data != 0)
		{
			return true; 
		}
	}

	return false;
}

// Convert texcoord -> worldspace direcction
vec3 SampleIncidentRayDirection(vec2 screenspace)
{
	vec4 clip = vec4(screenspace * 2.0f - 1.0f, -1.0, 1.0);
	vec4 eye = vec4(vec2(u_InverseProjection * clip), -1.0, 0.0);
	return normalize(vec3(u_InverseView * eye));
}

vec3 blackbody(float t) {
    float p = pow(t, -1.5);
    float l = log(t);
    
	vec3 color;
    color.r = 220000.0 * p + 0.5804;
    color.g = 0.3923 * l - 2.4431;
    if (t > 6500.0) 
		color.g = 138039.0 * p + 0.738;
    color.b = 0.7615 * l - 5.681;
    
    color = clamp(color, 0.0, 1.0);
    
    if (t < 1000.0) 
		color *= t/1000.0;
        
    return color;
}

// T is between 0 and 1
vec3 Heatmap(float T) {
    float level = T*3.14159265/2.;
    vec3 col;
    col.r = sin(level);
    col.g = sin(level*2.);
    col.b = cos(level);
    return col;
}

void main() {

	float Depth = texture(u_Depth, v_TexCoords).x;
	vec3 WorldPos = WorldPosFromDepth(Depth, v_TexCoords).xyz;
	vec3 Voxel = TransformToVoxelSpace(WorldPos) / float(u_VoxelVolSize);
	Voxel = clamp(Voxel,0.,1.);

	vec3 RayOrigin = u_InverseView[3].xyz;
	vec3 RayDirection = normalize(SampleIncidentRayDirection(v_TexCoords));

	o_Color = vec4(0.);

	if (Depth < 0.99999999) {
		//o_Color.xyz = blackbody(texture(u_Volume, Voxel).x*10000.);
		float T = texture(u_Volume, Voxel).x * (5./4.);
		T = 1. - exp(-T);
		o_Color.xyz = Heatmap(T);
	}

	if (u_Ye) {
		o_Color.xyz = texture(u_Input, v_TexCoords).xyz;
	}
	bool debug = false;

	if (debug) {
		ivec3 Coord = ivec3(int(gl_FragCoord.x), int(gl_FragCoord.y), u_Frame % 256);
		vec3 w,n;
		bool IntersectedVoxelizedWorld = DDA(RayOrigin, RayDirection, int(10000), n, w);
		o_Color.xyz = vec3(0.5,0.5,0.5);
		if (IntersectedVoxelizedWorld) {
			o_Color.xyz = vec3(n);
		}
	}
	o_Color.w=1.;
}