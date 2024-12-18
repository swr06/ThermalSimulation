#include "Voxelizer.h"

#include "ModelRenderer.h"

#define VOXELRES 256

namespace Candela {

	static GLClasses::Shader* VoxelizeShader;
	static GLClasses::ComputeShader* ClearShader;

	const float RangeV = 32;

	static GLuint VoxelMap = 0;
	static GLuint TemperatureMap = 0;
	static GLuint TemperatureMap1 = 0;

	static float Align(float value, float size)
	{
		return std::floor(value / size) * size;
	}

	static glm::vec3 SnapPosition(glm::vec3 p) {
	   
		p.x = Align(p.x, 0.2f);
		p.y = Align(p.y, 0.2f);
		p.z = Align(p.z, 0.2f);

		return p;
	}

	double AlphaTransform(double alpha, int n, double m) {
		double ratio = m / n;
		return alpha / (ratio * ratio);
	}

	void Voxelizer::CreateVolumes()
	{
		glGenTextures(1, &VoxelMap);
		glBindTexture(GL_TEXTURE_3D, VoxelMap);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, VOXELRES, VOXELRES, VOXELRES, 0, GL_RED, GL_FLOAT, nullptr);

		glGenTextures(1, &TemperatureMap);
		glBindTexture(GL_TEXTURE_3D, TemperatureMap);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, VOXELRES, VOXELRES, VOXELRES, 0, GL_RED, GL_FLOAT, nullptr);

		glGenTextures(1, &TemperatureMap1);
		glBindTexture(GL_TEXTURE_3D, TemperatureMap1);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, VOXELRES, VOXELRES, VOXELRES, 0, GL_RED, GL_FLOAT, nullptr);

		ClearShader = new GLClasses::ComputeShader();
		ClearShader->CreateComputeShader("Core/Shaders/ClearVVolume.glsl");
		ClearShader->Compile();

		VoxelizeShader = new GLClasses::Shader();
		VoxelizeShader->CreateShaderProgramFromFile("Core/Shaders/VoxelizationVertex.glsl",
													"Core/Shaders/VoxelizationRadiance.glsl",
													"Core/Shaders/VoxelizationGeometry.geom");
		VoxelizeShader->CompileShaders();

	}

	void Voxelizer::Voxelize(glm::vec3 Position, const std::vector<Entity*>& EntityList)
	{

		Position = SnapPosition(Position);

		glBindTexture(GL_TEXTURE_3D, VoxelMap);
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glUseProgram(0);

		int GROUP_SIZE = 8;

		ClearShader->Use();
		ClearShader->SetFloat("u_ClearValue", 0.6f);
		glBindImageTexture(0, VoxelMap, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
		glDispatchCompute(VOXELRES / GROUP_SIZE, VOXELRES / GROUP_SIZE, VOXELRES / GROUP_SIZE);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		ClearShader->Use();
		ClearShader->SetFloat("u_ClearValue", 0.0f);
		glBindImageTexture(0, TemperatureMap, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
		glDispatchCompute(VOXELRES / GROUP_SIZE, VOXELRES / GROUP_SIZE, VOXELRES / GROUP_SIZE);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		ClearShader->Use();
		ClearShader->SetFloat("u_ClearValue", 0.0f);
		glBindImageTexture(0, TemperatureMap1, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
		glDispatchCompute(VOXELRES / GROUP_SIZE, VOXELRES / GROUP_SIZE, VOXELRES / GROUP_SIZE);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//// Voxelize ->
		
		VoxelizeShader->Use();
		glBindImageTexture(0, VoxelMap, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
		glBindImageTexture(1, TemperatureMap, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
		
		VoxelizeShader->SetVector3f("u_VoxelGridCenter", Position);
		VoxelizeShader->SetVector3f("u_VoxelGridCenterF", Position);
		VoxelizeShader->SetVector3f("u_CoverageSize", glm::vec3(RangeV));
		VoxelizeShader->SetFloat("u_CoverageSizeF", (RangeV));
		VoxelizeShader->SetInteger("u_VolumeSize", VOXELRES);
		
		glViewport(0, 0, VOXELRES, VOXELRES);
		
		for (auto& e : EntityList) {
		
			if (e->m_EmissiveAmount > 0.001f) {
				continue;
			}
			//VoxelizeShader->SetFloat("u_TAlpha", AlphaTransform(e->m_Alpha, VOXELRES, RangeV));
			VoxelizeShader->SetFloat("u_TAlpha", e->m_Alpha);
			RenderEntityV(*e, *VoxelizeShader);
		}
	}

	GLuint Voxelizer::GetVolume() {
		return VoxelMap;
	}

	GLuint Voxelizer::GetTempVolume(bool x)
	{
		return x ? TemperatureMap : TemperatureMap1;
	}
	

	int Voxelizer::GetVolSize()
	{
		return VOXELRES;
	}

	int Voxelizer::GetVolRange()
	{
		return int(RangeV);
	}

	void Voxelizer::RecompileShaders()
	{
		delete ClearShader;
		delete VoxelizeShader;


		ClearShader = new GLClasses::ComputeShader();
		ClearShader->CreateComputeShader("Core/Shaders/ClearVVolume.glsl");
		ClearShader->Compile();

		VoxelizeShader = new GLClasses::Shader();
		VoxelizeShader->CreateShaderProgramFromFile("Core/Shaders/VoxelizationVertex.glsl",
			"Core/Shaders/VoxelizationRadiance.glsl",
			"Core/Shaders/VoxelizationGeometry.geom");
		VoxelizeShader->CompileShaders();
	}


}