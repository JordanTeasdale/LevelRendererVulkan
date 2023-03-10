#include "FSLogo.h"
#include "load_data_oriented.h"
#include "shaderc/shaderc.h" // needed for compiling shaders at runtime

#ifdef _WIN32 // must use MT platform DLL libraries on windows
	#pragma comment(lib, "shaderc_combined.lib") 
#endif

#define KHRONOS_STATIC // must be defined if ktx libraries are built statically
#include <ktxvulkan.h>
// Imports Shader from External Files
std::string ShaderAsString(const char* shaderFilePath) {
	std::string output;
	unsigned int stringLength = 0;
	GW::SYSTEM::GFile file; file.Create();
	file.GetFileSize(shaderFilePath, stringLength);
	if (stringLength && +file.OpenBinaryRead(shaderFilePath)) {
		output.resize(stringLength);
		file.Read(&output[0], stringLength);
	}
	else
		std::cout << "ERROR: Shader Source File \"" << shaderFilePath << "\" Not Found!" << std::endl;
	return output;
}

// Simple Vertex Shader
std::string vertexString = ShaderAsString("../BasicVertexShader.hlsl");
const char* vertexShaderSource = vertexString.c_str();

// Simple Pixel Shader
std::string fragmentString = ShaderAsString("../BasicPixelShader.hlsl");
const char* pixelShaderSource = fragmentString.c_str();

// Texture Pixel Shader
std::string textureFragmentString = ShaderAsString("../TexturePixelShader.hlsl");
const char* texturePixelShaderSource = textureFragmentString.c_str();

// Creation, Rendering & Cleanup
class Renderer
{
#define MAX_SUBMESH_PER_DRAW 1054 // we can change this if desired
	struct SHADER_MODEL_DATA {
		//gloabally shared model data
		GW::MATH::GVECTORF sunDirection = { -1, -1, 2 }, sunColor; // lighting info
		GW::MATH::GVECTORF sunAmbient = { 0.25, 0.25, 0.35 }, cameraPos;
		GW::MATH::GMATRIXF viewMatrix, projectionMatrix; // viewing info
		// per sub-mesh transform and material data
		GW::MATH::GMATRIXF matricies[MAX_SUBMESH_PER_DRAW]; // world space transforms
		H2B::ATTRIBUTES materials[MAX_SUBMESH_PER_DRAW]; // color/texture of surface
	};
	struct Push_Constants {
		unsigned materialIndex;
		unsigned startWorld;
	};

	// proxy handles
	GW::SYSTEM::GWindow win;
	GW::GRAPHICS::GVulkanSurface vlk;
	GW::CORE::GEventReceiver shutdown;
	Level_Data levelData;

	GW::INPUT::GInput inputProxy;
	GW::INPUT::GController controllerProxy;

	VkDevice device = nullptr;
	VkBuffer vertexHandle = nullptr;
	VkDeviceMemory vertexData = nullptr;
	
	VkBuffer indexHandle = nullptr;
	VkDeviceMemory indexData = nullptr;
	
	std::vector<VkBuffer> storageHandle;
	std::vector<VkDeviceMemory> storageData;
	VkShaderModule vertexShader = nullptr;
	VkShaderModule pixelShader = nullptr;
	VkShaderModule texturePixelShader = nullptr;
	
	VkPipeline pipeline = nullptr;
	VkPipelineLayout pipelineLayout = nullptr;
	
	VkDescriptorSetLayout descriptorLayout = nullptr;
	VkDescriptorPool descriptorPool = nullptr;
	std::vector<VkDescriptorSet> descriptorSet;

	// Texturing Stuff
	VkDescriptorSet textureDescriptorSet = nullptr;
	VkDescriptorSetLayout pixelDescriptorLayout = nullptr;
	VkPipeline texturePipeline = nullptr;

