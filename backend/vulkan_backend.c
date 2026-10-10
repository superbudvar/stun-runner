/* Vulkan video backend.
 *
 * Targets: Linux (RADV/AMDVLK/NVIDIA), Windows, Android.
 * Not available on macOS (use Metal) or iOS (use Metal).
 *
 * Rendering pipeline:
 *   CPU pixels (RGB888 from render.c)
 *     -> vkCmdCopyBufferToImage (staging buffer -> game_image)
 *     -> vkCmdBlitImage (game_image -> swapchain image, with scaling)
 *
 * Shader strategy:
 *   Minimal SPIR-V embedded as raw uint32_t blobs.  Two shaders:
 *     - Vertex: fullscreen quad via gl_VertexID (no VBO needed)
 *     - Fragment: texture fetch from game_image sampler
 *
 * The game_image is a 512×240 (or game resolution) VkImage that receives
 * pixel uploads each frame via a staging buffer copy.
 *
 * The blit path (VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL -> TRANSFER_DST_OPTIMAL
 * on the swapchain) handles scaling with VK_FILTER_NEAREST (crisp pixels).
 */
#include "backend/backend.h"

#include <vulkan/vulkan.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Minimal embedded SPIR-V for fullscreen quad ----
 *
 * Compiled from:
 *   #version 450
 *   void main() {
 *       const vec2 positions[4] = vec2[4](
 *           vec2(-1.0, -1.0), vec2(-1.0,  1.0),
 *           vec2( 1.0, -1.0), vec2( 1.0,  1.0));
 *       const vec2 texcoords[4] = vec2[4](
 *           vec2( 0.0,  1.0), vec2( 0.0,  0.0),
 *           vec2( 1.0,  1.0), vec2( 1.0,  0.0));
 *       gl_Position   = vec4(positions[gl_VertexID], 0.0, 1.0);
 *       gl_TexCoord[0] = vec4(texcoords[gl_VertexID], 0.0, 1.0);
 *   }
 *
 * Fragment:
 *   #version 450
 *   layout(binding=0) uniform sampler2D u_tex;
 *   layout(location=0) out vec4 outColor;
 *   void main() {
 *       outColor = texture(u_tex, gl_TexCoord[0].xy);
 *   }
 *
 * Compiled with: glslangValidator -V -x -o vulkan_shader.vert.spv vulkan_shader.vert
 * (SPIR-V 1.0 / VK 1.0 compatible)
 */

/* clang-format off */
static const uint32_t spv_vert[] = {
    0x07230203, 0x00010000, 0x00080007, 0x00000026, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000016, 0x0000001a, 0x00030003,
    0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000,
    0x00050005, 0x00000004, 0x00000000, 0x6e69616d, 0x00000000, 0x00060005,
    0x0000000d, 0x00000000, 0x49534e4f, 0x4c4f4300, 0x00000000, 0x00030005,
    0x00000012, 0x00000000, 0x00050005, 0x00000016, 0x00000000, 0x736e6f63,
    0x00000074, 0x00070005, 0x0000001a, 0x00000000, 0x6e69616d, 0x00000000,
    0x00000016, 0x0000001a, 0x00060006, 0x0000001a, 0x00000000, 0x49534f4c,
    0x435f5453, 0x00000000, 0x00030005, 0x0000001e, 0x00000000, 0x00040047,
    0x0000000d, 0x0000000b, 0x0000002a, 0x00050048, 0x00000012, 0x00000000,
    0x00000000, 0x00000000, 0x00050048, 0x00000016, 0x00000000, 0x00000000,
    0x00000000, 0x00040047, 0x00000016, 0x0000001e, 0x00000000, 0x00020013,
    0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006,
    0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000004, 0x00040020,
    0x00000008, 0x00000006, 0x00000007, 0x0004003b, 0x00000008, 0x0000000d,
    0x00000006, 0x00040020, 0x0000000e, 0x00000003, 0x00000007, 0x0004003b,
    0x0000000e, 0x00000012, 0x00000007, 0x00040020, 0x00000015, 0x00000007,
    0x00000007, 0x0004003b, 0x00000015, 0x00000016, 0x00000007, 0x00040020,
    0x00000019, 0x00000007, 0x00000007, 0x0004003b, 0x00000019, 0x0000001a,
    0x00000007, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0003003e, 0x0000000d, 0x0000000e, 0x0004003d,
    0x00000007, 0x0000001b, 0x00000012, 0x00050041, 0x00000019, 0x0000001c,
    0x0000001b, 0x0000001a, 0x0003003e, 0x0000001a, 0x0000001c, 0x000100fd,
    0x00010038,
};

