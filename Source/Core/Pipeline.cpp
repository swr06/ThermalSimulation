#define USE_STACKLESS_TRAVERSAL

#define STRINGIFY(x) (#x)

#include "Pipeline.h"

#include "FpsCamera.h"
#include "GLClasses/Shader.h"
#include "Object.h"
#include "Entity.h"
#include "ModelFileLoader.h" 
#include "ModelRenderer.h"
#include "GLClasses/Fps.h"
#include "GLClasses/Framebuffer.h"
#include "ShaderManager.h"
#include "Utils/Timer.h"

#include "Physics/PhysicsApi.h"

#include "SphereLight.h"

#include "Voxelizer.h"

#include "Tonemap.h"


#include <string>


#include "BVH/BVHConstructor.h"
#include "BVH/Intersector.h"

#include "TAAJitter.h"

#include "Utility.h"

#include "Player.h"

#include "../Dependencies/imguizmo/ImGuizmo.h"

#include <implot.h>

// Externs.
int __TotalMeshesRendered = 0;
int __MainViewMeshesRendered = 0;

#ifdef USE_STACKLESS_TRAVERSAL
	Candela::RayIntersector<Candela::BVH::StacklessTraversalNode> Intersector;
#else
	Candela::RayIntersector<Candela::BVH::StackTraversalNode> Intersector;
#endif 

Candela::Player Player;
Candela::FPSCamera& Camera = Player.Camera;


// 
double AverageTemperature;
///


static bool vsync = true;
static glm::vec3 _SunDirection = glm::vec3(0.1f, -1.0f, 0.1f);

// Misc 
static float InternalRenderResolution = 1.0f;
static float RoughnessMultiplier = 1.0f;
static bool DoNormalFix = false;
static bool DoFaceCulling = false;

float SpeedMultiplier = 7.0f;


// Timings
float CurrentTime = glfwGetTime();
float Frametime = 0.0f;
float DeltaTime = 0.0f;

// Edit mode 
static bool EditMode = false;
static bool ShouldDrawGrid = true;
static int EditOperation = 0;

static int SimulationFrames = 0;
bool DoSim = false;
bool SpawnCube = false;
bool RenderTempView = false;

static Candela::Entity* SelectedEntity = nullptr;
static ImGuizmo::MODE Mode = ImGuizmo::MODE::LOCAL;
static bool UseSnap = true;
static glm::vec3 SnapSize = glm::vec3(0.5f);

// Frame time plotter
static float* GraphX;
static float* GraphY;

// Render list 
std::vector<Candela::Entity*> EntityRenderList;

// GBuffers
GLClasses::Framebuffer GBuffer(16, 16, {{GL_RGBA16F, GL_RGBA, GL_FLOAT, false, false}, {GL_R16I, GL_RED_INTEGER, GL_SHORT, false, false} }, false, true);

GLClasses::ComputeShader SimulateShader;


// Draws editor grid 
void DrawGrid(const glm::mat4 CameraMatrix, const glm::mat4& ProjectionMatrix, const glm::vec3& GridBasis, float size) 
{
	glm::mat4 CurrentMatrix = glm::mat4(1.0f);
	ImGuizmo::DrawGrid(glm::value_ptr(CameraMatrix), glm::value_ptr(ProjectionMatrix), value_ptr(CurrentMatrix), size);
}

static double RoundToNearest(double n, double x) {
	return round(n / x) * x;
}

bool WindowResizedThisFrame = false;

class RayTracerApp : public Candela::Application
{
public:

	RayTracerApp()
	{
		m_Width = 800;
		m_Height = 600;
	}

	void OnUserCreate(double ts) override
	{
	
	}

	void OnUserUpdate(double ts) override
	{
		glfwSwapInterval((int)vsync);

		GLFWwindow* window = GetWindow();
		float camera_speed = DeltaTime * 23.0f * ((glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS ? 3.0f : 1.0f));

		Player.OnUpdate(window, DeltaTime, camera_speed * SpeedMultiplier, GetCurrentFrame());
	}

