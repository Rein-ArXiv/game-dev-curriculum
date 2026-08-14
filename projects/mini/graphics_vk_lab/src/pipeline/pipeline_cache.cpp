#include "pipeline/pipeline_cache.h"

#include "device/vk_context.h"
#include "scene/scene.h"   // PushConstants 레이아웃.

#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstddef>
#include <filesystem>
#include <string>

namespace vklab {

namespace {

#ifndef VKLAB_SHADER_DIR
#define VKLAB_SHADER_DIR "shaders"
#endif

std::string ShaderPath(const char* file) {
    return (std::filesystem::path(VKLAB_SHADER_DIR) / file).string();
}

}  // namespace

VkShaderModule PipelineCache::Load(VkContext& ctx, const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error(std::string("missing shader: ") + path);
    size_t sz = (size_t)f.tellg();
    if ((sz % 4) != 0) {
        throw std::runtime_error(std::string("invalid SPIR-V size: ") + path);
    }
    std::vector<uint32_t> code(sz / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(code.data()), (std::streamsize)sz);

    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = sz;
    ci.pCode    = code.data();

    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx.Device(), &ci, nullptr, &m) != VK_SUCCESS) {
        throw std::runtime_error(std::string("vkCreateShaderModule failed: ") + path);
    }
    return m;
}

VkPipeline PipelineCache::Build(VkContext& ctx, VkFormat colorFormat, VkFormat depthFormat,
                                  VkShaderModule vs, VkShaderModule fs) {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    struct VertexForLayout {
        float pos[3];
        float normal[3];
    };
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(VertexForLayout);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(VertexForLayout, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = offsetof(VertexForLayout, normal);

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.cullMode  = VK_CULL_MODE_BACK_BIT;
    // 메시는 CCW(OpenGL 관례)로 작성됐고 proj 에서 Y 를 뒤집으므로 framebuffer
    // winding 이 반전된다. front face 를 CW 로 둬야 보이는 면이 살아남는다.
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;

    // Dynamic Rendering — VkPipelineRenderingCreateInfo 의 pNext.
    // depth 테스트를 켰으므로 depthAttachmentFormat 도 명시해야 한다.
    VkPipelineRenderingCreateInfo rci{};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &colorFormat;
    rci.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.pNext = &rci;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &dynState;
    pci.layout              = layout_;

    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx.Device(), VK_NULL_HANDLE, 1, &pci, nullptr, &p) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }
    return p;
}

void PipelineCache::Init(VkContext& ctx, VkFormat colorFormat, VkFormat depthFormat,
                         VkDescriptorSetLayout setLayout) {
    // Per-frame UBO + instance SSBO 는 set 0, per-draw 데이터는 push constant.
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(PushConstants);   // mat4 model + vec4 color + uint mode.

    VkPipelineLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts    = &setLayout;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcr;
    vkCreatePipelineLayout(ctx.Device(), &lci, nullptr, &layout_);

    const std::string vsPath = ShaderPath("cube.vert.spv");
    const std::string fsLPath = ShaderPath("lambert.frag.spv");
    const std::string fsBPath = ShaderPath("blinn.frag.spv");
    auto vs = Load(ctx, vsPath.c_str());
    auto fsL = Load(ctx, fsLPath.c_str());
    auto fsB = Load(ctx, fsBPath.c_str());

    lambert_ = Build(ctx, colorFormat, depthFormat, vs, fsL);
    blinn_   = Build(ctx, colorFormat, depthFormat, vs, fsB);

    vkDestroyShaderModule(ctx.Device(), vs, nullptr);
    vkDestroyShaderModule(ctx.Device(), fsL, nullptr);
    vkDestroyShaderModule(ctx.Device(), fsB, nullptr);
}

void PipelineCache::Shutdown(VkContext& ctx) {
    if (lambert_)  vkDestroyPipeline(ctx.Device(), lambert_, nullptr);
    if (blinn_)    vkDestroyPipeline(ctx.Device(), blinn_,   nullptr);
    if (layout_)   vkDestroyPipelineLayout(ctx.Device(), layout_, nullptr);
    lambert_ = blinn_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE;
}

}  // namespace vklab