	GW::MATH::GMatrix proxy;
	GW::MATH::GMATRIXF camera = GW::MATH::GIdentityMatrixF;
	float aspect;
	GW::MATH::GMATRIXF perspective;
	GW::MATH::GMATRIXF world = GW::MATH::GIdentityMatrixF;
	std::chrono::steady_clock::time_point start;
	GW::MATH::GVECTORF lightDir = { -1, -1, 2 };
	GW::MATH::GVECTORF lightClr = { 0.9, 0.9, 1.0};
	
	SHADER_MODEL_DATA sceneData;
	Push_Constants pushConstants;

	struct Texture {
		ktxVulkanTexture texture;
		VkDescriptorSet descriptorSet = nullptr;
		VkImageView textureView = nullptr;
		VkSampler textureSampler = nullptr;
	};
	std::vector<Texture> levelTextures;

	unsigned indexOffset = 0;
	unsigned vertexOffset = 0;
	unsigned materialOffset = 0;
	
public:

	Renderer(GW::SYSTEM::GWindow _win, GW::GRAPHICS::GVulkanSurface _vlk, Level_Data _levelData)
	{
		start = std::chrono::steady_clock::now();
		win = _win;
		vlk = _vlk;
		levelData = _levelData;
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);

		inputProxy.Create(win);
		controllerProxy.Create();

		proxy.Create();
		GW::MATH::GVECTORF eye = { 0.75, 0.25, -1.5, 1 };
		GW::MATH::GVECTORF center = { 0.15, 0.75, 0, 1 };
		GW::MATH::GVECTORF up = { 0, 1, 0, 0 };
		proxy.LookAtLHF(eye, center, up, camera);
		vlk.GetAspectRatio(aspect);
		proxy.ProjectionVulkanLHF(1.13446f, aspect, 0.1f, 100.0f, perspective);

		sceneData.sunColor = lightClr;
		sceneData.viewMatrix = camera;
		sceneData.projectionMatrix = perspective;
		sceneData.cameraPos = eye;

		for (int i = 0; i < levelData.levelMaterials.size(); ++i) {
			sceneData.materials[i] = levelData.levelMaterials[i].attrib;
		}
		for (int i = 0; i < levelData.levelTransforms.size(); ++i) {
			sceneData.matricies[i] = levelData.levelTransforms[i];
		}

		/***************** GEOMETRY INTIALIZATION ******************/
		// Grab the device & physical device so we can allocate some stuff
		VkPhysicalDevice physicalDevice = nullptr;
		vlk.GetDevice((void**)&device);
		vlk.GetPhysicalDevice((void**)&physicalDevice);

		// Transfer triangle data to the vertex buffer. (staging would be prefered here)
		unsigned vertexSize = levelData.levelVertices.size() * sizeof(H2B::VERTEX);
		GvkHelper::create_buffer(physicalDevice, device, vertexSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexHandle, &vertexData);
		GvkHelper::write_to_buffer(device, vertexData, levelData.levelVertices.data(), vertexSize);

		unsigned indexSize = levelData.levelIndices.size() * sizeof(unsigned);
		GvkHelper::create_buffer(physicalDevice, device, indexSize,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indexHandle, &indexData);
		GvkHelper::write_to_buffer(device, indexData, levelData.levelIndices.data(), indexSize);

		UINT32 numBBS = 0;
		vlk.GetSwapchainImageCount(numBBS);
		storageHandle.resize(numBBS);
		storageData.resize(numBBS);
		descriptorSet.resize(numBBS);
		for (int i = 0; i < numBBS; ++i) {
			GvkHelper::create_buffer(physicalDevice, device, sizeof(sceneData),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &storageHandle[i], &storageData[i]);
			GvkHelper::write_to_buffer(device, storageData[i], &sceneData, sizeof(sceneData));
		}

