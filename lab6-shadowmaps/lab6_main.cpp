

#include <GL/glew.h>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <chrono>

#include <labhelper.h>
#include <imgui.h>
#include <imgui_impl_sdl_gl3.h>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
using namespace glm;

#include <Model.h>
#include "hdr.h"
#include "fbo.h"

using std::min;
using std::max;

///////////////////////////////////////////////////////////////////////////////
// Various globals
///////////////////////////////////////////////////////////////////////////////
SDL_Window* g_window = nullptr;
static float currentTime = 0.0f;
static float deltaTime = 0.0f;
bool showUI = false;

// Mouse input
ivec2 g_prevMouseCoords = { -1, -1 };
bool g_isMouseDragging = false;
bool g_isMouseRightDragging = false;

///////////////////////////////////////////////////////////////////////////////
// Shader programs
///////////////////////////////////////////////////////////////////////////////
GLuint shaderProgram;       // Shader for rendering the final image
GLuint simpleShaderProgram; // Shader used to draw the shadow map
GLuint backgroundProgram;

///////////////////////////////////////////////////////////////////////////////
// Environment
///////////////////////////////////////////////////////////////////////////////
float environment_multiplier = 0.9f;
GLuint environmentMap, irradianceMap, reflectionMap;
const std::string envmap_base_name = "001";

///////////////////////////////////////////////////////////////////////////////
// Light source
///////////////////////////////////////////////////////////////////////////////
vec3 lightPosition;
float lightRotation = 0.f;
bool lightManualOnly = true; //Changed false
vec3 point_light_color = vec3(1.f, 1.f, 1.f);
bool useSpotLight = true; //Changed false
float innerSpotlightAngle = 17.5f;
float outerSpotlightAngle = 22.5f;
float point_light_intensity_multiplier = 10000.0f; 


///////////////////////////////////////////////////////////////////////////////
// Shadow map
///////////////////////////////////////////////////////////////////////////////
enum ClampMode
{
	Edge = 1,
	Border = 2
};

FboInfo shadowMapFB;
int shadowMapResolution = 128;
int shadowMapClampMode = ClampMode::Border; //Changed Edge
bool shadowMapClampBorderShadowed = false;
bool usePolygonOffset = true; //Changed false
bool useSoftFalloff = false;
bool useHardwarePCF = false;
float polygonOffset_factor = 1.87f; //Changed .25f
float polygonOffset_units = 1.0f;


///////////////////////////////////////////////////////////////////////////////
// Camera parameters.
///////////////////////////////////////////////////////////////////////////////
vec3 cameraPosition(-70.0f, 50.0f, 70.0f);
vec3 cameraDirection = normalize(vec3(0.0f) - cameraPosition);
float cameraSpeed = 10.f;

vec3 worldUp(0.0f, 1.0f, 0.0f);

///////////////////////////////////////////////////////////////////////////////
// Models
///////////////////////////////////////////////////////////////////////////////
labhelper::Model* fighterModel = nullptr;
labhelper::Model* landingpadModel = nullptr;
labhelper::Model* sphereModel = nullptr;

mat4 roomModelMatrix;
mat4 fighterModelMatrix;

void initGL()
{
	///////////////////////////////////////////////////////////////////////
	//		Load Shaders
	///////////////////////////////////////////////////////////////////////
	backgroundProgram = labhelper::loadShaderProgram("../lab6-shadowmaps/background.vert",
	                                                 "../lab6-shadowmaps/background.frag");
	shaderProgram = labhelper::loadShaderProgram("../lab6-shadowmaps/shading.vert",
	                                             "../lab6-shadowmaps/shading.frag");
	simpleShaderProgram = labhelper::loadShaderProgram("../lab6-shadowmaps/simple.vert",
	                                                   "../lab6-shadowmaps/simple.frag");

	///////////////////////////////////////////////////////////////////////
	// Load models and set up model matrices
	///////////////////////////////////////////////////////////////////////
	fighterModel = labhelper::loadModelFromOBJ("../scenes/NewShip.obj");
	landingpadModel = labhelper::loadModelFromOBJ("../scenes/landingpad.obj");
	sphereModel = labhelper::loadModelFromOBJ("../scenes/sphere.obj");

	roomModelMatrix = mat4(1.0f);
	fighterModelMatrix = translate(15.0f * worldUp);

	///////////////////////////////////////////////////////////////////////
	// Load environment map
	///////////////////////////////////////////////////////////////////////
	const int roughnesses = 8;
	std::vector<std::string> filenames;
	for(int i = 0; i < roughnesses; i++)
		filenames.push_back("../scenes/envmaps/" + envmap_base_name + "_dl_" + std::to_string(i) + ".hdr");

	reflectionMap = labhelper::loadHdrMipmapTexture(filenames);
	environmentMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + ".hdr");
	irradianceMap = labhelper::loadHdrTexture("../scenes/envmaps/" + envmap_base_name + "_irradiance.hdr");

	///////////////////////////////////////////////////////////////////////
	// Setup Framebuffer for shadow map rendering
	///////////////////////////////////////////////////////////////////////
	shadowMapFB.resize(shadowMapResolution, shadowMapResolution);

	glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);

	glEnable(GL_DEPTH_TEST); // enable Z-buffering
	glEnable(GL_CULL_FACE);  // enables backface culling
}