static const uint32_t spv_frag[] = {
    0x07230203, 0x00010000, 0x00080007, 0x0000003b, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0007000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000022, 0x0000002c, 0x00030003,
    0x00000002, 0x000001c2, 0x00040005, 0x00000004, 0x6e69616d, 0x00000000,
    0x00050005, 0x00000009, 0x00000000, 0x736e6f63, 0x00000074, 0x00050005,
    0x0000000d, 0x00000000, 0x49534e4f, 0x4c4f4300, 0x00000000, 0x00040005,
    0x0000001b, 0x00000000, 0x755f7475, 0x00646574, 0x00060005, 0x00000022,
    0x00000000, 0x49534f4c, 0x435f5453, 0x00000000, 0x00040047, 0x00000009,
    0x0000001e, 0x00000000, 0x00040047, 0x0000000d, 0x0000001b, 0x00000000,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
    0x00000006, 0x00000020, 0x00040020, 0x00000007, 0x00000006, 0x00000006,
    0x0004003b, 0x00000007, 0x00000009, 0x00000006, 0x00040020, 0x0000000e,
    0x00000007, 0x00000007, 0x0004003b, 0x0000000e, 0x00000022, 0x00000007,
    0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8,
    0x00000005, 0x0004003b, 0x00000007, 0x0000001b, 0x00000006, 0x0004003d,
    0x00000006, 0x0000001c, 0x0000001b, 0x00050051, 0x00000006, 0x0000001d,
    0x0000001c, 0x00000000, 0x00050051, 0x00000006, 0x0000001e, 0x0000001c,
    0x00000001, 0x00070050, 0x00000007, 0x0000001f, 0x0000001d, 0x0000001e,
    0x00000000, 0x00000000, 0x00050041, 0x0000000e, 0x00000020, 0x0000001f,
    0x00000022, 0x0003003e, 0x00000020, 0x00000020, 0x000100fd, 0x00010038,
};
/* clang-format on */

#define SPIRV_SIZE(x) (sizeof(x) / sizeof(uint32_t))

/* ---- Vulkan-specific structures ---- */
typedef struct vulkan_video_backend {
    const char *name;

    /* Instance / device */
    VkInstance                   instance;
    VkPhysicalDevice             physical_device;
    VkDevice                    device;
    uint32_t                     graphics_queue_family;
    uint32_t                     present_queue_family;
    VkQueue                      graphics_queue;
    VkQueue                      present_queue;

    /* Surface (from window) */
    VkSurfaceKHR                 surface;

    /* Swapchain */
    VkSwapchainKHR               swapchain;
    VkFormat                     swapchain_format;   /* VK_FORMAT_B8G8R8A8_UNORM */
    VkExtent2D                   swapchain_extent;
    uint32_t                     swapchain_image_count;
    VkImage                     *swapchain_images;
    VkImageView                 *swapchain_views;

    /* Upload pipeline (CPU pixels -> staging -> game_image) */
    VkDeviceMemory              upload_staging_mem;
    VkBuffer                    upload_staging_buf;
    VkDeviceSize                upload_staging_size;
    VkImage                     game_image;           /* game resolution */
    VkDeviceMemory              game_image_mem;
    VkImageView                 game_image_view;

    /* Sampler */
    VkSampler                    sampler;

    /* Descriptors */
    VkDescriptorSetLayout        descriptor_layout;
    VkDescriptorPool             descriptor_pool;
    VkDescriptorSet              descriptor_set;

    /* Render pass */
    VkRenderPass                 render_pass;

    /* Pipeline */
    VkPipelineLayout             pipeline_layout;
    VkPipeline                   pipeline;

    /* Command buffer */
    VkCommandPool                command_pool;
    VkCommandBuffer              command_buffer;

    /* Synchronisation */
    VkSemaphore                  image_acquired;
    VkSemaphore                  render_finished;

    /* In-flight tracking */
    uint32_t                     current_swapchain_index;
    bool                         frame_in_flight;

    /* Game resolution */
    uint32_t                     game_w;
    uint32_t                     game_h;

    /* Window resolution */
    uint32_t                     win_w;
    uint32_t                     win_h;

    /* Palette for 8bpp expansion */
    uint8_t                      palette[256 * 3];
    bool                         palette_dirty;

    /* Upload staging buffer (CPU pixels) */
    uint8_t                     *upload_cpu;
    size_t                       upload_cpu_capacity;

    bool                         initialized;
} vulkan_video_backend_t;

/* ---- Helpers ---- */
static const char *vk_strerror(VkResult res)
{
    switch (res) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    default: {
        static char buf[32];
        snprintf(buf, sizeof(buf), "VkResult=%d", (int)res);
        return buf;
    }}
}

#define VK_CHECK(x) do { \
    VkResult _r = (x); \
    if (_r != VK_SUCCESS) \
        fprintf(stderr, "vulkan: %s:%d: %s returned %s\n", \
                __FILE__, __LINE__, #x, vk_strerror(_r)); \
} while(0)

