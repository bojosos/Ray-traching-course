#include "Window.h"

#include <thread>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <imgui.h>

#include "RenderLog.h"
#include "Primitive.h"
#include "threading.hpp"
#include "Image.hpp"
#include "ImGui.h"

const char* vertexShaderSource = "#version 460 core\n"

"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aUv;\n"
"out vec2 uv;\n"
"void main()\n"
"{\n"
"uv = vec2(aUv.x, 1 - aUv.y);\n"
"   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
"}\0";
const char* fragmentShaderSource = "#version 460 core\n"
"out vec4 FragColor;\n"
"uniform sampler2D u_Texture;\n"
"in vec2 uv;\n"
"void main()\n"
"{\n"
"   FragColor = vec4(texture(u_Texture, uv).rgb, 1.0f);\n"
"}\n\0";

Window::Window(ThreadManager* manager) : m_ThreadManager(manager)
{
	
}

void Window::init()
{
	m_RenderThread = std::thread([this] { run(); });
	m_WaitForInit.wait();
}

void Window::setContext(ImageData* imageData)
{
	std::unique_lock<std::mutex> lock(m_ImageLock);
	m_DisplayImage = imageData;
	m_SizeChanged = true;
}

void OpenGLMessageCallback(unsigned source, unsigned type, unsigned id, unsigned severity, int length, const char* message, const void* userParam)
{
	switch (severity)
	{
	case GL_DEBUG_SEVERITY_HIGH:         printf(message); return;
	case GL_DEBUG_SEVERITY_MEDIUM:       printf(message); return;
	case GL_DEBUG_SEVERITY_LOW:          printf(message); return;
	case GL_DEBUG_SEVERITY_NOTIFICATION: printf(message); return;
	}
}

void Window::run()
{
	glfwInit();
	glfwWindowHint(GLFW_SAMPLES, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	m_Window = glfwCreateWindow(1280, 720, "V-Ray 7 LUL", NULL, NULL);
	glfwMakeContextCurrent(m_Window);
	if (m_Window == NULL)
	{
		printf("Failed to create GLFW window\n");
		glfwTerminate();
		return;
	}

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		printf("Failed to initialize OpenGL context\n");
		return;
	}
	
	glEnable(GL_MULTISAMPLE);
	glDepthFunc(GL_LESS);

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(OpenGLMessageCallback, nullptr);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);

	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);
	int success;
	char infoLog[512];
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
		printf("Vertex shader did not compile\n%s\n", infoLog);
	}
	// fragment shader
	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
		printf("Fragment shader did not compile\n%s\n", infoLog);
	}
	unsigned int shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
		printf("Couldn't link shaders\n%s\n", infoLog);
	}
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	glUseProgram(shaderProgram);

	float verts[] = { -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
					  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
					   1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
					   1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
					  -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
					  1.0f, -1.0f, 0.0f, 1.0f, 0.0f };

	GLuint vbo = 0;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

	GLuint vao = 0;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

	m_Framebuffer = new Framebuffer({ 1280, 720, 1, true });

	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glActiveTexture(GL_TEXTURE0); // activate the texture unit first before binding texture

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glUniform1i(glGetUniformLocation(shaderProgram, "u_Texture"), 0);

	glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
	initImGui();
	m_WaitForInit.notify();
	while (!glfwWindowShouldClose(m_Window))
	{
		glfwPollEvents();
		float time = (float)glfwGetTime();
		float timestep = time - m_LastFrameTime;
		m_LastFrameTime = time;
		m_Timer += timestep;

		m_Framebuffer->resize(m_ViewportWidth, m_ViewportHeight);
		m_Framebuffer->bind();
		glClear(GL_COLOR_BUFFER_BIT);
		glBindTexture(GL_TEXTURE_2D, texture);
		glActiveTexture(GL_TEXTURE0);

		if (m_Timer > 0.1f) // Do this every 100ms
		{
			std::unique_lock<std::mutex> lock(m_ImageLock);
			{
				if (m_DisplayImage != nullptr)
				{
					if (m_SizeChanged)
					{
						// glfwSetWindowSize(m_Window, m_DisplayImage->width, m_DisplayImage->height); // These "have" to be called from this thread
						// glViewport(0, 0, m_DisplayImage->width, m_DisplayImage->height); Framebuffer does this
						m_SizeChanged = false;
					}
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, m_DisplayImage->width, m_DisplayImage->height, 0, GL_RGB, GL_FLOAT, m_DisplayImage->pixels.data());
				}
			}
			m_Timer = 0;
		}
		glDrawArrays(GL_TRIANGLES, 0, 6);
		m_Framebuffer->unbind();
		onImGuiBegin();
		onImGuiRender();
		onImGuiEnd();
		glfwSwapBuffers(m_Window);
		// std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	shutdownImGui();
	delete m_Framebuffer;
	glfwTerminate();

	exit(0); // So that everything terminates if I close the window
}

