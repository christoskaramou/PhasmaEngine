#ifndef CONTEXT_H
#define CONTEXT_H


#include <map>
#include <any>
#define VK_USE_PLATFORM_WIN32_KHR
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <../SDL/SDL.h>
#include <../SDL/SDL_vulkan.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_RADIANS
#include <../glm/glm.hpp>
#include <../glm/gtc/matrix_transform.hpp>
#include <../glm/gtx/hash.hpp>
#include <../glm/gtx/rotate_vector.hpp>
#include <../glm/gtc/constants.hpp>
#include <../glm/gtx/matrix_decompose.hpp>

struct Context; // foward declaration

struct Vertex
{
    float x, y, z;			// Vertex Position
    float nX, nY, nZ;		// Normals x, y, z
    float u, v;				// Texture u, v
	float tX, tY, tZ, tW;	// Tangents

    bool operator==(const Vertex& other) const;
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGeneral();
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionGUI();
	static std::vector<vk::VertexInputBindingDescription> getBindingDescriptionSkyBox();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGeneral();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionGUI();
	static std::vector<vk::VertexInputAttributeDescription> getAttributeDescriptionSkyBox();
};

struct Image
{
    vk::Image image;
    vk::DeviceMemory memory;
    vk::ImageView view;
	vk::Sampler sampler;

	// values
    vk::Format format;
	vk::ImageLayout initialLayout = vk::ImageLayout::ePreinitialized;
	uint32_t mipLevels = 1;
	uint32_t arrayLayers = 1;
	vk::ImageCreateFlags imageCreateFlags = vk::ImageCreateFlags();
	vk::ImageViewType viewType = vk::ImageViewType::e2D;
	vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat;
	float maxAnisotropy = 16.f;
	vk::BorderColor borderColor = vk::BorderColor::eFloatOpaqueBlack;
	vk::Bool32 samplerCompareEnable = VK_FALSE;

    void createImage(const Context* info, const uint32_t width, const uint32_t height, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
    void createImageView(const Context* info, const vk::ImageAspectFlags aspectFlags);
    void transitionImageLayout(const Context* info, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout);
    void copyBufferToImage(const Context* info, const vk::Buffer buffer, const int x, const int y, const int width, const int height, const uint32_t baseLayer = 0);
	void generateMipMaps(const Context* info, const int32_t texWidth, const int32_t texHeight);
	void createSampler(const Context* info);
    void destroy(const Context* info);
};

struct Surface
{
	vk::SurfaceKHR surface;
	vk::SurfaceCapabilitiesKHR capabilities;
	vk::SurfaceFormatKHR formatKHR;
	vk::PresentModeKHR presentModeKHR;
};

struct Swapchain
{
	vk::SwapchainKHR swapchain;
	std::vector<Image> images{};

	void destroy(const vk::Device& device);
};

struct Buffer
{
    vk::Buffer buffer;
    vk::DeviceMemory memory;
    vk::DeviceSize size = 0;
    void *data;

    void createBuffer(const Context* info, const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties);
    void copyBuffer(const Context* info, const vk::Buffer srcBuffer, const vk::DeviceSize size);
    void destroy(const Context* info);
};

struct specificGraphicsPipelineCreateInfo
{
	std::vector<std::string> shaders{};
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
	std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptions{};
	std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions{};
	vk::CullModeFlagBits cull;
	vk::FrontFace face;
	vk::PushConstantRange pushConstantRange;
	bool useBlendState = true;
	vk::RenderPass renderPass;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
	vk::Extent2D viewportSize;
};

struct Pipeline
{
	vk::Pipeline pipeline;
	vk::GraphicsPipelineCreateInfo info;

	void destroy(const Context* info);
};

struct Effects {
	glm::vec4 specular;
	glm::vec4 ambient;
	glm::vec4 diffuse;
};

struct Object
{
	bool enabled = true;
	vk::DescriptorSet descriptorSet;
	Image texture;
	std::vector<float> vertices{};
	Buffer vertexBuffer;
	Buffer uniformBuffer;

	virtual void createVertexBuffer(const Context* info);
	virtual void createUniformBuffer(const Context* info);
	virtual void loadTexture(const std::string path, const Context* info);
	virtual void createDescriptorSet(vk::DescriptorSetLayout& descriptorSetLayout, const Context* info);
	virtual void destroy(Context* info);
};

struct GUI : Object
{
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(const Context* info = nullptr); // after the first call it is no need to point the Context for the divice
	static specificGraphicsPipelineCreateInfo getPipelineSpecifications(const Context* info);
	static GUI loadGUI(const std::string textureName, const Context* info, bool show);
	void createDescriptorSet(vk::DescriptorSetLayout& descriptorSetLayout, const Context* info);
};

struct SkyBox : Object
{
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(const Context* info = nullptr);
	static specificGraphicsPipelineCreateInfo getPipelineSpecifications(const Context* info);
	static SkyBox loadSkyBox(const std::vector<std::string>& textureNames, uint32_t imageSideSize, const Context* info, bool show);
	void loadTextures(const std::vector<std::string>& paths, uint32_t imageSideSize, const Context* info);
};

struct Terrain : Object
{
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(const Context* info = nullptr);
	static specificGraphicsPipelineCreateInfo getPipelineSpecifications(const Context* info);
	static Terrain generateTerrain(const std::string path, const Context* info);
	void loadTexture(const std::string path, const Context* info);
};

struct Shadows
{
	static bool shadowCast;
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(const Context* info = nullptr);
	static specificGraphicsPipelineCreateInfo getPipelineSpecifications(const Context* info);
	static vk::RenderPass renderPass;
	static vk::RenderPass getRenderPass(const Context* info);
	static uint32_t imageSize;
	Image texture;
	vk::DescriptorSet descriptorSet;
	vk::Framebuffer frameBuffer;
	Buffer uniformBuffer;