void debugDrawLight(const glm::mat4& viewMatrix,
                    const glm::mat4& projectionMatrix,
                    const glm::vec3& worldSpaceLightPos)
{
	mat4 modelMatrix = glm::translate(worldSpaceLightPos);
	glUseProgram(shaderProgram);
	labhelper::setUniformSlow(shaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * modelMatrix);
	labhelper::render(sphereModel);
}


void drawBackground(const mat4& viewMatrix, const mat4& projectionMatrix)
{
	glUseProgram(backgroundProgram);
	labhelper::setUniformSlow(backgroundProgram, "environment_multiplier", environment_multiplier);
	labhelper::setUniformSlow(backgroundProgram, "inv_PV", inverse(projectionMatrix * viewMatrix));
	labhelper::setUniformSlow(backgroundProgram, "camera_pos", cameraPosition);
	labhelper::drawFullScreenQuad();
}

void drawScene(GLuint currentShaderProgram,
               const mat4& viewMatrix,
               const mat4& projectionMatrix,
               const mat4& lightViewMatrix,
               const mat4& lightProjectionMatrix)
{
	glUseProgram(currentShaderProgram);
	// Light source
	vec4 viewSpaceLightPosition = viewMatrix * vec4(lightPosition, 1.0f);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_color", point_light_color);
	labhelper::setUniformSlow(currentShaderProgram, "point_light_intensity_multiplier",
	                          point_light_intensity_multiplier);
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightPosition", vec3(viewSpaceLightPosition));
	labhelper::setUniformSlow(currentShaderProgram, "viewSpaceLightDir",
	                          normalize(vec3(viewMatrix * vec4(-lightPosition, 0.0f))));
	labhelper::setUniformSlow(currentShaderProgram, "spotOuterAngle", std::cos(radians(outerSpotlightAngle)));
	labhelper::setUniformSlow(currentShaderProgram, "spotInnerAngle", std::cos(radians(innerSpotlightAngle)));


	mat4 lightMatrix = translate(vec3(0.5f)) * scale(vec3(0.5f)) * lightProjectionMatrix * lightViewMatrix * inverse(viewMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "lightMatrix", lightMatrix); 

	// Environment
	labhelper::setUniformSlow(currentShaderProgram, "environment_multiplier", environment_multiplier);

	// camera
	labhelper::setUniformSlow(currentShaderProgram, "viewInverse", inverse(viewMatrix));

	// landing pad
	mat4 modelMatrix(1.0f);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * modelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * modelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	                          inverse(transpose(viewMatrix * modelMatrix)));

	labhelper::render(landingpadModel);

	// Fighter
	labhelper::setUniformSlow(currentShaderProgram, "modelViewProjectionMatrix",
	                          projectionMatrix * viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "modelViewMatrix", viewMatrix * fighterModelMatrix);
	labhelper::setUniformSlow(currentShaderProgram, "normalMatrix",
	                          inverse(transpose(viewMatrix * fighterModelMatrix)));

	labhelper::render(fighterModel);
}