void Window::onImGuiRender()
{
	static bool dockspaceOpen = true;
	static bool opt_fullscreen_persistant = true;
	bool opt_fullscreen = opt_fullscreen_persistant;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}

	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Crowny Editor", &dockspaceOpen, window_flags);
	ImGui::PopStyleVar();

	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("Crowny Editor");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
	ImGui::Begin("Viewport");
	ImGui::Image((ImTextureID)m_Framebuffer->GetColorAttachment(), ImVec2(m_ViewportWidth , m_ViewportHeight), ImVec2{0, 1}, ImVec2{1, 0});
	m_ViewportWidth = ImGui::GetWindowWidth();
	m_ViewportHeight = ImGui::GetWindowHeight();
	ImGui::End();

	ImGui::Begin("Render settings");
	bool running = m_ThreadManager->isRunning();
	if (running)
		ImGui::BeginDisabled(true);
	
	BeginPropertyGrid();
	const std::vector<const char*> optionsAcc = { "Octtree", "BVH", "KDTree" };
	PropertyDropdown("Accelerator", optionsAcc, m_CurrentRenderProperties.accelerator);

	static uint32_t selectedScene = 0;
	const std::vector<const char*> optionsSc = { "Example", "Dragon", "Instanced Cubes", "Instanced Dragons", "CustomMesh" };
	if (PropertyDropdown("Scene", optionsSc, m_CurrentRenderProperties.sceneType))
	{
		if (m_CurrentRenderProperties.sceneType == SceneType::Example)
			m_CurrentRenderProperties.samples = 4;
		else if (m_CurrentRenderProperties.sceneType == SceneType::Dragon)
			m_CurrentRenderProperties.samples = 4;
		else if (m_CurrentRenderProperties.sceneType == SceneType::InstancedCubes)
			m_CurrentRenderProperties.samples = 2;
		else if (m_CurrentRenderProperties.sceneType == SceneType::InstancedDragons)
			m_CurrentRenderProperties.samples = 10;
	}

	Property("Samples", m_CurrentRenderProperties.samples);

	std::string path = m_CurrentRenderProperties.scenePath.string();
	if (PropertyFilepath("Open Mesh", path))
	{
		m_CurrentRenderProperties.sceneType = SceneType::CustomMesh;
		m_CurrentRenderProperties.scenePath = path;
	}
	EndPropertyGrid();

	if (ImGui::Button("Render"))
	{
		m_WaitForTask.notify();
	}
	if (running)
		ImGui::EndDisabled();
	ImGui::End();

	ImGui::Begin("Render Log");
	RenderLog::Get().Render(running);
	ImGui::End();
	ImGui::End();
}

RenderProperties Window::waitForTask()
{	
	m_WaitForTask.wait();
	return m_CurrentRenderProperties;
}

void Window::initImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.MouseDoubleClickTime = 0.15f;
	io.MouseDoubleClickMaxDist = 6.0f;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	io.FontDefault = io.Fonts->AddFontFromFileTTF("Fonts/Roboto/Roboto-Regular.ttf", 17.0f, nullptr,
		io.Fonts->GetGlyphRangesCyrillic());

	style.WindowMenuButtonPosition = ImGuiDir_None;
	style.ColorButtonPosition = ImGuiDir_Left;

	style.FrameRounding = 2.5f;
	style.FrameBorderSize = 1.0f;
	style.IndentSpacing = 11.0f;

	auto& colors = ImGui::GetStyle().Colors;

	// Headers
	colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(IM_COL32(47, 47, 47, 255));
	colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(47, 47, 47, 255));
	colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(47, 47, 47, 255));

	// Buttons
	colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(IM_COL32(56, 56, 56, 200));
	colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(70, 70, 70, 255));
	colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(56, 56, 56, 150));

	// Frame BG
	colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(15, 15, 15, 255));
	colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(15, 15, 15, 255));
	colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(15, 15, 15, 255));

	// Tabs
	colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(IM_COL32(21, 21, 21, 255));
	colors[ImGuiCol_TabHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(255, 225, 135, 30));
	colors[ImGuiCol_TabActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(255, 225, 135, 60));
	colors[ImGuiCol_TabUnfocused] = ImGui::ColorConvertU32ToFloat4(IM_COL32(21, 21, 21, 255));
	colors[ImGuiCol_TabUnfocusedActive] = colors[ImGuiCol_TabHovered];

	// Title
	colors[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(21, 21, 21, 255));
	colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(21, 21, 21, 255));
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

	// Resize Grip
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.91f, 0.91f, 0.91f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.81f, 0.81f, 0.81f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.46f, 0.46f, 0.46f, 0.95f);

	// Scrollbar
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

	// Check Mark
	colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(IM_COL32(200, 200, 200, 255));

	// Slider
	colors[ImGuiCol_SliderGrab] = ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

	// Text
	colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(IM_COL32(192, 192, 192, 255));

	// Checkbox
	colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(IM_COL32(192, 192, 192, 255));

	// Separator
	colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(IM_COL32(26, 26, 26, 255));
	colors[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(39, 185, 242, 255));
	colors[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(39, 185, 242, 150));

	// Window Background
	colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(21, 21, 21, 255));
	colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(36, 36, 36, 255));
	colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(63, 70, 77, 255));
	colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(IM_COL32(26, 26, 26, 255));

	// Tables
	colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(47, 47, 47, 255));
	colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(IM_COL32(26, 26, 26, 255));

	// Menubar
	colors[ImGuiCol_MenuBarBg] = ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f };
	// colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, style.Colors[ImGuiCol_WindowBg].w);

	//========================================================
	/// Style
	style.FrameRounding = 2.5f;
	style.FrameBorderSize = 1.0f;
	style.IndentSpacing = 11.0f;

	ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
	ImGui_ImplOpenGL3_Init("#version 410");
}

void Window::shutdownImGui()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

void Window::onImGuiBegin()
{
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void Window::onImGuiEnd()
{
	ImGuiIO& io = ImGui::GetIO();
	int width, height;
	glfwGetWindowSize(m_Window, &width, &height);
	io.DisplaySize.x = (float)width;
	io.DisplaySize.x = (float)height;

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		GLFWwindow* backup_current_context = glfwGetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent(backup_current_context);
	}
}

void Window::waitForExit()
{
	m_RenderThread.join();
}