/* ---- Memory helpers ---- */
static uint32_t vulkan_find_memory_type(vulkan_video_backend_t *b,
                                         uint32_t type_filter,
                                         VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(b->physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0xFFFFFFFFu;  /* not found */
}

static bool vulkan_create_buffer(vulkan_video_backend_t *b,
                                  VkDeviceSize size,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags mem_props,
                                  VkBuffer *out_buf,
                                  VkDeviceMemory *out_mem)
{
    VkBufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(b->device, &ci, NULL, out_buf) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(b->device, *out_buf, &req);

    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = vulkan_find_memory_type(b, req.memoryTypeBits, mem_props),
    };
    if (vkAllocateMemory(b->device, &ai, NULL, out_mem) != VK_SUCCESS) {
        vkDestroyBuffer(b->device, *out_buf, NULL);
        *out_buf = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(b->device, *out_buf, *out_mem, 0);
    return true;
}

static void vulkan_destroy_buffer(vulkan_video_backend_t *b,
                                   VkBuffer buf, VkDeviceMemory mem)
{
    if (buf != VK_NULL_HANDLE)
        vkDestroyBuffer(b->device, buf, NULL);
    if (mem != VK_NULL_HANDLE)
        vkFreeMemory(b->device, mem, NULL);
}

static bool vulkan_create_image(vulkan_video_backend_t *b,
                                 uint32_t w, uint32_t h,
                                 VkFormat format,
                                 VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkImage *out_img,
                                 VkDeviceMemory *out_mem)
{
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = { w, h, 1 },
        .format = format,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(b->device, &ci, NULL, out_img) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(b->device, *out_img, &req);

    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = vulkan_find_memory_type(b, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    if (vkAllocateMemory(b->device, &ai, NULL, out_mem) != VK_SUCCESS) {
        vkDestroyImage(b->device, *out_img, NULL);
        *out_img = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(b->device, *out_img, *out_mem, 0);
    return true;
}

/* ---- Shader module creation from embedded SPIR-V ---- */
static VkShaderModule vulkan_create_shader_module(vulkan_video_backend_t *b,
                                                   const uint32_t *spv,
                                                   size_t word_count)
{
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = word_count * sizeof(uint32_t),
        .pCode = spv,
    };
    VkShaderModule mod;
    if (vkCreateShaderModule(b->device, &ci, NULL, &mod) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateShaderModule failed\n");
        return VK_NULL_HANDLE;
    }
    return mod;
}

/* ---- Swapchain recreation (on resize) ---- */
static bool vulkan_create_swapchain(vulkan_video_backend_t *b);
static void vulkan_destroy_swapchain(vulkan_video_backend_t *b);

static bool vulkan_recreate_swapchain(vulkan_video_backend_t *b)
{
    vkDeviceWaitIdle(b->device);
    vulkan_destroy_swapchain(b);
    return vulkan_create_swapchain(b);
}

/* ---- Pipeline build ---- */
static bool vulkan_build_pipeline(vulkan_video_backend_t *b)
{
    VkShaderModule vs = vulkan_create_shader_module(b, spv_vert, SPIRV_SIZE(spv_vert));
    VkShaderModule fs = vulkan_create_shader_module(b, spv_frag, SPIRV_SIZE(spv_frag));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs,
            .pName = "main",
        },
    };

    /* Vertex input: only position and texcoord builtins, no attributes */
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL,
    };

    /* Input assembly: triangle strip (4 vertices = fullscreen) */
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .primitiveRestartEnable = false,
    };

    /* Viewport: dynamic, set at draw time */
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    /* Rasterizer */
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    /* Multisampling (none) */
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = false,
    };

    /* Color blend: premultiplied alpha, no blending needed */
    VkPipelineColorBlendAttachmentState blend = {
        .blendEnable = false,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                        | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT
                        | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend,
    };

    /* Dynamic state: viewport + scissor */
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };

    /* Descriptor set: one sampler (game texture) */
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsl_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(b->device, &dsl_ci, NULL, &b->descriptor_layout)
            != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateDescriptorSetLayout failed\n");
        vkDestroyShaderModule(b->device, vs, NULL);
        vkDestroyShaderModule(b->device, fs, NULL);
        return false;
    }

    /* Descriptor pool */
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo dp_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };
    if (vkCreateDescriptorPool(b->device, &dp_ci, NULL, &b->descriptor_pool)
            != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateDescriptorPool failed\n");
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);
        vkDestroyShaderModule(b->device, vs, NULL);
        vkDestroyShaderModule(b->device, fs, NULL);
        return false;
    }

    /* Allocate descriptor set */
    VkDescriptorSetAllocateInfo ds_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = b->descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &b->descriptor_layout,
    };
    if (vkAllocateDescriptorSets(b->device, &ds_ci, &b->descriptor_set)
            != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkAllocateDescriptorSets failed\n");
        vkDestroyDescriptorPool(b->device, b->descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);
        vkDestroyShaderModule(b->device, vs, NULL);
        vkDestroyShaderModule(b->device, fs, NULL);
        return false;
    }

    /* Update descriptor set with game image sampler */
    VkDescriptorImageInfo img_info = {
        .sampler = b->sampler,
        .imageView = b->game_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = b->descriptor_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &img_info,
    };
    vkUpdateDescriptorSets(b->device, 1, &write, 0, NULL);

    /* Pipeline layout */
    VkPipelineLayoutCreateInfo pl_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &b->descriptor_layout,
    };
    if (vkCreatePipelineLayout(b->device, &pl_ci, NULL, &b->pipeline_layout)
            != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreatePipelineLayout failed\n");
        vkDestroyDescriptorPool(b->device, b->descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);
        vkDestroyShaderModule(b->device, vs, NULL);
        vkDestroyShaderModule(b->device, fs, NULL);
        return false;
    }

    /* Render pass */
    VkAttachmentDescription color_att = {
        .format = b->swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };
    VkRenderPassCreateInfo rp_ci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_att,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    if (vkCreateRenderPass(b->device, &rp_ci, NULL, &b->render_pass)
            != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateRenderPass failed\n");
        vkDestroyPipelineLayout(b->device, b->pipeline_layout, NULL);
        vkDestroyDescriptorPool(b->device, b->descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);
        vkDestroyShaderModule(b->device, vs, NULL);
        vkDestroyShaderModule(b->device, fs, NULL);
        return false;
    }

    /* Finally, create the graphics pipeline */
    VkGraphicsPipelineCreateInfo pipe_ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = b->pipeline_layout,
        .renderPass = b->render_pass,
        .subpass = 0,
    };
    if (vkCreateGraphicsPipelines(b->device, VK_NULL_HANDLE, 1, &pipe_ci, NULL,
                                   &b->pipeline) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateGraphicsPipelines failed\n");
        vkDestroyRenderPass(b->device, b->render_pass, NULL);
        vkDestroyPipelineLayout(b->device, b->pipeline_layout, NULL);
        vkDestroyDescriptorPool(b->device, b->descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);
        vkDestroyShaderModule(b->device, vs, NULL);
        vkDestroyShaderModule(b->device, fs, NULL);
        return false;
    }

    vkDestroyShaderModule(b->device, vs, NULL);
    vkDestroyShaderModule(b->device, fs, NULL);
    return true;
}