	void OnImguiRender(double ts) override
	{
		ImGuiIO& io = ImGui::GetIO();

		if (EditMode) {

			if (ImGui::Begin("Debug/Edit Mode")) {

				ImGui::NewLine();

				// Operations
				std::string OperationLabels[4] = { "Translation", "Rotation", "Scaling", "Universal (T/R/S)" };
				
				static bool space = 1;
				ImGui::Text("Editor Options");
				ImGui::Checkbox("Work in Local Space?", &space);
				ImGui::Checkbox("Draw Grid?", &ShouldDrawGrid);
				ImGui::Checkbox("Use Snap?", &UseSnap);
				if (UseSnap) {
					ImGui::SliderFloat3("Snap Size", &SnapSize[0], 0.01f, 4.0f);
				}
				ImGui::NewLine();
				ImGui::Text("Current Operation : %s", OperationLabels[EditOperation].c_str());
				ImGui::Text("Note : Use keys F6 - F9 to change operation (T/R/S/Universal)");

				Mode = space ? ImGuizmo::MODE::LOCAL : ImGuizmo::MODE::WORLD;

				ImGui::NewLine();
				ImGui::NewLine();

			} ImGui::End();

			// Draw editor

			static bool DeleteFlag = false;

			ImGuizmo::BeginFrame();

			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetRect(0, 0, GetWidth(), GetHeight());

			if (ShouldDrawGrid) {
				DrawGrid(Camera.GetViewMatrix(), Camera.GetProjectionMatrix(), glm::vec3(0.0f, 1.0f, 0.0f), 200.0f);
			}

			if (DeleteFlag) {
				DeleteFlag = false;
			}

			if (SelectedEntity && std::find(EntityRenderList.begin(), EntityRenderList.end(), SelectedEntity) != EntityRenderList.end()) {

				const ImGuizmo::OPERATION Ops[4] = { ImGuizmo::OPERATION::TRANSLATE, ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE, ImGuizmo::OPERATION::UNIVERSAL };

				glm::mat4 ModelMatrix = glm::mat4(1.0f);

				glm::vec3 Offset;

				{
					glm::vec3 tMin = SelectedEntity->m_Object->Min;
					glm::vec3 tMax = SelectedEntity->m_Object->Max;

					tMin = glm::vec3(ModelMatrix * glm::vec4(tMin, 1.0f));
					tMax = glm::vec3(ModelMatrix * glm::vec4(tMax, 1.0f));

					Offset = (tMin + tMax) * 0.5f;
				}

				ModelMatrix = glm::translate(SelectedEntity->m_Model, Offset);

				ImGuizmo::Manipulate(glm::value_ptr(Camera.GetViewMatrix()), glm::value_ptr(Camera.GetProjectionMatrix()),
					Ops[glm::clamp(EditOperation, 0, 3)], Mode, glm::value_ptr(ModelMatrix), nullptr, UseSnap ? glm::value_ptr(SnapSize) : nullptr);

				SelectedEntity->m_Model = glm::translate(ModelMatrix, -Offset);

				bool Hovered = ImGuizmo::IsOver();
				bool Using = ImGuizmo::IsUsing();

				if (Hovered || Using) {
					glm::vec3 P, S, R;
					ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(SelectedEntity->m_Model), glm::value_ptr(P), glm::value_ptr(R), glm::value_ptr(S));
				}

				// Entity window 
				ImGui::Begin("Entity Settings");
				ImGui::NewLine();

				std::string filename = SelectedEntity->m_Object->Path;
				size_t Idx = filename.find_last_of("\\/");
				if (std::string::npos != Idx)
				{
					filename.erase(0, Idx + 1);
				}


				ImGui::Text("Entity has parent object with model : %s", filename.c_str());
				ImGui::Text("Entity Position : %f %f %f", SelectedEntity->m_Model[3].x, SelectedEntity->m_Model[3].y, SelectedEntity->m_Model[3].z);
				ImGui::SliderFloat("Entity Alpha", &SelectedEntity->m_Alpha, 0.0f, 2.0f);
				ImGui::SliderFloat("Entity Temperature", &SelectedEntity->m_Temperature, 0.0f, 1000.0f);
				
				if (ImGui::Button("DELETE")) {
					EntityRenderList.erase(std::remove(EntityRenderList.begin(), EntityRenderList.end(), SelectedEntity), EntityRenderList.end());
					DeleteFlag = true;
				}

				ImGui::End();
			}
		}