void display(void)
{
	int w, h;
	SDL_GetWindowSize(g_window, &w, &h);

	///////////////////////////////////////////////////////////////////////////
	// setup matrices
	///////////////////////////////////////////////////////////////////////////
	mat4 projMatrix = perspective(radians(45.0f), float(w) / float(h), 5.0f, 500.0f);
	mat4 viewMatrix = lookAt(cameraPosition, cameraPosition + cameraDirection, worldUp);

	vec4 lightStartPosition = vec4(40.0f, 40.0f, 0.0f, 1.0f);
	float light_rotation_speed = 1.f;
	if(!lightManualOnly && !g_isMouseRightDragging)
	{
		lightRotation += deltaTime * light_rotation_speed;
	}
	lightPosition = vec3(rotate(lightRotation, worldUp) * lightStartPosition);
	mat4 lightViewMatrix = lookAt(lightPosition, vec3(0.0f), worldUp);
	mat4 lightProjMatrix = perspective(radians(45.0f), 1.0f, 25.0f, 100.0f);

	///////////////////////////////////////////////////////////////////////////
	// Bind the environment map(s) to unused texture units
	///////////////////////////////////////////////////////////////////////////
	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, environmentMap);
	glActiveTexture(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, irradianceMap);
	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_2D, reflectionMap);
	glActiveTexture(GL_TEXTURE0);

	///////////////////////////////////////////////////////////////////////////
	// Set up shadow map parameters
	///////////////////////////////////////////////////////////////////////////
	// >>> @task 1
	if (shadowMapFB.width != shadowMapResolution || shadowMapFB.height != shadowMapResolution) {
		shadowMapFB.resize(shadowMapResolution, shadowMapResolution);
	}

	if (shadowMapClampMode == ClampMode::Edge) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	if (shadowMapClampMode == ClampMode::Border) {
		glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		vec4 border(shadowMapClampBorderShadowed ? 0.f : 1.f);
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, &border.x);
	}

	///////////////////////////////////////////////////////////////////////////
	// Draw Shadow Map
	///////////////////////////////////////////////////////////////////////////

	glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFB.framebufferId);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glViewport(0, 0, shadowMapFB.width, shadowMapFB.height);

	if (usePolygonOffset) {
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(polygonOffset_factor, polygonOffset_units);
	}

	drawScene(simpleShaderProgram, lightViewMatrix, lightProjMatrix, lightViewMatrix, lightProjMatrix);

	labhelper::Material& screen = landingpadModel->m_materials[8];
	screen.m_emission_texture.gl_id = shadowMapFB.colorTextureTarget;

	if (usePolygonOffset) {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	
	///////////////////////////////////////////////////////////////////////////
	// Draw from camera
	///////////////////////////////////////////////////////////////////////////
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, w, h);
	glClearColor(0.2f, 0.2f, 0.8f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, shadowMapFB.depthBuffer);

	drawBackground(viewMatrix, projMatrix);
	drawScene(shaderProgram, viewMatrix, projMatrix, lightViewMatrix, lightProjMatrix);
	debugDrawLight(viewMatrix, projMatrix, vec3(lightPosition));


	CHECK_GL_ERROR();
}