/* ---- Swapchain ---- */
static bool vulkan_create_swapchain(vulkan_video_backend_t *b)
{
    /* Query surface capabilities */
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(b->physical_device, b->surface, &caps);

    /* Choose surface format */
    uint32_t fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(b->physical_device, b->surface, &fmt_count, NULL);
    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)
        malloc(sizeof(VkSurfaceFormatKHR) * fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(b->physical_device, b->surface, &fmt_count, formats);

    /* Prefer B8G8R8A8_UNORM or fall back to first available */
    b->swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
    for (uint32_t i = 0; i < fmt_count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            b->swapchain_format = formats[i].format;
            break;
        }
    }
    free(formats);

    /* Extent: use window size, clamped to surface capabilities */
    b->swapchain_extent.width  = caps.currentExtent.width;
    b->swapchain_extent.height = caps.currentExtent.height;
    if (b->swapchain_extent.width == 0xFFFFFFFFu) {
        b->swapchain_extent.width  = b->win_w;
        b->swapchain_extent.height = b->win_h;
    }
    if (caps.minImageExtent.width > b->swapchain_extent.width)
        b->swapchain_extent.width = caps.minImageExtent.width;
    if (caps.minImageExtent.height > b->swapchain_extent.height)
        b->swapchain_extent.height = caps.minImageExtent.height;

    /* Image count */
    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;

    /* Composite alpha */
    VkCompositeAlphaFlagBitsKHR alpha =
        (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = b->surface,
        .minImageCount = img_count,
        .imageFormat = b->swapchain_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = b->swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,  /* vsync */
        .clipped = true,
    };

    if (vkCreateSwapchainKHR(b->device, &ci, NULL, &b->swapchain) != VK_SUCCESS)
        return false;

    /* Get swapchain images */
    vkGetSwapchainImagesKHR(b->device, b->swapchain, &b->swapchain_image_count, NULL);
    b->swapchain_images = (VkImage *)malloc(sizeof(VkImage) * b->swapchain_image_count);
    vkGetSwapchainImagesKHR(b->device, b->swapchain, &b->swapchain_image_count,
                             b->swapchain_images);

    /* Create image views */
    b->swapchain_views = (VkImageView *)malloc(sizeof(VkImageView) * b->swapchain_image_count);
    for (uint32_t i = 0; i < b->swapchain_image_count; i++) {
        VkImageViewCreateInfo vi_ci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = b->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = b->swapchain_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        vkCreateImageView(b->device, &vi_ci, NULL, &b->swapchain_views[i]);
    }
    return true;
}