		if (ImGui::Begin("Options")) {

			static double PrintAverageTemp = 0.0f;
			PrintAverageTemp = glm::mix(PrintAverageTemp, AverageTemperature, 0.02);

			ImGui::Text("Camera Position : %f,  %f,  %f", Camera.GetPosition().x, Camera.GetPosition().y, Camera.GetPosition().z);
			ImGui::Text("Camera Front : %f,  %f,  %f", Camera.GetFront().x, Camera.GetFront().y, Camera.GetFront().z);
			ImGui::Text("Time : %f s", glfwGetTime());
			ImGui::Text("Average Temperature : %lf", PrintAverageTemp);
			ImGui::NewLine();
			ImGui::Text("Number of Meshes Rendered (For the main camera view) : %d", __MainViewMeshesRendered);
			ImGui::Text("Total Number of Meshes Rendered : %d", __TotalMeshesRendered);
			ImGui::NewLine();
			ImGui::NewLine();
			ImGui::SliderFloat("Player Speed", &SpeedMultiplier, 0.01f, 24.0f);
			ImGui::NewLine();
			ImGui::SliderFloat3("Sun Direction", &_SunDirection[0], -1.0f, 1.0f);
			ImGui::NewLine();

			ImGui::Checkbox("DoSim", &DoSim);
			ImGui::NewLine();

			ImGui::Checkbox("Face Cull?", & DoFaceCulling);
			

		} ImGui::End();

		__TotalMeshesRendered = 0;
		__MainViewMeshesRendered = 0;
	}

	void OnEvent(Candela::Event e) override
	{
		ImGuiIO& io = ImGui::GetIO();
		
		if (e.type == Candela::EventTypes::MousePress && EditMode && !ImGui::GetIO().WantCaptureMouse)
		{
			if (!this->GetCursorLocked()) {

				if (!ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
					double mxx, myy;
					glfwGetCursorPos(this->m_Window, &mxx, &myy);
					mxx *= InternalRenderResolution;
					myy = (double)(this->GetHeight() - myy) * InternalRenderResolution;
				
					float d1;
				
					glBindFramebuffer(GL_FRAMEBUFFER, GBuffer.GetFramebuffer());
					glReadPixels((int)mxx, (int)myy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d1);
				
					short read_data = 0;
					glBindFramebuffer(GL_FRAMEBUFFER, GBuffer.GetFramebuffer());
					glReadBuffer(GL_COLOR_ATTACHMENT0 + 1);
					glReadPixels((int)mxx, (int)myy, 1, 1, GL_RED_INTEGER, GL_SHORT, &read_data);
				
					if (read_data >= 2) {
						read_data -= 2;
						SelectedEntity = EntityRenderList[read_data];
					}
				}
			}
		}

		if (e.type == Candela::EventTypes::MouseMove && GetCursorLocked())
		{
			Camera.UpdateOnMouseMovement(e.mx, e.my);
		}


		if (e.type == Candela::EventTypes::MouseScroll && !ImGui::GetIO().WantCaptureMouse)
		{
			float Sign = e.msy < 0.0f ? 1.0f : -1.0f;
			Camera.SetFov(Camera.GetFov() + 2.0f * Sign);
			Camera.SetFov(glm::clamp(Camera.GetFov(), 1.0f, 89.0f));
		}

		if (e.type == Candela::EventTypes::WindowResize)
		{
			Camera.SetAspect((float)glm::max(e.wx, 1) / (float)glm::max(e.wy, 1));
			WindowResizedThisFrame = true && this->GetCurrentFrame() > 4;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_ESCAPE) {
			exit(0);
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F1)
		{
			this->SetCursorLocked(!this->GetCursorLocked());
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F2 && this->GetCurrentFrame() > 5)
		{
			Candela::ShaderManager::RecompileShaders();
			Candela::Voxelizer::RecompileShaders();
			Intersector.Recompile();
			SimulateShader.Recompile();
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F3 && this->GetCurrentFrame() > 5)
		{
			Candela::ShaderManager::ForceRecompileShaders();
			Intersector.Recompile();
		}
		
		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F5 && this->GetCurrentFrame() > 5)
		{
			EditMode = !EditMode;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F6 && this->GetCurrentFrame() > 5) {
			EditOperation = 0;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F7 && this->GetCurrentFrame() > 5) {
			EditOperation = 1;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F8 && this->GetCurrentFrame() > 5) {
			EditOperation = 2;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F9 && this->GetCurrentFrame() > 5) {
			EditOperation = 3;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_V && this->GetCurrentFrame() > 5)
		{
			vsync = !vsync;
		}
		
		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_G && this->GetCurrentFrame() > 5)
		{
			DoSim = !DoSim;
		}


		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_R && this->GetCurrentFrame() > 5)
		{
			SimulationFrames = 0;
		}

		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_B && this->GetCurrentFrame() > 5)
		{
			SpawnCube = true;
		}
		
		if (e.type == Candela::EventTypes::KeyPress && e.key == GLFW_KEY_F && this->GetCurrentFrame() > 5)
		{
			RenderTempView = !RenderTempView;
		}

		if (e.type == Candela::EventTypes::MousePress && !ImGui::GetIO().WantCaptureMouse)
		{
		}
	}


};


