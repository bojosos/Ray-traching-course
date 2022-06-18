#pragma once

#include <mutex>

#include "Image.hpp"
#include "Threading.hpp"
#include "Framebuffer.h"
#include "Primitive.h"
#include "FileSystem.h"

struct GLFWwindow;

struct Semaphore
{
	void notify()
	{
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Signalled = true;
		}
		m_Condvar.notify_one();
	}

	void wait()
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		m_Condvar.wait(lock, [&] {
			return m_Signalled;
			});
		m_Signalled = false;
	}

private:
	std::mutex m_Mutex;
	std::condition_variable m_Condvar;
	bool m_Signalled = false;
};

enum class SceneType
{
	Example,
	Dragon,
	InstancedCubes,
	InstancedDragons,
	CustomMesh
};

struct RenderProperties
{
	AcceleratorType accelerator = AcceleratorType::Octtree;
	SceneType sceneType = SceneType::Example;
	uint32_t samples = 4;
	Path scenePath;
};

class Window
{
public:
	// Initialized the render context
	Window(ThreadManager* tm);
	void init();
	void setContext(ImageData* imageData);
	void waitForExit();

	RenderProperties waitForTask();
	
private:
	void run();
	
	void initImGui();
	void onImGuiBegin();
	void onImGuiRender();
	void onImGuiEnd();
	void shutdownImGui();
	
private:
	ThreadManager* m_ThreadManager = nullptr;
	Framebuffer* m_Framebuffer = nullptr;
	std::thread m_RenderThread;
	std::mutex m_ImageLock;
	
	RenderProperties m_CurrentRenderProperties;

	Semaphore m_WaitForInit;
	Semaphore m_WaitForTask;

	ImageData* m_DisplayImage = nullptr;
	GLFWwindow* m_Window = nullptr;
	uint32_t m_FramebufferColor = 0;
	bool m_SizeChanged = false;

	float m_LastFrameTime = 0;
	float m_Timer = 0;

	uint32_t m_ViewportWidth = 1280;
	uint32_t m_ViewportHeight = 720;
};