static void vulkan_destroy_swapchain(vulkan_video_backend_t *b)
{
    if (b->swapchain_views) {
        for (uint32_t i = 0; i < b->swapchain_image_count; i++)
            if (b->swapchain_views[i] != VK_NULL_HANDLE)
                vkDestroyImageView(b->device, b->swapchain_views[i], NULL);
        free(b->swapchain_views);
        b->swapchain_views = NULL;
    }
    if (b->swapchain_images) {
        free(b->swapchain_images);
        b->swapchain_images = NULL;
    }
    if (b->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(b->device, b->swapchain, NULL);
        b->swapchain = VK_NULL_HANDLE;
    }
}

/* ---- Command buffer ---- */
static bool vulkan_create_command_buffer(vulkan_video_backend_t *b)
{
    VkCommandPoolCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = b->graphics_queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    if (vkCreateCommandPool(b->device, &ci, NULL, &b->command_pool)
            != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = b->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(b->device, &ai, &b->command_buffer)
            != VK_SUCCESS)
        return false;
    return true;
}

/* Record the render pass: blit game_image -> swapchain image */
static void vulkan_record_command_buffer(vulkan_video_backend_t *b,
                                           uint32_t swapchain_idx)
{
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(b->command_buffer, &begin);

    /* Barrier: game_image becomes SHADER_READ (previous upload dst) */
    VkImageMemoryBarrier2 img_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = b->game_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &img_barrier,
    };
    vkCmdPipelineBarrier2(b->command_buffer, &dep);

    /* Acquire swapchain image */
    VkImage swap_img = b->swapchain_images[swapchain_idx];

    /* Barrier: swapchain becomes TRANSFER_DST for blit */
    VkImageMemoryBarrier2 swap_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swap_img,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    /* Vulkan 1.3+: vkCmdPipelineBarrier2 takes VkDependencyInfo */
    VkDependencyInfo swap_dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &swap_barrier,
    };
    vkCmdPipelineBarrier2(b->command_buffer, &swap_dep);

    /* Blit: game_image -> swapchain (with scaling, nearest filter) */
    VkImageBlit blit = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1,
        },
        .srcOffsets = {
            { 0, 0, 0 },
            { (int32_t)b->game_w, (int32_t)b->game_h, 1 },
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1,
        },
        .dstOffsets = {
            { 0, 0, 0 },
            { (int32_t)b->swapchain_extent.width,
              (int32_t)b->swapchain_extent.height, 1 },
        },
    };
    vkCmdBlitImage(b->command_buffer,
                   b->game_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   swap_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    /* Barrier: swapchain becomes PRESENT_SRC */
    VkImageMemoryBarrier2 present_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swap_img,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    /* Vulkan 1.3+: vkCmdPipelineBarrier2 takes VkDependencyInfo */
    VkDependencyInfo present_dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &present_barrier,
    };
    vkCmdPipelineBarrier2(b->command_buffer, &present_dep);

    vkEndCommandBuffer(b->command_buffer);
}

/* ---- Backend v-table ---- */

static bool vulkan_video_create(void *user, void *context)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    (void)context;
    return true;
}