void RenderEntityList(const std::vector<Candela::Entity*> EntityList, GLClasses::Shader& shader, bool glasspass) {

	int En = 0;
		
	for (auto& e : EntityList) {
		Candela::RenderEntity(*e, shader, Player.CameraFrustum, false, En, glasspass);
		En++;
	}
}


void UnbindEverything() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(0);
}



template <typename T>
void SetCommonUniforms(T& shader, CommonUniforms& uniforms) {
	shader.SetFloat("u_Time", glfwGetTime());
	shader.SetInteger("u_Frame", uniforms.Frame);
	shader.SetInteger("u_CurrentFrame", uniforms.Frame);
	shader.SetMatrix4("u_ViewProjection", Camera.GetViewProjection());
	shader.SetMatrix4("u_Projection", uniforms.Projection);
	shader.SetMatrix4("u_View", uniforms.View);
	shader.SetMatrix4("u_InverseProjection", uniforms.InvProjection);
	shader.SetMatrix4("u_InverseView", uniforms.InvView);
	shader.SetMatrix4("u_PrevProjection", uniforms.PrevProj);
	shader.SetMatrix4("u_PrevView", uniforms.PrevView);
	shader.SetMatrix4("u_PrevInverseProjection", uniforms.InvPrevProj);
	shader.SetMatrix4("u_PrevInverseView", uniforms.InvPrevView);
	shader.SetMatrix4("u_InversePrevProjection", uniforms.InvPrevProj);
	shader.SetMatrix4("u_InversePrevView", uniforms.InvPrevView);
	shader.SetVector3f("u_ViewerPosition", glm::vec3(uniforms.InvView[3]));
	shader.SetVector3f("u_Incident", glm::vec3(uniforms.InvView[3]));
	shader.SetVector3f("u_SunDirection", uniforms.SunDirection);
	shader.SetVector3f("u_LightDirection", uniforms.SunDirection);
	shader.SetFloat("u_zNear", Camera.GetNearPlane());
	shader.SetFloat("u_zFar", Camera.GetFarPlane());
}

