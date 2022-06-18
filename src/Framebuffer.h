#include <iostream>

struct FramebufferProperties
{
	uint32_t Width = 0, Height = 0;
	uint32_t Samples = 1;

	bool SwapChainTarget = false;
};

class Framebuffer
{
public:
	Framebuffer(const FramebufferProperties& props);
	~Framebuffer();

	void invalidate();
	void bind() const;
	void unbind() const;
	void resize(uint32_t width, uint32_t height);
	uint32_t GetColorAttachment() const { return m_ColorAttachment; }

	const FramebufferProperties& GetProperties() const { return m_Properties; }

private:
	uint32_t m_RendererID = 0;
	uint32_t m_ColorAttachment = 0, m_DepthAttachment = 0;
	FramebufferProperties m_Properties;
};