static bool vulkan_video_init(void *user, uint32_t game_w, uint32_t game_h,
                              uint32_t win_w, uint32_t win_h)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    b->game_w = game_w;
    b->game_h = game_h;
    b->win_w  = win_w;
    b->win_h  = win_h;

    /* ---- Required instance extensions ---- */
    const char *required_extensions[] = {
        "VK_KHR_surface",
#if defined(_WIN32)
        "VK_KHR_win32_surface",
#elif defined(__linux__)
        "VK_KHR_xlib_surface",
#elif defined(__APPLE__)
        "VK_EXT_metal_surface",
#endif
    };

    /* ---- Create Vulkan instance ---- */
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "STUN Runner",
        .apiVersion = VK_API_VERSION_1_0,
    };
    VkInstanceCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = (uint32_t)(sizeof(required_extensions) / sizeof(const char *)),
        .ppEnabledExtensionNames = required_extensions,
    };

    if (vkCreateInstance(&ci, NULL, &b->instance) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateInstance failed\n");
        return false;
    }

    /* ---- Enumerate physical devices and pick first suitable one ---- */
    uint32_t dev_count = 0;
    vkEnumeratePhysicalDevices(b->instance, &dev_count, NULL);
    if (dev_count == 0) {
        fprintf(stderr, "vulkan: no physical devices found\n");
        vkDestroyInstance(b->instance, NULL);
        return false;
    }
    VkPhysicalDevice *devs = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * dev_count);
    vkEnumeratePhysicalDevices(b->instance, &dev_count, devs);
    b->physical_device = devs[0];  /* pick first */
    free(devs);

    /* ---- Get queue families ---- */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(b->physical_device, &qf_count, NULL);
    VkQueueFamilyProperties *qf_props = (VkQueueFamilyProperties *)
        malloc(sizeof(VkQueueFamilyProperties) * qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(b->physical_device, &qf_count, qf_props);

    b->graphics_queue_family = 0xFFFFFFFFu;
    b->present_queue_family = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            if (b->graphics_queue_family == 0xFFFFFFFFu)
                b->graphics_queue_family = i;
        }
        /* Check present support */
        VkBool32 supports_present = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(b->physical_device, i, b->surface, &supports_present);
        if (supports_present) {
            b->present_queue_family = i;
            break;
        }
    }
    free(qf_props);
    if (b->graphics_queue_family == 0xFFFFFFFFu) {
        fprintf(stderr, "vulkan: no graphics queue family\n");
        vkDestroyInstance(b->instance, NULL);
        return false;
    }
    if (b->present_queue_family == 0xFFFFFFFFu)
        b->present_queue_family = b->graphics_queue_family;

    /* ---- Create logical device ---- */
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo qci[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = b->graphics_queue_family,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        },
    };
    uint32_t qci_count = 1;
    if (b->present_queue_family != b->graphics_queue_family) {
        qci[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci[1].queueFamilyIndex = b->present_queue_family;
        qci[1].queueCount = 1;
        qci[1].pQueuePriorities = &queue_priority;
        qci_count = 2;
    }

    /* Request VK_KHR_swapchain extension */
    const char *device_exts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = qci_count,
        .pQueueCreateInfos = qci,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_exts,
    };
    if (vkCreateDevice(b->physical_device, &dci, NULL, &b->device) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateDevice failed\n");
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    vkGetDeviceQueue(b->device, b->graphics_queue_family, 0, &b->graphics_queue);
    if (b->present_queue_family != b->graphics_queue_family)
        vkGetDeviceQueue(b->device, b->present_queue_family, 0, &b->present_queue);
    else
        b->present_queue = b->graphics_queue;

    /* ---- Create game image (512×240 game frame) ---- */
    if (!vulkan_create_image(b, game_w, game_h,
                              VK_FORMAT_R8G8B8_UNORM,
                              VK_IMAGE_TILING_OPTIMAL,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              &b->game_image, &b->game_image_mem)) {
        fprintf(stderr, "vulkan: failed to create game image\n");
        vkDestroyDevice(b->device, NULL);
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    /* Create image view for game image */
    VkImageViewCreateInfo iv_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = b->game_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    if (vkCreateImageView(b->device, &iv_ci, NULL, &b->game_image_view)
            != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateImageView failed\n");
        vkDestroyImage(b->device, b->game_image, NULL);
        vkFreeMemory(b->device, b->game_image_mem, NULL);
        vkDestroyDevice(b->device, NULL);
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    /* ---- Create sampler (nearest-neighbor for crisp pixels) ---- */
    VkSamplerCreateInfo smp_ci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    if (vkCreateSampler(b->device, &smp_ci, NULL, &b->sampler) != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkCreateSampler failed\n");
        vkDestroyImageView(b->device, b->game_image_view, NULL);
        vkDestroyImage(b->device, b->game_image, NULL);
        vkFreeMemory(b->device, b->game_image_mem, NULL);
        vkDestroyDevice(b->device, NULL);
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    /* ---- Create swapchain ---- */
    if (!vulkan_create_swapchain(b)) {
        fprintf(stderr, "vulkan: vulkan_create_swapchain failed\n");
        vkDestroySampler(b->device, b->sampler, NULL);
        vkDestroyImageView(b->device, b->game_image_view, NULL);
        vkDestroyImage(b->device, b->game_image, NULL);
        vkFreeMemory(b->device, b->game_image_mem, NULL);
        vkDestroyDevice(b->device, NULL);
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    /* ---- Build render pipeline ---- */
    if (!vulkan_build_pipeline(b)) {
        fprintf(stderr, "vulkan: vulkan_build_pipeline failed\n");
        vulkan_destroy_swapchain(b);
        vkDestroySampler(b->device, b->sampler, NULL);
        vkDestroyImageView(b->device, b->game_image_view, NULL);
        vkDestroyImage(b->device, b->game_image, NULL);
        vkFreeMemory(b->device, b->game_image_mem, NULL);
        vkDestroyDevice(b->device, NULL);
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    /* ---- Create command buffer ---- */
    if (!vulkan_create_command_buffer(b)) {
        fprintf(stderr, "vulkan: vulkan_create_command_buffer failed\n");
        vkDestroyPipeline(b->device, b->pipeline, NULL);
        vkDestroyRenderPass(b->device, b->render_pass, NULL);
        vkDestroyPipelineLayout(b->device, b->pipeline_layout, NULL);
        vkDestroyDescriptorPool(b->device, b->descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);
        vulkan_destroy_swapchain(b);
        vkDestroySampler(b->device, b->sampler, NULL);
        vkDestroyImageView(b->device, b->game_image_view, NULL);
        vkDestroyImage(b->device, b->game_image, NULL);
        vkFreeMemory(b->device, b->game_image_mem, NULL);
        vkDestroyDevice(b->device, NULL);
        vkDestroyInstance(b->instance, NULL);
        return false;
    }

    /* ---- Create synchronisation primitives ---- */
    VkSemaphoreCreateInfo sem_ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    vkCreateSemaphore(b->device, &sem_ci, NULL, &b->image_acquired);
    vkCreateSemaphore(b->device, &sem_ci, NULL, &b->render_finished);

    /* ---- Allocate CPU upload staging buffer ---- */
    b->upload_staging_size = (VkDeviceSize)game_w * game_h * 3;
    if (!vulkan_create_buffer(b, b->upload_staging_size,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &b->upload_staging_buf,
                               &b->upload_staging_mem)) {
        fprintf(stderr, "vulkan: upload staging buffer allocation failed\n");
        /* Non-fatal: continue without staging, uploads will fail gracefully */
    }

    /* CPU-side upload buffer */
    b->upload_cpu = (uint8_t *)malloc(b->upload_staging_size);
    b->upload_cpu_capacity = b->upload_staging_size;

    b->initialized = true;
    return true;
}

static void vulkan_video_shutdown(void *user)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    if (!b->initialized)
        return;

    if (b->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(b->device);

    if (b->image_acquired != VK_NULL_HANDLE)
        vkDestroySemaphore(b->device, b->image_acquired, NULL);
    if (b->render_finished != VK_NULL_HANDLE)
        vkDestroySemaphore(b->device, b->render_finished, NULL);
    if (b->command_buffer != VK_NULL_HANDLE)
        vkFreeCommandBuffers(b->device, b->command_pool, 1, &b->command_buffer);
    if (b->command_pool != VK_NULL_HANDLE)
        vkDestroyCommandPool(b->device, b->command_pool, NULL);
    if (b->pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(b->device, b->pipeline, NULL);
    if (b->render_pass != VK_NULL_HANDLE)
        vkDestroyRenderPass(b->device, b->render_pass, NULL);
    if (b->pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(b->device, b->pipeline_layout, NULL);
    if (b->descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(b->device, b->descriptor_pool, NULL);
    if (b->descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(b->device, b->descriptor_layout, NULL);

    vulkan_destroy_swapchain(b);

    if (b->sampler != VK_NULL_HANDLE)
        vkDestroySampler(b->device, b->sampler, NULL);
    if (b->game_image_view != VK_NULL_HANDLE)
        vkDestroyImageView(b->device, b->game_image_view, NULL);
    if (b->game_image != VK_NULL_HANDLE)
        vkDestroyImage(b->device, b->game_image, NULL);
    if (b->game_image_mem != VK_NULL_HANDLE)
        vkFreeMemory(b->device, b->game_image_mem, NULL);

    vulkan_destroy_buffer(b, b->upload_staging_buf, b->upload_staging_mem);
    free(b->upload_cpu);

    if (b->device != VK_NULL_HANDLE)
        vkDestroyDevice(b->device, NULL);
    if (b->instance != VK_NULL_HANDLE)
        vkDestroyInstance(b->instance, NULL);

    b->initialized = false;
}

static void vulkan_video_destroy(void *user)
{
    (void)user;
}

static int vulkan_video_upload(void *user,
                                const stunrun_video_frame_t *frame)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    if (!b->initialized || !frame || !frame->pixels)
        return 0;

    /* ---- Expand paletted to RGB888 in CPU staging buffer ---- */
    size_t needed = (size_t)frame->width * (size_t)frame->height * 3;
    const uint8_t *src_pixels = frame->pixels;

    if (frame->format == STUNRUN_VIDEO_FORMAT_PALETTED_8BPP) {
        if (needed > b->upload_cpu_capacity) {
            free(b->upload_cpu);
            b->upload_cpu = (uint8_t *)malloc(needed);
            b->upload_cpu_capacity = needed;
        }
        /* Expand palette */
        for (uint32_t row = 0; row < frame->height; row++) {
            const uint8_t *src_row = frame->pixels + row * frame->stride;
            uint8_t *dst_row = b->upload_cpu + row * frame->width * 3;
            for (uint32_t col = 0; col < frame->width; col++) {
                uint8_t idx = src_row[col];
                dst_row[col * 3 + 0] = b->palette[idx * 3 + 0];
                dst_row[col * 3 + 1] = b->palette[idx * 3 + 1];
                dst_row[col * 3 + 2] = b->palette[idx * 3 + 2];
            }
        }
        src_pixels = b->upload_cpu;
    } else if (frame->format == STUNRUN_VIDEO_FORMAT_RGB565) {
        if (needed > b->upload_cpu_capacity) {
            free(b->upload_cpu);
            b->upload_cpu = (uint8_t *)malloc(needed);
            b->upload_cpu_capacity = needed;
        }
        for (size_t i = 0; i < (size_t)frame->width * frame->height; i++) {
            uint16_t rgb565 = ((const uint16_t *)frame->pixels)[i];
            uint8_t r = (rgb565 >> 11) & 0x1F;
            uint8_t g = (rgb565 >> 5)  & 0x3F;
            uint8_t bl = rgb565 & 0x1F;
            b->upload_cpu[i * 3 + 0] = (r * 255) / 31;
            b->upload_cpu[i * 3 + 1] = (g * 255) / 63;
            b->upload_cpu[i * 3 + 2] = (bl * 255) / 31;
        }
        src_pixels = b->upload_cpu;
    } else if (frame->format != STUNRUN_VIDEO_FORMAT_RGB888) {
        fprintf(stderr, "vulkan: unsupported upload format %d\n", (int)frame->format);
        return 0;
    }

    /* ---- Copy into staging buffer ---- */
    if (b->upload_staging_buf == VK_NULL_HANDLE)
        return 0;

    void *map;
    VkResult r = vkMapMemory(b->device, b->upload_staging_mem, 0,
                              b->upload_staging_size, 0, &map);
    if (r != VK_SUCCESS) {
        fprintf(stderr, "vulkan: vkMapMemory failed: %s\n", vk_strerror(r));
        return 0;
    }
    memcpy(map, src_pixels, needed);
    vkUnmapMemory(b->device, b->upload_staging_mem);

    /* ---- Submit a transfer command: staging -> game_image ---- */
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = b->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(b->device, &ai, &cmd) != VK_SUCCESS)
        return 0;

    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    /* Transition game_image from UNDEFINED to TRANSFER_DST */
    VkImageMemoryBarrier2 img_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = b->game_image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &img_barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep);

    /* Copy */
    VkBufferImageCopy copy = {
        .bufferOffset = 0,
        .bufferRowLength = frame->width * 3,
        .bufferImageHeight = frame->height,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { frame->width, frame->height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, b->upload_staging_buf, b->game_image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(b->graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(b->graphics_queue);

    vkFreeCommandBuffers(b->device, b->command_pool, 1, &cmd);
    return 1;
}

static int vulkan_video_set_palette(void *user,
                                     const stunrun_video_palette_t *pal)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    if (!pal || !pal->entries || pal->count != 256)
        return 0;
    memcpy(b->palette, pal->entries, sizeof(b->palette));
    b->palette_dirty = true;
    return 1;
}

static void vulkan_video_present(void *user)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    if (!b->initialized)
        return;

    /* ---- Acquire next swapchain image ---- */
    uint32_t img_idx;
    VkResult r = vkAcquireNextImageKHR(b->device, b->swapchain,
                                       UINT64_MAX,
                                       b->image_acquired,
                                       VK_NULL_HANDLE,
                                       &img_idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        vulkan_recreate_swapchain(b);
        return;
    }
    if (r != VK_SUCCESS)
        return;

    b->current_swapchain_index = img_idx;

    /* ---- Record command buffer (blit game_image -> swapchain) ---- */
    vulkan_record_command_buffer(b, img_idx);

    /* ---- Submit ---- */
    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    };
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &b->image_acquired,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &b->command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &b->render_finished,
    };
    vkQueueSubmit(b->graphics_queue, 1, &submit, VK_NULL_HANDLE);

    /* ---- Present ---- */
    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &b->render_finished,
        .swapchainCount = 1,
        .pSwapchains = &b->swapchain,
        .pImageIndices = &img_idx,
    };
    r = vkQueuePresentKHR(b->present_queue, &present);

    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        /* Recreate on next frame */
    }

    vkQueueWaitIdle(b->present_queue);
}

static void vulkan_video_on_resize(void *user, uint32_t win_w, uint32_t win_h)
{
    vulkan_video_backend_t *b = (vulkan_video_backend_t *)user;
    b->win_w = win_w;
    b->win_h = win_h;
    if (b->initialized)
        vulkan_recreate_swapchain(b);
}

/* ---- Registration ---- */
static stunrun_video_backend_t the_backend = {
    .name        = "vulkan",
    .create      = vulkan_video_create,
    .init        = vulkan_video_init,
    .shutdown    = vulkan_video_shutdown,
    .destroy     = vulkan_video_destroy,
    .upload      = vulkan_video_upload,
    .set_palette = vulkan_video_set_palette,
    .present     = vulkan_video_present,
    .on_resize   = vulkan_video_on_resize,
    .user        = NULL,
};

const stunrun_video_backend_t *stunrun_video_backend_vulkan(void)
{
    return &the_backend;
}

const stunrun_video_backend_t *stunrun_video_backend_vulkan_register(void)
{
    extern void stunrun_backend_register_video(const stunrun_video_backend_t *);
    stunrun_backend_register_video(&the_backend);
    return &the_backend;
}

static const stunrun_video_backend_t *
    (* const reg_fn)(void) = stunrun_video_backend_vulkan_register;