bool handleEvents(void)
{
	// check events (keyboard among other)
	SDL_Event event;
	bool quitEvent = false;

	// Allow ImGui to capture events.
	ImGuiIO& io = ImGui::GetIO();

	while(SDL_PollEvent(&event))
	{
		ImGui_ImplSdlGL3_ProcessEvent(&event);

		if(event.type == SDL_QUIT || (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_ESCAPE))
		{
			quitEvent = true;
		}
		if(event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_g)
		{
			showUI = !showUI;
		}
		else if(event.type == SDL_MOUSEBUTTONDOWN && (!showUI || !ImGui::GetIO().WantCaptureMouse)
		        && (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
		        && !(g_isMouseDragging || g_isMouseRightDragging))
		{
			if(event.button.button == SDL_BUTTON_LEFT)
			{
				g_isMouseDragging = true;
			}
			else if(event.button.button == SDL_BUTTON_RIGHT)
			{
				g_isMouseRightDragging = true;
			}
			int x;
			int y;
			SDL_GetMouseState(&x, &y);
			g_prevMouseCoords.x = x;
			g_prevMouseCoords.y = y;
		}

		if(!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)))
		{
			g_isMouseDragging = false;
		}
		if(!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT)))
		{
			g_isMouseRightDragging = false;
		}

		if(event.type == SDL_MOUSEMOTION)
		{
			// More info at https://wiki.libsdl.org/SDL_MouseMotionEvent
			int delta_x = event.motion.x - g_prevMouseCoords.x;
			int delta_y = event.motion.y - g_prevMouseCoords.y;
			if(g_isMouseDragging)
			{
				float rotation_speed = 0.005f;
				mat4 yaw = rotate(rotation_speed * -delta_x, worldUp);
				mat4 pitch = rotate(rotation_speed * -delta_y, normalize(cross(cameraDirection, worldUp)));
				cameraDirection = vec3(pitch * yaw * vec4(cameraDirection, 0.0f));
			}
			else if(g_isMouseRightDragging)
			{
				const float rotation_speed = 0.01f;
				lightRotation += delta_x * rotation_speed;
			}
			g_prevMouseCoords.x = event.motion.x;
			g_prevMouseCoords.y = event.motion.y;
		}
	}

	if(!io.WantCaptureKeyboard)
	{
		// check keyboard state (which keys are still pressed)
		const uint8_t* state = SDL_GetKeyboardState(nullptr);
		vec3 cameraRight = cross(cameraDirection, worldUp);
		if(state[SDL_SCANCODE_W])
		{
			cameraPosition += deltaTime * cameraSpeed * cameraDirection;
		}
		if(state[SDL_SCANCODE_S])
		{
			cameraPosition -= deltaTime * cameraSpeed * cameraDirection;
		}
		if(state[SDL_SCANCODE_A])
		{
			cameraPosition -= deltaTime * cameraSpeed * cameraRight;
		}
		if(state[SDL_SCANCODE_D])
		{
			cameraPosition += deltaTime * cameraSpeed * cameraRight;
		}
		if(state[SDL_SCANCODE_Q])
		{
			cameraPosition -= deltaTime * cameraSpeed * worldUp;
		}
		if(state[SDL_SCANCODE_E])
		{
			cameraPosition += deltaTime * cameraSpeed * worldUp;
		}
	}

	return quitEvent;
}

void gui()
{
	// Inform imgui of new frame
	ImGui_ImplSdlGL3_NewFrame(g_window);

	// ----------------- Set variables --------------------------
	ImGui::SliderInt("Shadow Map Resolution", &shadowMapResolution, 32, 2048);
	ImGui::Text("Polygon Offset");
	ImGui::Checkbox("Use polygon offset", &usePolygonOffset);
	ImGui::SliderFloat("Factor", &polygonOffset_factor, 0.0f, 10.0f);
	ImGui::SliderFloat("Units", &polygonOffset_units, 0.0f, 100.0f);
	ImGui::Text("Clamp Mode");
	ImGui::RadioButton("Clamp to edge", &shadowMapClampMode, ClampMode::Edge);
	ImGui::RadioButton("Clamp to border", &shadowMapClampMode, ClampMode::Border);
	ImGui::Checkbox("Border as shadow", &shadowMapClampBorderShadowed);
	ImGui::Checkbox("Use spot light", &useSpotLight);
	ImGui::Checkbox("Use soft falloff", &useSoftFalloff);
	ImGui::SliderFloat("Inner Deg.", &innerSpotlightAngle, 0.0f, 90.0f);
	ImGui::SliderFloat("Outer Deg.", &outerSpotlightAngle, 0.0f, 90.0f);
	ImGui::Checkbox("Use hardware PCF", &useHardwarePCF);
	ImGui::Checkbox("Manual light only (right-click drag to move)", &lightManualOnly);
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate,
	            ImGui::GetIO().Framerate);
	// ----------------------------------------------------------

	// Render the GUI.
	ImGui::Render();
}

int main(int argc, char* argv[])
{
	g_window = labhelper::init_window_SDL("OpenGL Lab 6");

	initGL();

	bool stopRendering = false;
	auto startTime = std::chrono::system_clock::now();

	while(!stopRendering)
	{
		//update currentTime
		std::chrono::duration<float> timeSinceStart = std::chrono::system_clock::now() - startTime;
		deltaTime = timeSinceStart.count() - currentTime;
		currentTime = timeSinceStart.count();

		// render to window
		display();

		// Render overlay GUI.
		if(showUI)
		{
			gui();
		}

		// Swap front and back buffer. This frame will now been displayed.
		SDL_GL_SwapWindow(g_window);

		// check events (keyboard among other)
		stopRendering = handleEvents();
	}
	// Free Models
	labhelper::freeModel(fighterModel);
	labhelper::freeModel(landingpadModel);
	labhelper::freeModel(sphereModel);

	// Shut down everything. This includes the window and all other subsystems.
	labhelper::shutDown(g_window);
	return 0;
}