		/***************** SHADER INTIALIZATION ******************/
		// Intialize runtime shader compiler HLSL -> SPIRV
		shaderc_compiler_t compiler = shaderc_compiler_initialize();
		shaderc_compile_options_t options = shaderc_compile_options_initialize();
		shaderc_compile_options_set_source_language(options, shaderc_source_language_hlsl);
		shaderc_compile_options_set_invert_y(options, false); // TODO: Part 2i
#ifndef NDEBUG
		shaderc_compile_options_set_generate_debug_info(options);
#endif
		// Create Vertex Shader
		shaderc_compilation_result_t result = shaderc_compile_into_spv( // compile
			compiler, vertexShaderSource, strlen(vertexShaderSource),
			shaderc_vertex_shader, "main.vert", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Vertex Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &vertexShader);
		shaderc_result_release(result); // done
		// Create Pixel Shader
		result = shaderc_compile_into_spv( // compile
			compiler, pixelShaderSource, strlen(pixelShaderSource),
			shaderc_fragment_shader, "main.frag", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Pixel Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &pixelShader);
		shaderc_result_release(result); // done
		// Create Texture Pixel Shader
		result = shaderc_compile_into_spv( // compile
			compiler, texturePixelShaderSource, strlen(texturePixelShaderSource),
			shaderc_fragment_shader, "main.frag", "main", options);
		if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) // errors?
			std::cout << "Pixel Shader Errors: " << shaderc_result_get_error_message(result) << std::endl;
		GvkHelper::create_shader_module(device, shaderc_result_get_length(result), // load into Vulkan
			(char*)shaderc_result_get_bytes(result), &texturePixelShader);
		shaderc_result_release(result); // done
		// Free runtime shader compiler resources
		shaderc_compile_options_release(options);
		shaderc_compiler_release(compiler);

		/***************** PIPELINE INTIALIZATION ******************/
		// Create Pipeline & Layout (Thanks Tiny!)
		VkRenderPass renderPass;
		vlk.GetRenderPass((void**)&renderPass);
		VkPipelineShaderStageCreateInfo stage_create_info[2] = {};
		// Create Stage Info for Vertex Shader
		stage_create_info[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage_create_info[0].module = vertexShader;
		stage_create_info[0].pName = "main";
		// Create Stage Info for Fragment Shader
		stage_create_info[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage_create_info[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stage_create_info[1].module = pixelShader;
		stage_create_info[1].pName = "main";
		// Assembly State
		VkPipelineInputAssemblyStateCreateInfo assembly_create_info = {};
		assembly_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		assembly_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		assembly_create_info.primitiveRestartEnable = false;
		// Vertex Input State
		VkVertexInputBindingDescription vertex_binding_description = {};
		vertex_binding_description.binding = 0;
		vertex_binding_description.stride = sizeof(H2B::VERTEX);
		vertex_binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		VkVertexInputAttributeDescription vertex_attribute_description[3] = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, //uv, normal, etc....
			{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
			{ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24 }
		};
		VkPipelineVertexInputStateCreateInfo input_vertex_info = {};
		input_vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		input_vertex_info.vertexBindingDescriptionCount = 1;
		input_vertex_info.pVertexBindingDescriptions = &vertex_binding_description;
		input_vertex_info.vertexAttributeDescriptionCount = 3;
		input_vertex_info.pVertexAttributeDescriptions = vertex_attribute_description;
		// Viewport State (we still need to set this up even though we will overwrite the values)
		VkViewport viewport = {
			0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1
		};
		VkRect2D scissor = { {0, 0}, {width, height} };
		VkPipelineViewportStateCreateInfo viewport_create_info = {};
		viewport_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_create_info.viewportCount = 1;
		viewport_create_info.pViewports = &viewport;
		viewport_create_info.scissorCount = 1;
		viewport_create_info.pScissors = &scissor;
		// Rasterizer State
		VkPipelineRasterizationStateCreateInfo rasterization_create_info = {};
		rasterization_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterization_create_info.rasterizerDiscardEnable = VK_FALSE;
		rasterization_create_info.polygonMode = VK_POLYGON_MODE_FILL;
		rasterization_create_info.lineWidth = 1.0f;
		rasterization_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterization_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterization_create_info.depthClampEnable = VK_FALSE;
		rasterization_create_info.depthBiasEnable = VK_FALSE;
		rasterization_create_info.depthBiasClamp = 0.0f;
		rasterization_create_info.depthBiasConstantFactor = 0.0f;
		rasterization_create_info.depthBiasSlopeFactor = 0.0f;
		// Multisampling State
		VkPipelineMultisampleStateCreateInfo multisample_create_info = {};
		multisample_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample_create_info.sampleShadingEnable = VK_FALSE;
		multisample_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisample_create_info.minSampleShading = 1.0f;
		multisample_create_info.pSampleMask = VK_NULL_HANDLE;
		multisample_create_info.alphaToCoverageEnable = VK_FALSE;
		multisample_create_info.alphaToOneEnable = VK_FALSE;
		// Depth-Stencil State
		VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info = {};
		depth_stencil_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depth_stencil_create_info.depthTestEnable = VK_TRUE;
		depth_stencil_create_info.depthWriteEnable = VK_TRUE;
		depth_stencil_create_info.depthCompareOp = VK_COMPARE_OP_LESS;
		depth_stencil_create_info.depthBoundsTestEnable = VK_FALSE;
		depth_stencil_create_info.minDepthBounds = 0.0f;
		depth_stencil_create_info.maxDepthBounds = 1.0f;
		depth_stencil_create_info.stencilTestEnable = VK_FALSE;
		// Color Blending Attachment & State
		VkPipelineColorBlendAttachmentState color_blend_attachment_state = {};
		color_blend_attachment_state.colorWriteMask = 0xF;
		color_blend_attachment_state.blendEnable = VK_FALSE;
		color_blend_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		color_blend_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		color_blend_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
		color_blend_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		color_blend_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		color_blend_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
		VkPipelineColorBlendStateCreateInfo color_blend_create_info = {};
		color_blend_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend_create_info.logicOpEnable = VK_FALSE;
		color_blend_create_info.logicOp = VK_LOGIC_OP_COPY;
		color_blend_create_info.attachmentCount = 1;
		color_blend_create_info.pAttachments = &color_blend_attachment_state;
		color_blend_create_info.blendConstants[0] = 0.0f;
		color_blend_create_info.blendConstants[1] = 0.0f;
		color_blend_create_info.blendConstants[2] = 0.0f;
		color_blend_create_info.blendConstants[3] = 0.0f;
		// Dynamic State 
		VkDynamicState dynamic_state[2] = {
			// By setting these we do not need to re-create the pipeline on Resize
			VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamic_create_info = {};
		dynamic_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_create_info.dynamicStateCount = 2;
		dynamic_create_info.pDynamicStates = dynamic_state;

		VkDescriptorSetLayoutBinding layout_binding = {};
		layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		layout_binding.binding = 0;
		layout_binding.descriptorCount = 1;
		VkDescriptorSetLayoutCreateInfo layout_create_info = {};
		layout_create_info.bindingCount = 1;
		layout_create_info.pBindings = &layout_binding;
		layout_create_info.flags = 0;
		layout_create_info.pNext = nullptr;
		layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr, &descriptorLayout);
		
		VkDescriptorPoolSize pool_size = {};
		pool_size.descriptorCount = numBBS;
		pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		VkDescriptorPoolCreateInfo pool_create_info = {};
		pool_create_info.maxSets = numBBS;
		pool_create_info.poolSizeCount = 1;
		pool_create_info.pPoolSizes = &pool_size;
		pool_create_info.pNext = nullptr;
		pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_create_info.flags = 0;
		vkCreateDescriptorPool(device, &pool_create_info, nullptr, &descriptorPool);
		for (int i = 0; i < numBBS; i++)
		{
			VkDescriptorSetAllocateInfo set_allocate_info = {};
			set_allocate_info.descriptorPool = descriptorPool;
			set_allocate_info.descriptorSetCount = 1;
			set_allocate_info.pNext = nullptr;
			set_allocate_info.pSetLayouts = &descriptorLayout;
			set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			vkAllocateDescriptorSets(device, &set_allocate_info, &descriptorSet[i]);

			VkDescriptorBufferInfo descriptor_buffer_info = {};
			descriptor_buffer_info.buffer = storageHandle[i];
			descriptor_buffer_info.offset = 0;
			descriptor_buffer_info.range = VK_WHOLE_SIZE;
			VkWriteDescriptorSet write_descriptor_set = {};
			write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_descriptor_set.pNext = nullptr;
			write_descriptor_set.dstSet = descriptorSet[i];
			write_descriptor_set.dstBinding = 0;
			write_descriptor_set.dstArrayElement = 0;
			write_descriptor_set.descriptorCount = 1;
			write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			write_descriptor_set.pImageInfo = nullptr;
			write_descriptor_set.pBufferInfo = &descriptor_buffer_info;
			write_descriptor_set.pTexelBufferView = nullptr;
			vkUpdateDescriptorSets(device, 1, &write_descriptor_set, 0, nullptr);
		}

	// Descriptor pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_create_info = {};
		pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		
		pipeline_layout_create_info.setLayoutCount = 1;
		pipeline_layout_create_info.pSetLayouts = &descriptorLayout;
		
		VkPushConstantRange push_constant_range = {};
		push_constant_range.offset = 0;
		push_constant_range.size = sizeof(Push_Constants);
		push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		pipeline_layout_create_info.pushConstantRangeCount = 1;
		pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
		vkCreatePipelineLayout(device, &pipeline_layout_create_info,
			nullptr, &pipelineLayout);
		// Pipeline State... (FINALLY) 
		VkGraphicsPipelineCreateInfo pipeline_create_info = {};
		pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_create_info.stageCount = 2;
		pipeline_create_info.pStages = stage_create_info;
		pipeline_create_info.pInputAssemblyState = &assembly_create_info;
		pipeline_create_info.pVertexInputState = &input_vertex_info;
		pipeline_create_info.pViewportState = &viewport_create_info;
		pipeline_create_info.pRasterizationState = &rasterization_create_info;
		pipeline_create_info.pMultisampleState = &multisample_create_info;
		pipeline_create_info.pDepthStencilState = &depth_stencil_create_info;
		pipeline_create_info.pColorBlendState = &color_blend_create_info;
		pipeline_create_info.pDynamicState = &dynamic_create_info;
		pipeline_create_info.layout = pipelineLayout;
		pipeline_create_info.renderPass = renderPass;
		pipeline_create_info.subpass = 0;
		pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
			&pipeline_create_info, nullptr, &pipeline);

		/***************** TEXTURE DESCRIPTOR FOR FRAGMENT/PIXEL SHADER ******************/

		stage_create_info[1].module = texturePixelShader;
		pipeline_create_info.pStages = stage_create_info;

		// desribes the order and type of resources bound to the pixel shader
		VkDescriptorSetLayoutBinding pshader_descriptor_layout_binding = {};
		pshader_descriptor_layout_binding.binding = 0;
		pshader_descriptor_layout_binding.descriptorCount = 1;
		pshader_descriptor_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		pshader_descriptor_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pshader_descriptor_layout_binding.pImmutableSamplers = nullptr;
		// pixel shader will have its own descriptor set layout
		layout_create_info.pBindings = &pshader_descriptor_layout_binding;
		vkCreateDescriptorSetLayout(device, &layout_create_info,
			nullptr, &pixelDescriptorLayout);
		// Create a descriptor pool!
		// this is how many unique descriptor sets you want to allocate 
		// we need one for each uniform buffer and one for each unique texture
		unsigned int total_descriptorsets = numBBS + 1;
		VkDescriptorPoolCreateInfo descriptorpool_create_info = {};
		descriptorpool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		VkDescriptorPoolSize descriptorpool_size[2] = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, numBBS },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
		};
		descriptorpool_create_info.poolSizeCount = 2;
		descriptorpool_create_info.pPoolSizes = descriptorpool_size;
		descriptorpool_create_info.maxSets = total_descriptorsets;
		descriptorpool_create_info.flags = 0;
		descriptorpool_create_info.pNext = nullptr;
		vkCreateDescriptorPool(device, &descriptorpool_create_info, nullptr, &descriptorPool);
		// Create a descriptor set for our texture!
		VkDescriptorSetAllocateInfo descriptorset_allocate_info = {};
		descriptorset_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorset_allocate_info.descriptorSetCount = 1;
		descriptorset_allocate_info.pSetLayouts = &pixelDescriptorLayout;
		descriptorset_allocate_info.descriptorPool = descriptorPool;
		descriptorset_allocate_info.pNext = nullptr;
		vkAllocateDescriptorSets(device, &descriptorset_allocate_info, &textureDescriptorSet);
		// end texturing descriptor specifics

		vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
			&pipeline_create_info, nullptr, &texturePipeline);



		for (int i = 0; i < levelData.levelMaterials.size(); ++i) {
			if (levelData.levelMaterials[i].map_Kd != NULL)
				levelTextures.push_back(LoadTextures(levelData.levelMaterials[i].map_Kd));
			else
				levelTextures.push_back(Texture());
		}

		/***************** CLEANUP / SHUTDOWN ******************/
		// GVulkanSurface will inform us when to release any allocated resources
		shutdown.Create(vlk, [&]() {
			if (+shutdown.Find(GW::GRAPHICS::GVulkanSurface::Events::RELEASE_RESOURCES, true)) {
				CleanUp(); // unlike D3D we must be careful about destroy timing
			}
			});
	}

	Texture LoadTextures(const char* texturePath)
	{
		Texture output;

		// Gateware, access to underlying Vulkan queue and command pool & physical device
		VkQueue graphicsQueue;
		VkCommandPool cmdPool;
		VkPhysicalDevice physicalDevice;
		vlk.GetGraphicsQueue((void**)&graphicsQueue);
		vlk.GetCommandPool((void**)&cmdPool);
		vlk.GetPhysicalDevice((void**)&physicalDevice);
		// libktx, temporary variables
		ktxTexture* kTexture;
		KTX_error_code ktxresult;
		ktxVulkanDeviceInfo vdi;
		// used to transfer texture CPU memory to GPU. just need one
		ktxresult = ktxVulkanDeviceInfo_Construct(&vdi, physicalDevice, device,
			graphicsQueue, cmdPool, nullptr);
		if (ktxresult != KTX_error_code::KTX_SUCCESS)
			return output;
		// load texture into CPU memory from file
		ktxresult = ktxTexture_CreateFromNamedFile(texturePath,
			KTX_TEXTURE_CREATE_NO_FLAGS, &kTexture);
		if (ktxresult != KTX_error_code::KTX_SUCCESS)
			return output;
		// This gets mad if you don't encode/save the .ktx file in a format Vulkan likes
		ktxresult = ktxTexture_VkUploadEx(kTexture, &vdi, &output.texture,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		if (ktxresult != KTX_error_code::KTX_SUCCESS)
			return output;
		// after loading all textures you don't need these anymore
		ktxTexture_Destroy(kTexture);
		ktxVulkanDeviceInfo_Destruct(&vdi);

		// create the the image view and sampler
		VkSamplerCreateInfo samplerInfo = {};
		// Set the struct values
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.flags = 0;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; // REPEAT IS COMMON
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.mipLodBias = 0;
		samplerInfo.minLod = 0;
		samplerInfo.maxLod = output.texture.levelCount;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.pNext = nullptr;
		VkResult vr = vkCreateSampler(device, &samplerInfo, nullptr, &output.textureSampler);
		if (vr != VkResult::VK_SUCCESS)
			return output;

		// Create image view.
		// Textures are not directly accessed by the shaders and are abstracted
		// by image views containing additional information and sub resource ranges.
		VkImageViewCreateInfo viewInfo = {};
		// Set the non-default values.
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.flags = 0;
		viewInfo.components = {
			VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A
		};
		viewInfo.image = output.texture.image;
		viewInfo.format = output.texture.imageFormat;
		viewInfo.viewType = output.texture.viewType;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.layerCount = output.texture.layerCount;
		viewInfo.subresourceRange.levelCount = output.texture.levelCount;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.pNext = nullptr;
		vr = vkCreateImageView(device, &viewInfo, nullptr, &output.textureView);
		if (vr != VkResult::VK_SUCCESS)
			return output;

		// update the descriptor set(s) to point to the correct views
		VkWriteDescriptorSet write_descriptorset = {};
		write_descriptorset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptorset.descriptorCount = 1;
		write_descriptorset.dstArrayElement = 0;
		write_descriptorset.dstBinding = 0;
		write_descriptorset.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write_descriptorset.dstSet = textureDescriptorSet;
		VkDescriptorImageInfo diinfo = { output.textureSampler, output.textureView, output.texture.imageLayout };
		write_descriptorset.pImageInfo = &diinfo;
		vkUpdateDescriptorSets(device, 1, &write_descriptorset, 0, nullptr);

		output.descriptorSet = textureDescriptorSet;

		return output;
	}

	void Render()
	{
		// TODO: Part 2a
		auto end = std::chrono::steady_clock::now();
		std::chrono::duration<float> timer = end - start;
		proxy.RotateYLocalF(world, 0.5 * timer.count(), world);
		// TODO: Part 4d
		//sceneData.matricies[1] = world;
		// grab the current Vulkan commandBuffer
		sceneData.viewMatrix = camera;
		sceneData.cameraPos = camera.row4;
		unsigned int currentBuffer;
		vlk.GetSwapchainCurrentImage(currentBuffer);
		VkCommandBuffer commandBuffer;
		vlk.GetCommandBuffer(currentBuffer, (void**)&commandBuffer);
		// what is the current client area dimensions?
		unsigned int width, height;
		win.GetClientWidth(width);
		win.GetClientHeight(height);
		// setup the pipeline's dynamic settings
		VkViewport viewport = {
			0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1
		};
		VkRect2D scissor = { {0, 0}, {width, height} };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// now we can draw
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexHandle, offsets);
		// TODO: Part 1h
		vkCmdBindIndexBuffer(commandBuffer, indexHandle, 0, VK_INDEX_TYPE_UINT32);
		// TODO: Part 4d
		UINT32 currentImage = 0;
		vlk.GetSwapchainCurrentImage(currentImage);
		GvkHelper::write_to_buffer(device, storageData[currentImage], &sceneData, sizeof(SHADER_MODEL_DATA));
		// TODO: Part 2i
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet[currentImage], 0, nullptr);

		indexOffset = 0;
		vertexOffset = 0;
		materialOffset = 0;
		for (size_t j = 0; j < levelData.levelModels.size(); j++)
		{
			pushConstants.startWorld = levelData.levelInstances[j].transformStart;
			for (int i = levelData.levelModels[j].meshStart; i < levelData.levelModels[j].meshCount + levelData.levelModels[j].meshStart; ++i) {
				if (levelTextures[levelData.levelMeshes[i].materialIndex + materialOffset].descriptorSet == nullptr) {
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet[currentImage], 0, nullptr);
				}
				else {
					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, texturePipeline);
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &textureDescriptorSet, 0, nullptr);
				}
				pushConstants.materialIndex = levelData.levelMeshes[i].materialIndex + materialOffset;
				vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push_Constants), &pushConstants);
				vkCmdDrawIndexed(commandBuffer, levelData.levelMeshes[i].drawInfo.indexCount, levelData.levelInstances[j].transformCount, levelData.levelMeshes[i].drawInfo.indexOffset + indexOffset, vertexOffset, 0);
			}
			indexOffset += levelData.levelModels[j].indexCount;
			vertexOffset += levelData.levelModels[j].vertexCount;
			materialOffset += levelData.levelModels[j].materialCount;
		}

		start = std::chrono::steady_clock::now();
	}

	void UpdateCamera() {
		const float cameraSpeed = 0.8;
		auto end = std::chrono::steady_clock::now();
		std::chrono::duration<float> timer = end - start;
		float frameSpeed = cameraSpeed * timer.count();
		float thumbSpeed = (355 / 113) * timer.count();
		// TODO: Part 4c
		GW::MATH::GMATRIXF tempCam;
		proxy.InverseF(camera, tempCam);
		// TODO: Part 4d
		float rTrig = 0;
		float lTrig = 0;
		float space = 0;
		float shift = 0;
		float wKey = 0;
		float sKey = 0;
		float LYStick = 0;
		float aKey = 0;
		float dKey = 0;
		float LXStick = 0;
		float mouseX = 0;
		float mouseY = 0;
		float RYStick = 0;
		float RXStick = 0;
		controllerProxy.GetState(0, G_RIGHT_TRIGGER_AXIS, rTrig);
		controllerProxy.GetState(0, G_LEFT_TRIGGER_AXIS, lTrig);
		controllerProxy.GetState(0, G_LY_AXIS, LYStick);
		controllerProxy.GetState(0, G_LX_AXIS, LXStick);
		controllerProxy.GetState(0, G_RY_AXIS, RYStick);
		controllerProxy.GetState(0, G_RX_AXIS, RXStick);
		inputProxy.GetState(G_KEY_SPACE, space);
		inputProxy.GetState(G_KEY_LEFTSHIFT, shift);
		inputProxy.GetState(G_KEY_W, wKey);
		inputProxy.GetState(G_KEY_S, sKey);
		inputProxy.GetState(G_KEY_A, aKey);
		inputProxy.GetState(G_KEY_D, dKey);
		if (inputProxy.GetMouseDelta(mouseX, mouseY) == GW::GReturn::REDUNDANT) {
			mouseX = 0;
			mouseY = 0;
		}
		float yChange = space - shift + rTrig - lTrig;
		float zChange = wKey - sKey + LYStick;
		float xChange = dKey - aKey + LXStick;
		GW::MATH::GVECTORF trans = { 0, yChange * frameSpeed, 0 };
		
		proxy.TranslateGlobalF(tempCam, trans, tempCam);
		trans = { xChange * frameSpeed, 0 , zChange * frameSpeed };
		proxy.TranslateLocalF(tempCam, trans, tempCam);
		// TODO: Part 4e
		// TODO: Part 4f
		float totalPitch = 1.13446f * mouseY / 600 + RYStick;
		float totalYaw = 1.13446f * aspect * mouseX / 800 + RXStick;
		proxy.RotateXLocalF(tempCam, totalPitch, tempCam);
		proxy.RotateYGlobalF(tempCam, totalYaw, tempCam);
		// TODO: Part 4g
		// TODO: Part 4c
		proxy.InverseF(tempCam, camera);
		start = std::chrono::steady_clock::now();
	}

private:
	void CleanUp()
	{
		// wait till everything has completed
		vkDeviceWaitIdle(device);
		// Release allocated buffers, shaders & pipeline
		// TODO: Part 1g
		vkDestroyBuffer(device, indexHandle, nullptr);
		vkFreeMemory(device, indexData, nullptr);
		// TODO: Part 2d
		for (int i = 0; i < 2; ++i) {
			vkDestroyBuffer(device, storageHandle[i], nullptr);
			vkFreeMemory(device, storageData[i], nullptr);
		}
		

		vkDestroyBuffer(device, vertexHandle, nullptr);
		vkFreeMemory(device, vertexData, nullptr);
		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, pixelShader, nullptr);
		// TODO: Part 2e
		vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
		// TODO: part 2f
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipeline(device, pipeline, nullptr);
	}
};