void DecomposeMatrix(const glm::mat4& matrix, glm::vec3& position, glm::vec3& scale) {
	position = glm::vec3(matrix[3]);
	scale = glm::vec3(
		glm::length(glm::vec3(matrix[0])),
		glm::length(glm::vec3(matrix[1])),
		glm::length(glm::vec3(matrix[2]))
	);
}



void Candela::StartPipeline()
{
	// Matrices to rotate models whose axis basis is different 
	const glm::mat4 ZOrientMatrix = glm::mat4(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(1.0f));
	const glm::mat4 ZOrientMatrixNegative = glm::mat4(glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f), glm::vec4(0.0f, -1.0f, 0.0f, 0.0f), glm::vec4(1.0f));

	using namespace BVH;

	GraphX = new float[1024];
	GraphY = new float[1024];

	memset(GraphX, 0, 1024 * sizeof(float));
	memset(GraphY, 0, 1024 * sizeof(float));

	// Create App, initialize 
	RayTracerApp app;
	app.Initialize();
	app.SetCursorLocked(true);
	ImPlot::CreateContext();

	// Scene setup 
	Object MainModel;
	Object Cube;
	Object Sphere;
	
	FileLoader::LoadModelFile(&Cube, "Models/cube/scene.gltf");
	FileLoader::LoadModelFile(&MainModel, "Models/server/server.obj");
	FileLoader::LoadModelFile(&Sphere, "Models/ball/scene.gltf");

	// Add objects to intersector
	Intersector.Initialize();

	// Add the objects to the intersector (you could use a vector or similar to make this generic)
	Intersector.AddObject(MainModel);
	Intersector.AddObject(Cube);
	Intersector.AddObject(Sphere);
	Intersector.BufferData(true); // The flag is to tell the intersector to delete the cached cpu data 
	Intersector.GenerateMeshTextureReferences(); // This function is called to generate the texture references for the BVH

	// Create entities, each entity has a parent object 
	// Entities can have an arbitrary model matrix, transparency etc.

	// Create the main model 

	float GPU_TEMP = 4000.0f;


	float Heights[4] = { 8., 7., 6., 5. };

	{
		float z = -12.0f;
		while (true) {
			if (z > 12.) {
				break;
			}

			{
				Entity* m = new Entity(&MainModel);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(-13.5, 0.8, z));
				m->m_Model = glm::rotate(glm::mat4(m->m_Model), 3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.05f));
				m->m_Alpha = 1.0;
				EntityRenderList.push_back(m);
			}


			for (int i = 0 ; i < 4 ; i++)
			{
				Entity* m = new Entity(&Cube);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(-13.5, Heights[i], z));
				//m->m_Model = glm::rotate(glm::mat4(m->m_Model), 3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.5f, 0.25f, 1.2f));
				m->m_Alpha = 1.0;
				m->m_Temperature = GPU_TEMP;
				EntityRenderList.push_back(m);
			}


			z += 4.7f;
		}
	}

	{
		float z = -12.0f;
		while (true) {
			if (z > 12.) {
				break;
			}
			{
				Entity* m = new Entity(&MainModel);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(13.5, 0.8, z));
				m->m_Model = glm::rotate(glm::mat4(m->m_Model), -3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.05f));
				m->m_Alpha = 1.0;
				EntityRenderList.push_back(m);
			}

			for (int i = 0; i < 4; i++)
			{
				Entity* m = new Entity(&Cube);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(13.5, Heights[i], z));
				//m->m_Model = glm::rotate(glm::mat4(m->m_Model), 3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.5f, 0.25f, 1.2f));
				m->m_Alpha = 1.0;
				m->m_Temperature = GPU_TEMP;
				EntityRenderList.push_back(m);
			}


			z += 4.7f;
		}
	}

	{
		float z = -6.;
		while (true) {
			if (z > 6.) {
				break;
			}
			{
				Entity* m = new Entity(&MainModel);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(z, 0.8, -13.5));
				//m->m_Model = glm::rotate(glm::mat4(m->m_Model), -3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.05f));
				m->m_Alpha = 1.0;
				EntityRenderList.push_back(m);
			}

			for (int i = 0; i < 4; i++)
			{
				Entity* m = new Entity(&Cube);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(z, Heights[i], -13.5));
				m->m_Model = glm::rotate(glm::mat4(m->m_Model), 3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.5f, 0.25f, 1.2f));
				m->m_Alpha = 1.0;
				m->m_Temperature = GPU_TEMP;
				EntityRenderList.push_back(m);
			}

			z += 4.7f;
		}
	}

	{
		float z = -6.0f;
		while (true) {
			if (z > 6.) {
				break;
			}
			{
				Entity* m = new Entity(&MainModel);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(z, 0.8, 13.5));
				m->m_Model = glm::rotate(glm::mat4(m->m_Model), -3.14159f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.05f));
				m->m_Alpha = 1.0;
				EntityRenderList.push_back(m);
			}

			for (int i = 0; i < 4; i++)
			{
				Entity* m = new Entity(&Cube);
				m->m_Model = glm::translate(m->m_Model, glm::vec3(z, Heights[i], 13.5));
				m->m_Model = glm::rotate(glm::mat4(m->m_Model), 3.14159f / 2.0f, glm::vec3(0., 1., 0.));
				m->m_Model = glm::scale(glm::mat4(m->m_Model), glm::vec3(0.5f, 0.25f, 1.2f));
				m->m_Alpha = 1.0;
				m->m_Temperature = GPU_TEMP;
				EntityRenderList.push_back(m);
			}
			z += 4.7f;
		}
	}


	Entity Cube1(&Cube);
	Cube1.m_Model = glm::translate(Cube1.m_Model, glm::vec3(0., 7., 0.));
	Cube1.m_Model = glm::scale(Cube1.m_Model, glm::vec3(16., 6., 16));
	Cube1.m_Alpha = 0.5;

	// Sensor model
	GLuint SensorSSBO;
	glGenBuffers(1, &SensorSSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SensorSSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 32, 0, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	// Create VBO and VAO for drawing the screen-sized quad.
	GLClasses::VertexBuffer ScreenQuadVBO;
	GLClasses::VertexArray ScreenQuadVAO;

	// Setup screensized quad for rendering
	{
		unsigned long long CurrentFrame = 0;
		float QuadVertices_NDC[] =
		{
			-1.0f,  1.0f,  0.0f, 1.0f, -1.0f, -1.0f,  0.0f, 0.0f,
			 1.0f, -1.0f,  1.0f, 0.0f, -1.0f,  1.0f,  0.0f, 1.0f,
			 1.0f, -1.0f,  1.0f, 0.0f,  1.0f,  1.0f,  1.0f, 1.0f
		};

		ScreenQuadVAO.Bind();
		ScreenQuadVBO.Bind();
		ScreenQuadVBO.BufferData(sizeof(QuadVertices_NDC), QuadVertices_NDC, GL_STATIC_DRAW);
		ScreenQuadVBO.VertexAttribPointer(0, 2, GL_FLOAT, 0, 4 * sizeof(GLfloat), 0);
		ScreenQuadVBO.VertexAttribPointer(1, 2, GL_FLOAT, 0, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
		ScreenQuadVAO.Unbind();
	}

	// Create Shaders
	ShaderManager::CreateShaders();

	GLClasses::Shader& GBufferShader = ShaderManager::GetShader("GBUFFER");
	GLClasses::Shader& BasicBlitShader = ShaderManager::GetShader("BASIC_BLIT");

	SimulateShader.CreateComputeShader("Core/Shaders/Simulate.comp");
	SimulateShader.Compile();

	// Matrices
	glm::mat4 PreviousView;
	glm::mat4 PreviousProjection;
	glm::mat4 View;
	glm::mat4 Projection;
	glm::mat4 InverseView;
	glm::mat4 InverseProjection;

	// Voxelizer
	Voxelizer::CreateVolumes();
	Voxelizer::RecompileShaders();

	EntityRenderList.push_back(&Cube1);
	bool PrevSimBuff = 1;

	while (!glfwWindowShouldClose(app.GetWindow()))
	{
		// Window is minimized.
		if (glfwGetWindowAttrib(app.GetWindow(), GLFW_ICONIFIED)) {
			app.OnUpdate();
			app.FinishFrame();
			GLClasses::DisplayFrameRate(app.GetWindow(), "Candela ");
			continue;
		}

		if (SpawnCube) {
			SpawnCube = false;
			Entity *ptr = new Entity(&Cube);
			Entity& CubeP = *ptr;
			CubeP.m_Model = glm::translate(CubeP.m_Model, glm::vec3(Camera.GetPosition() + Camera.GetFront() * 1.5f));
			CubeP.m_Model = glm::scale(CubeP.m_Model, glm::vec3(0.5));
			CubeP.m_Temperature = 24.;
			CubeP.m_Alpha = 1.0;
			EntityRenderList.push_back(ptr);
		}


		glm::vec3 SunDirection = glm::normalize(_SunDirection);

		InternalRenderResolution = RoundToNearest(InternalRenderResolution, 0.25f);

		// Prepare Intersector
		Intersector.PushEntities(EntityRenderList);
		Intersector.BufferEntities();
		GBuffer.SetSize(app.GetWidth() * InternalRenderResolution, app.GetHeight() * InternalRenderResolution);
		
		PreviousProjection = Camera.GetProjectionMatrix();
		PreviousView = Camera.GetViewMatrix();

		app.OnUpdate(); 
		
		Projection = Camera.GetProjectionMatrix();
		View = Camera.GetViewMatrix();
		InverseProjection = glm::inverse(Camera.GetProjectionMatrix());
		InverseView = glm::inverse(Camera.GetViewMatrix());

		CommonUniforms UniformBuffer = { View, Projection, InverseView, InverseProjection, PreviousProjection, PreviousView, glm::inverse(PreviousProjection), glm::inverse(PreviousView), (int)app.GetCurrentFrame(), SunDirection};

		// VOXELIZE
		if (SimulationFrames <= 2 && DoSim)
		{
			Voxelizer::Voxelize(glm::vec3(0.0,5.,0.), EntityRenderList);
		}

		//if (app.GetCurrentFrame() % 128 == 0) {
		//	for (auto& e : EntityRenderList) {
		//		glm::vec3 p, s;
		//		DecomposeMatrix(e->m_Model, p, s);
		//		std::cout << "P : " << p.x << " " << p.y << " " << p.z << "\n";
		//		std::cout << "S : " << s.x << " " << s.y << " " << s.z << "\n";
		//		std::cout << "\n\n";
		//	}
		//}
		//

		// Render GBuffer

		if (DoFaceCulling) {
			glEnable(GL_CULL_FACE);
		}

		else {
			glDisable(GL_CULL_FACE);
		}

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthMask(GL_TRUE);

		glDisable(GL_BLEND);

		// Render GBuffer
		GBuffer.Bind();
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		GBufferShader.Use();
		GBufferShader.SetMatrix4("u_ViewProjection", Camera.GetViewProjection());
		GBufferShader.SetInteger("u_AlbedoMap", 0);
		GBufferShader.SetInteger("u_NormalMap", 1);
		GBufferShader.SetInteger("u_RoughnessMap", 2);
		GBufferShader.SetInteger("u_MetalnessMap", 3);
		GBufferShader.SetInteger("u_MetalnessRoughnessMap", 5);
		GBufferShader.SetBool("u_CatmullRom", false);
		GBufferShader.SetBool("u_NormalFix", DoNormalFix);
		GBufferShader.SetVector3f("u_ViewerPosition", Camera.GetPosition());
		GBufferShader.SetFloat("u_RoughnessMultiplier", RoughnessMultiplier);
		GBufferShader.SetFloat("u_ScaleLODBias", floor(log2(InternalRenderResolution)));
		GBufferShader.SetVector2f("u_Dimensions", glm::vec2(GBuffer.GetWidth(), GBuffer.GetHeight()));

		RenderEntityList(EntityRenderList, GBufferShader, false);
		UnbindEverything();

		// Post processing passes :

		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);


		// Simulation :flushed:

		AverageTemperature = 0;

		if (DoSim) {
			int ITERATIONS = 3;
			for (int i = 0; i < ITERATIONS; i++) {
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, SensorSSBO);
				glClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32F, 0, 32*sizeof(float), GL_RED, GL_FLOAT, nullptr);

				SimulateShader.Use();
				SimulateShader.SetFloat("u_Dt", DeltaTime);
				SimulateShader.SetFloat("u_DeltaTime", DeltaTime);
				SimulateShader.SetFloat("u_Ratio", float(Voxelizer::GetVolRange()) / float(Voxelizer::GetVolSize()));
				SimulateShader.SetFloat("u_Dim", Voxelizer::GetVolSize());
				SimulateShader.SetInteger("i_Prev", 0);
				SimulateShader.SetInteger("u_Frame", app.GetCurrentFrame());
				SimulateShader.SetInteger("u_SimulationFrames", SimulationFrames);

				// Current output
				glBindImageTexture(1, Voxelizer::GetTempVolume(!PrevSimBuff), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
				glBindImageTexture(2, Voxelizer::GetVolume(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
				glBindImageTexture(3, Voxelizer::GetSaturationVolume(), 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);

				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_3D, Voxelizer::GetTempVolume(PrevSimBuff));

				glBindBuffer(GL_SHADER_STORAGE_BUFFER, SensorSSBO);
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, SensorSSBO);

				glDispatchCompute(Voxelizer::GetVolSize() / 8, Voxelizer::GetVolSize() / 8, Voxelizer::GetVolSize() / 8);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

				PrevSimBuff = !PrevSimBuff;

				// Read back
				GLfloat data[32];
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, SensorSSBO);
				glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(data), data);
				AverageTemperature += data[0];
			}

			AverageTemperature /= float(ITERATIONS);
		}
		// Blit! 

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, app.GetWidth(), app.GetHeight());

		BasicBlitShader.Use();
		BasicBlitShader.SetInteger("u_Texture", 0);
		BasicBlitShader.SetInteger("u_Volume", 1);
		BasicBlitShader.SetInteger("u_Depth", 2);
		BasicBlitShader.SetInteger("u_Frame", app.GetCurrentFrame());
		BasicBlitShader.SetInteger("u_VoxelRange", Voxelizer::GetVolRange());
		BasicBlitShader.SetInteger("u_VoxelVolSize", Voxelizer::GetVolSize());

		BasicBlitShader.SetBool("u_Ye", RenderTempView);
		
		SetCommonUniforms<GLClasses::Shader>(BasicBlitShader, UniformBuffer);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, GBuffer.GetTexture());

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, Voxelizer::GetTempVolume(PrevSimBuff));

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, GBuffer.GetDepthBuffer());

		ScreenQuadVAO.Bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
		ScreenQuadVAO.Unbind();

		// Finish 

		glFinish();
		app.FinishFrame();

		if (DoSim) {
			SimulationFrames++;
		}


		CurrentTime = glfwGetTime();
		DeltaTime = CurrentTime - Frametime;
		Frametime = glfwGetTime();

		if (app.GetCurrentFrame() > 4 && WindowResizedThisFrame) {
			WindowResizedThisFrame = false;
		}

		std::string _s;
		_s = (EditMode ? "Candela | Edit Mode | " : "Candela | ");
		_s += DoSim ? ("  |  SIM ON") : "";
		GLClasses::DisplayFrameRate(app.GetWindow(), _s);
	}
}

// End.