	void createFrameBuffer(const Context* info);
	void createUniformBuffer(const Context* info);
	void createDescriptorSet(vk::DescriptorSetLayout& descriptorSetLayout, const Context* info);
	void destroy(const Context* info);
};

struct Mesh
{
	enum TextureType
	{
		DiffuseMap,
		NormalMap,
		SpecularMap,
		AlphaMap
	};
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(const Context* info = nullptr);
	vk::DescriptorSet descriptorSet;
	std::vector<Vertex> vertices{};
	std::vector<uint32_t> indices{};
	Image texture;
	Image normalsTexture;
	Image specularTexture;
	Image alphaTexture;
	Effects colorEffects;

	void loadTexture(TextureType type, const std::string path, const Context* info);
	void destroy(const Context* info);
};

struct Model
{
	bool enabled = true;
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(const Context* info = nullptr);
	static specificGraphicsPipelineCreateInfo getPipelineSpecifications(const Context* info);
	vk::DescriptorSet descriptorSet;
	std::map<std::string, Image> uniqueTextures{};
	std::vector<Mesh> meshes{};
	std::string name;
	glm::mat4 matrix = glm::mat4(1.0f);
	Buffer vertexBuffer;
	Buffer indexBuffer;
	Buffer uniformBuffer;
	uint32_t numberOfVertices;
	uint32_t numberOfIndices;
	float initialBoundingSphereRadius = 0.0f;

	glm::vec4 getBoundingSphere();
	static Model loadModel(const std::string path, const std::string modelName, const Context* info);
	void createVertexBuffer(const Context* info);
	void createIndexBuffer(const Context* info);
	void createUniformBuffers(const Context* info);
	void createDescriptorSets(const Context* info);
	void destroy(const Context* info);
};

struct Camera
{
	enum struct RelativeDirection {
		FORWARD,
		BACKWARD,
		LEFT,
		RIGHT
	};
	glm::vec3 position;
	glm::vec3 front;
	glm::vec3 up;
	glm::vec3 right;
	glm::vec3 worldUp;
	float aspect = 4.f/3.f;
	float nearPlane = 0.01f;
	float farPlane = 50.0f;
	float FOV = 45.0f;
	float yaw;
	float pitch;
	float speed;
	float rotationSpeed;

	Camera();
	glm::mat4 getPerspective();
	glm::mat4 getView();
	void move(RelativeDirection direction, float deltaTime, bool combineDirections = false);
	void rotate(float xoffset, float yoffset);
};

struct ProjView
{
	glm::mat4 projection;
	glm::mat4 view;
};

struct UBO
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model;
};

struct ShadowsUBO
{
	glm::mat4 projection;
	glm::mat4 view;
	glm::mat4 model;
	float castShadows;
};

struct Light
{
	glm::vec4 color;
	glm::vec4 position;
	glm::vec4 attenuation;
	//void attachTo(glm::mat4& matrix, glm::vec3 offset);
};

struct Context
{
private:
	uint32_t __id = 0;
	std::map<uint32_t, std::any> var{};
public:
	template<typename T>
	void add(uint32_t* assignTo, const T & value) {
		if (*assignTo != UINT32_MAX) throw std::runtime_error("attempt to write to an existing object");
		*assignTo = __id++;
		var[*assignTo] = std::any(value);
	}
	template<typename T>
	T& get(const uint32_t id) {
		if (id == UINT32_MAX) throw std::runtime_error("attempt to get an invalid object");
		return std::any_cast<T&>(var[id]);
	}

    SDL_Window* window;
    Surface surface;
	vk::Instance instance;
    vk::PhysicalDevice gpu;
    vk::PhysicalDeviceProperties gpuProperties;
    vk::PhysicalDeviceFeatures gpuFeatures;
    int graphicsFamilyId = -1;
    int presentFamilyId = -1;
    vk::Device device;
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;
    vk::CommandPool commandPool;
    vk::RenderPass renderPass;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
	Swapchain swapchain;
    Image depth;
	Image multiSampleColorImage;
	Image multiSampleDepthImage;
    std::vector<vk::Framebuffer> frameBuffers{};
    std::vector<vk::CommandBuffer> cmdBuffers{};
	bool useDynamicCmdBuffer = true;
	vk::CommandBuffer dynamicCmdBuffer;
	std::vector<vk::CommandBuffer> shadowsPassCmdBuffers{};
	vk::DescriptorPool descriptorPool;
	Pipeline pipeline;
	Pipeline pipelineGUI;
	Pipeline pipelineSkyBox;
	Pipeline pipelineShadows;
	Pipeline pipelineTerrain;
    std::vector<vk::Semaphore> semaphores{};
    std::vector<Model> models{};
	std::vector<Shadows> shadows{};
	GUI gui;
	SkyBox skyBox;
	Camera mainCamera;
	Terrain terrain;
	std::vector<Light> light{};
};

#endif // CONTEXT_H