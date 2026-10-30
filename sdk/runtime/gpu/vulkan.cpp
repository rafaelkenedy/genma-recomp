// Experimental native Vulkan backend. Correctness first: guest resolution and synchronous frame retirement.
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <map>
#include <memory>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include "app/app.h"
#include "core/diag.h"
#include "gpu/backend.h"
#include "gpu/shaders.h"
#include "gpu/texture.h"
#include "gpu/vertex.h"
#include "kernel/kernel.h"
extern char **environ;
namespace app {
Mode LinuxWindowSize();
}
namespace gpu {
namespace {
[[noreturn]] void Fail(const char *reason) {
    LOGE("vulkan: %s", reason);
    kernel::Shutdown(1);
    std::abort();
}
void Check(VkResult r, const char *what) {
    if (r != VK_SUCCESS) {
        LOGE("vulkan: %s (%d)", what, int(r));
        Fail("operation failed");
    }
}
uint32_t SS(const State &s, uint32_t k) {
    return s.simple[k / 4];
}
float Bits(uint32_t n) {
    return std::bit_cast<float>(n);
}
void Color(uint32_t n, float *o) {
    o[0] = float((n >> 16) & 255) / 255;
    o[1] = float((n >> 8) & 255) / 255;
    o[2] = float(n & 255) / 255;
    o[3] = float(n >> 24) / 255;
}
VkCompareOp Compare(uint32_t n) {
    return n >= 0x200 && n <= 0x207 ? VkCompareOp(n - 0x200) : VK_COMPARE_OP_ALWAYS;
}
VkStencilOp Stencil(uint32_t n) {
    switch (n) {
    case 0:
        return VK_STENCIL_OP_ZERO;
    case 0x1e01:
        return VK_STENCIL_OP_REPLACE;
    case 0x1e02:
        return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 0x1e03:
        return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 0x150a:
        return VK_STENCIL_OP_INVERT;
    case 0x8507:
        return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 0x8508:
        return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default:
        return VK_STENCIL_OP_KEEP;
    }
}
VkBlendFactor Blend(uint32_t n, bool alpha) {
    switch (n) {
    case 0:
        return VK_BLEND_FACTOR_ZERO;
    case 1:
        return VK_BLEND_FACTOR_ONE;
    case 0x300:
        return alpha ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_SRC_COLOR;
    case 0x301:
        return alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x302:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x303:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x304:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x305:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x306:
        return alpha ? VK_BLEND_FACTOR_DST_ALPHA : VK_BLEND_FACTOR_DST_COLOR;
    case 0x307:
        return alpha ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x308:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 0x8001:
    case 0x8003:
        return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002:
    case 0x8004:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    default:
        Fail("unsupported blend factor");
    }
}
VkBlendOp BlendOp(uint32_t n) {
    switch (n) {
    case 0x800a:
        return VK_BLEND_OP_SUBTRACT;
    case 0x800b:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 0x8007:
        return VK_BLEND_OP_MIN;
    case 0x8008:
        return VK_BLEND_OP_MAX;
    default:
        return VK_BLEND_OP_ADD;
    }
}
VkSamplerAddressMode Address(uint32_t n) {
    switch (n) {
    case 2:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3:
    case 5:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 4:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}
VkFormat FormatOf(Host h) {
    switch (h) {
    case Host::BGRA8:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Host::BC1:
        return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Host::BC2:
        return VK_FORMAT_BC2_UNORM_BLOCK;
    case Host::BC3:
        return VK_FORMAT_BC3_UNORM_BLOCK;
    case Host::R32F:
        return VK_FORMAT_R32_SFLOAT;
    case Host::R16Unorm:
        return VK_FORMAT_R16_UNORM;
    default:
        Fail("unsupported texture format");
    }
}
struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    VkImageView view{}, sample{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format{};
    uint32_t w = 0, h = 0, depth = 1, levels = 1, layers = 1;
    bool z = false, drawn = false;
    uint64_t epoch = ~0ull, signature = 0, used = 0;
    TextureDesc desc{};
};
VkImageAspectFlags Aspects(bool z) {
    return z ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
             : VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT);
}
class Vulkan final : public Backend {
    VkInstance instance_{};
    VkSurfaceKHR surface_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkQueue queue_{};
    uint32_t family_ = 0;
    VkPhysicalDeviceMemoryProperties memory_{};
    VkCommandPool pool_{};
    VkCommandBuffer cmd_{};
    bool recording_ = false;
    VkSwapchainKHR swap_{};
    VkExtent2D extent_{};
    VkFormat swap_format_{};
    std::vector<VkImage> swap_images_;
    VkSemaphore acquired_{};
    VkFence fence_{};
    VkBuffer arena_{};
    VkDeviceMemory arena_mem_{};
    uint8_t *mapped_{};
    VkDeviceSize used_ = 0;
    static constexpr VkDeviceSize kArena = 128ull << 20;
    VkDeviceSize alignment_ = 256;
    VkDescriptorSetLayout descriptors_{};
    VkDescriptorPool descriptor_pool_{};
    VkPipelineLayout pipeline_layout_{};
    std::map<uint64_t, std::unique_ptr<Image>> targets_, textures_;
    std::unique_ptr<Image> white_;
    std::vector<std::unique_ptr<Image>> transient_;
    std::map<uint64_t, VkSampler> samplers_;
    std::map<uint64_t, VkShaderModule> shaders_;
    std::map<std::vector<uint64_t>, VkPipeline> pipelines_;
    std::map<uint32_t, VkRenderPass> passes_;
    std::map<std::pair<VkImageView, VkImageView>, VkFramebuffer> framebuffers_;
    VkQueryPool queries_{};
    uint32_t query_next_ = 0, report_first_ = 0;
    uint64_t query_sum_ = 0, frame_ = 0, draws_ = 0;
    uint8_t gamma_[768]{};
    bool gamma_identity_ = true;
    uint32_t MemoryType(uint32_t allowed, VkMemoryPropertyFlags flags);
    void Begin();
    void Submit(VkSemaphore wait = VK_NULL_HANDLE);
    VkDeviceSize Upload(const void *, size_t, size_t alignment = 16);
    std::unique_ptr<Image> MakeImage(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, VkFormat, bool, bool);
    void DestroyImage(Image &);
    void Layout(Image &, VkImageLayout);
    void UploadImage(Image &, const TextureDesc &);
    void UploadDepth(Image &, const TextureDesc &);
    Image *Target(const Surface &, bool);
    Image *Texture(uint32_t, uint32_t);
    VkSampler Sampler(const State &, int, bool);
    VkRenderPass Pass(bool, bool);
    VkFramebuffer Framebuffer(Image *, Image *, VkRenderPass);
    VkShaderModule Shader(std::string, bool, uint64_t &);
    VkPipeline Pipeline(const State &, const Assembled &, VkRenderPass, bool, bool, uint64_t, uint64_t);
    void Constants(const State &, uint32_t, uint32_t, const TexInfo *, DrawConstants &);
    void Resize();
    void Read(Image &, std::vector<uint8_t> &);
    void Capture(Image &);

  public:
    ~Vulkan() override;
    bool Init(uint32_t, uint32_t) override;
    void Clear(const State &, uint32_t, const uint8_t *, uint32_t, uint32_t, float, uint32_t) override;
    void Draw(const State &, uint32_t, uint32_t, uint32_t, const uint8_t *) override;
    void Copy(const Surface &, const Surface &, uint32_t, uint32_t, uint32_t) override;
    void Present(const Surface &) override;
    void Sync(uint32_t, uint32_t) override;
    void SyncLate(uint32_t d) override {
        LOG_FIRST(1, "vulkan: synchronous development readback for NOOVERWRITE");
        Sync(d, 0);
    }
    void Flush() override { Submit(); }
    uint32_t Fence() override {
        Submit();
        return Backend::Fence();
    }
    void WaitResource(uint32_t) override { Submit(); }
    void SetGamma(const uint8_t *r) override;
    void ClearReport() override {
        report_first_ = query_next_;
        query_sum_ = 0;
    }
    void GetReport(uint32_t) override;
};
uint32_t Vulkan::MemoryType(uint32_t allowed, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memory_.memoryTypeCount; i++)
        if ((allowed & (1u << i)) && (memory_.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    Fail("no compatible memory type");
}
bool Vulkan::Init(uint32_t w, uint32_t h) {
    uint32_t n = 0;
    const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&n);
    if (!exts)
        Fail(SDL_GetError());
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Genma native Linux";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = n;
    ci.ppEnabledExtensionNames = exts;
    const char *layer = "VK_LAYER_KHRONOS_validation";
    if (std::getenv("XBE_VK_VALIDATION")) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = &layer;
    }
    Check(vkCreateInstance(&ci, nullptr, &instance_), "create instance");
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window *>(app::Window()), instance_, nullptr, &surface_))
        Fail(SDL_GetError());
    Check(vkEnumeratePhysicalDevices(instance_, &n, nullptr), "enumerate devices");
    std::vector<VkPhysicalDevice> devices(n);
    Check(vkEnumeratePhysicalDevices(instance_, &n, devices.data()), "enumerate devices");
    for (auto p : devices) {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(p, &count, nullptr);
        std::vector<VkQueueFamilyProperties> q(count);
        vkGetPhysicalDeviceQueueFamilyProperties(p, &count, q.data());
        for (uint32_t i = 0; i < count; i++) {
            VkBool32 present = 0;
            Check(vkGetPhysicalDeviceSurfaceSupportKHR(p, i, surface_, &present), "surface support");
            if (present && (q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                physical_ = p;
                family_ = i;
                break;
            }
        }
        if (physical_)
            break;
    }
    if (!physical_)
        Fail("no graphics/presentation device");
    VkPhysicalDeviceProperties prop{};
    vkGetPhysicalDeviceProperties(physical_, &prop);
    alignment_ = std::max<VkDeviceSize>(256, prop.limits.minUniformBufferOffsetAlignment);
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory_);
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical_, &features);
    if (!features.textureCompressionBC)
        Fail("BC textures unavailable");
    VkPhysicalDeviceFeatures enable{};
    enable.textureCompressionBC = VK_TRUE;
    enable.independentBlend = features.independentBlend;
    enable.fillModeNonSolid = features.fillModeNonSolid;
    enable.occlusionQueryPrecise = features.occlusionQueryPrecise;
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    const char *swap_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = &swap_ext;
    di.pEnabledFeatures = &enable;
    Check(vkCreateDevice(physical_, &di, nullptr, &device_), "create device");
    vkGetDeviceQueue(device_, family_, 0, &queue_);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = family_;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    Check(vkCreateCommandPool(device_, &pi, nullptr, &pool_), "command pool");
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool_;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(device_, &ca, &cmd_), "command buffer");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(vkCreateFence(device_, &fi, nullptr, &fence_), "fence");
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    Check(vkCreateSemaphore(device_, &si, nullptr, &acquired_), "semaphore");
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = kArena;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(vkCreateBuffer(device_, &bi, nullptr, &arena_), "upload buffer");
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, arena_, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(device_, &ai, nullptr, &arena_mem_), "upload memory");
    Check(vkBindBufferMemory(device_, arena_, arena_mem_, 0), "bind upload");
    void *data = nullptr;
    Check(vkMapMemory(device_, arena_mem_, 0, kArena, 0, &data), "map upload");
    mapped_ = static_cast<uint8_t *>(data);
    std::array<VkDescriptorSetLayoutBinding, 10> bindings{};
    for (uint32_t i = 0; i < 10; i++) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = i < 2   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                     : i < 6 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                             : VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[i].stageFlags =
            i < 2 ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 10;
    li.pBindings = bindings.data();
    Check(vkCreateDescriptorSetLayout(device_, &li, nullptr, &descriptors_), "descriptor layout");
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &descriptors_;
    Check(vkCreatePipelineLayout(device_, &pli, nullptr, &pipeline_layout_), "pipeline layout");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32768},
                                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 65536},
                                    {VK_DESCRIPTOR_TYPE_SAMPLER, 65536}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 16384;
    dpi.poolSizeCount = 3;
    dpi.pPoolSizes = sizes;
    Check(vkCreateDescriptorPool(device_, &dpi, nullptr, &descriptor_pool_), "descriptor pool");
    VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qp.queryType = VK_QUERY_TYPE_OCCLUSION;
    qp.queryCount = 8192;
    Check(vkCreateQueryPool(device_, &qp, nullptr, &queries_), "query pool");
    white_ = MakeImage(1, 1, 1, 1, 1, VK_FORMAT_B8G8R8A8_UNORM, false, false);
    Begin();
    Layout(*white_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    uint32_t white = ~0u;
    auto off = Upload(&white, 4);
    VkBufferImageCopy cp{};
    cp.bufferOffset = off;
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd_, arena_, white_->image, white_->layout, 1, &cp);
    Layout(*white_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    LOGR("vulkan: %s, guest %ux%u; experimental native-resolution renderer", prop.deviceName, w, h);
    LOGR("vulkan: HD texture replacements and settings menu are not implemented in this development backend");
    Resize();
    return true;
}
void Vulkan::Begin() {
    if (recording_)
        return;
    Check(vkResetCommandBuffer(cmd_, 0), "reset command buffer");
    VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(cmd_, &b), "begin command buffer");
    recording_ = true;
}
void Vulkan::Submit(VkSemaphore wait) {
    if (!recording_)
        return;
    Check(vkEndCommandBuffer(cmd_), "end command buffer");
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    s.commandBufferCount = 1;
    s.pCommandBuffers = &cmd_;
    if (wait) {
        s.waitSemaphoreCount = 1;
        s.pWaitSemaphores = &wait;
        s.pWaitDstStageMask = &stage;
    }
    Check(vkResetFences(device_, 1, &fence_), "reset fence");
    Check(vkQueueSubmit(queue_, 1, &s, fence_), "submit");
    Check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, 30000000000ull), "GPU wait");
    if (query_next_ > report_first_) {
        std::vector<uint64_t> results(query_next_ - report_first_);
        Check(vkGetQueryPoolResults(device_, queries_, report_first_, uint32_t(results.size()), results.size() * 8,
                                    results.data(), 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
              "occlusion results");
        for (auto n : results)
            query_sum_ += n;
    }
    query_next_ = report_first_ = 0;
    recording_ = false;
    used_ = 0;
    for (auto &image : transient_)
        DestroyImage(*image);
    transient_.clear();
    Check(vkResetDescriptorPool(device_, descriptor_pool_, 0), "reset descriptors");
}
VkDeviceSize Vulkan::Upload(const void *data, size_t n, size_t alignment) {
    VkDeviceSize off = (used_ + alignment - 1) & ~VkDeviceSize(alignment - 1);
    if (off + n > kArena)
        Fail("upload arena exhausted; frame exceeds 128 MiB");
    if (data)
        std::memcpy(mapped_ + off, data, n);
    used_ = off + n;
    return off;
}
std::unique_ptr<Image> Vulkan::MakeImage(uint32_t w, uint32_t h, uint32_t depth, uint32_t levels, uint32_t layers,
                                         VkFormat format, bool z, bool target) {
    auto t = std::make_unique<Image>();
    t->w = w;
    t->h = h;
    t->depth = depth;
    t->levels = levels;
    t->layers = layers;
    t->format = format;
    t->z = z;
    t->used = frame_;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(physical_, format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        Fail("image format cannot be sampled");
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.flags = layers == 6 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.imageType = depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, depth};
    ci.mipLevels = levels;
    ci.arrayLayers = layers;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (target)
        ci.usage |= z ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    Check(vkCreateImage(device_, &ci, nullptr, &t->image), "create image");
    VkMemoryRequirements r;
    vkGetImageMemoryRequirements(device_, t->image, &r);
    VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a.allocationSize = r.size;
    a.memoryTypeIndex = MemoryType(r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(device_, &a, nullptr, &t->memory), "image memory");
    Check(vkBindImageMemory(device_, t->image, t->memory, 0), "bind image");
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t->image;
    vi.viewType = layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE : depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {Aspects(z), 0, levels, 0, layers};
    Check(vkCreateImageView(device_, &vi, nullptr, &t->view), "image view");
    t->sample = t->view;
    if (z) {
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        Check(vkCreateImageView(device_, &vi, nullptr, &t->sample), "depth sample view");
    }
    return t;
}
void Vulkan::DestroyImage(Image &t) {
    if (t.sample != t.view)
        vkDestroyImageView(device_, t.sample, nullptr);
    vkDestroyImageView(device_, t.view, nullptr);
    vkDestroyImage(device_, t.image, nullptr);
    vkFreeMemory(device_, t.memory, nullptr);
}
void Vulkan::Layout(Image &t, VkImageLayout layout) {
    Begin();
    // Include a memory dependency even when layout stays unchanged (successive render passes).
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask =
        t.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = t.layout;
    b.newLayout = layout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {Aspects(t.z), 0, t.levels, 0, t.layers};
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
    t.layout = layout;
}
void Vulkan::UploadImage(Image &image, const TextureDesc &d) {
    const uint8_t *guest = static_cast<const uint8_t *>(kernel::HostPtr(d.data));
    Layout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    for (uint32_t face = 0; face < image.layers; face++)
        for (uint32_t level = 0; level < d.levels; level++) {
            std::vector<uint8_t> pixels;
            uint32_t pitch = 0;
            DecodeLevel(d, level, face, guest, nullptr, pixels, pitch);
            if (pixels.empty())
                Fail("texture decode failed");
            auto off = Upload(pixels.data(), pixels.size(), 16);
            VkBufferImageCopy c{};
            c.bufferOffset = off;
            c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, face, 1};
            c.imageExtent = {std::max(d.width >> level, 1u), std::max(d.height >> level, 1u),
                             std::max(d.depth >> level, 1u)};
            vkCmdCopyBufferToImage(cmd_, arena_, image.image, image.layout, 1, &c);
        }
    Layout(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
void Vulkan::UploadDepth(Image &image, const TextureDesc &d) {
    std::vector<uint8_t> decoded;
    uint32_t pitch = 0;
    const auto *guest = static_cast<const uint8_t *>(kernel::HostPtr(d.data));
    DecodeLevel(d, 0, 0, guest, nullptr, decoded, pitch);
    if (decoded.size() != size_t(d.width) * d.height * 4)
        Fail("invalid depth decode");
    std::vector<uint32_t> depth(size_t(d.width) * d.height);
    std::vector<uint8_t> stencil(depth.size(), 0);
    bool has_stencil = Format(d.format).bits == 32;
    for (uint32_t y = 0; y < d.height; y++)
        for (uint32_t x = 0; x < d.width; x++) {
            size_t i = size_t(y) * d.width + x;
            float value;
            std::memcpy(&value, decoded.data() + i * 4, 4);
            depth[i] = uint32_t(std::lround(std::clamp(value, 0.0f, 1.0f) * 16777215.0f));
            if (has_stencil) {
                size_t address = size_t(y) * d.pitch + x * 4;
                if (!d.linear) {
                    uint32_t index = 0, outbit = 1;
                    for (uint32_t bit = 1; bit < d.width || bit < d.height; bit <<= 1) {
                        if (bit < d.width) {
                            if (x & bit)
                                index |= outbit;
                            outbit <<= 1;
                        }
                        if (bit < d.height) {
                            if (y & bit)
                                index |= outbit;
                            outbit <<= 1;
                        }
                    }
                    address = size_t(index) * 4;
                }
                stencil[i] = guest[address];
            }
        }
    Layout(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
    copy.imageExtent = {d.width, d.height, 1};
    copy.bufferOffset = Upload(depth.data(), depth.size() * 4);
    vkCmdCopyBufferToImage(cmd_, arena_, image.image, image.layout, 1, &copy);
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    copy.bufferOffset = Upload(stencil.data(), stencil.size());
    vkCmdCopyBufferToImage(cmd_, arena_, image.image, image.layout, 1, &copy);
    image.signature = Hash(guest, TotalSize(d));
    image.epoch = DirtyEpoch(d.data);
    image.drawn = true;
    LOG_FIRST(4, "vulkan: CPU depth imported %08X, %ux%u", d.data, d.width, d.height);
}
Image *Vulkan::Target(const Surface &s, bool z) {
    if (!s.header)
        return nullptr;
    TextureDesc d;
    if (!DescribeSurface(s, d))
        Fail("invalid surface");
    uint64_t key = uint64_t(d.data) << 1 | uint64_t(z);
    auto &p = targets_[key];
    if (p && (p->w != d.width || p->h != d.height)) {
        Submit();
        for (auto [key, fb] : framebuffers_)
            vkDestroyFramebuffer(device_, fb, nullptr);
        framebuffers_.clear();
        DestroyImage(*p);
        p.reset();
    }
    if (!p) {
        p = MakeImage(d.width, d.height, 1, 1, 1, z ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_B8G8R8A8_UNORM, z, true);
        p->desc = d;
        if (z) {
            Layout(*p, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearDepthStencilValue v{1, 0};
            VkImageSubresourceRange r{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
            vkCmdClearDepthStencilImage(cmd_, p->image, p->layout, &v, 1, &r);
            p->signature = Hash(kernel::HostPtr(d.data), TotalSize(d));
            p->epoch = DirtyEpoch(d.data);
        }
    }
    p->used = frame_;
    if (z && p->signature != Hash(kernel::HostPtr(d.data), TotalSize(d)))
        UploadDepth(*p, d);
    if (!z) {
        uint64_t sig = Hash(kernel::HostPtr(d.data), TotalSize(d));
        if (sig != p->signature || p->epoch == ~0ull) {
            UploadImage(*p, d);
            p->signature = sig;
            p->epoch = DirtyEpoch(d.data);
            p->drawn = false;
        }
    }
    return p.get();
}
Image *Vulkan::Texture(uint32_t header, uint32_t palette) {
    if (!header)
        return white_.get();
    TextureDesc d;
    if (!Describe(header, d))
        Fail("invalid texture");
    auto it = targets_.find(uint64_t(d.data) << 1 | uint64_t(Format(d.format).depth));
    if (it != targets_.end() && it->second->w == d.width && it->second->h == d.height) {
        it->second->used = frame_;
        return it->second.get();
    }
    uint64_t signature = Hash(kernel::HostPtr(d.data), TotalSize(d));
    uint64_t key = Hash(&d, sizeof(d), signature);
    const uint32_t *colors = nullptr;
    if (palette) {
        uint32_t va = mem::VirtualFromPhysical(kernel::R32(palette + 4));
        colors = static_cast<const uint32_t *>(kernel::HostPtr(va));
        key = Hash(colors, 1024, key);
    }
    auto &p = textures_[key];
    if (p) {
        p->used = frame_;
        return p.get();
    }
    p = MakeImage(d.width, d.height, d.depth, d.levels, d.cube ? 6 : 1, FormatOf(Format(d.format).host), false, false);
    p->desc = d;
    if (!colors)
        UploadImage(*p, d);
    else {
        Layout(*p, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for (uint32_t face = 0; face < p->layers; face++)
            for (uint32_t level = 0; level < d.levels; level++) {
                std::vector<uint8_t> px;
                uint32_t pitch;
                DecodeLevel(d, level, face, static_cast<const uint8_t *>(kernel::HostPtr(d.data)), colors, px, pitch);
                VkBufferImageCopy c{};
                c.bufferOffset = Upload(px.data(), px.size(), 16);
                c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, face, 1};
                c.imageExtent = {std::max(d.width >> level, 1u), std::max(d.height >> level, 1u),
                                 std::max(d.depth >> level, 1u)};
                vkCmdCopyBufferToImage(cmd_, arena_, p->image, p->layout, 1, &c);
            }
        Layout(*p, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return p.get();
}
VkSampler Vulkan::Sampler(const State &s, int stage, bool shadow) {
    const auto &t = s.tss[size_t(stage)];
    std::array<uint32_t, 10> keydata = {t[kTssAddressU],  t[kTssAddressV],    t[kTssAddressW],   t[kTssMagFilter],
                                        t[kTssMinFilter], t[kTssMipFilter],   t[kTssMipLodBias], t[kTssMaxMipLevel],
                                        uint32_t(shadow), s.rs[kRsShadowFunc]};
    uint64_t key = Hash(keydata.data(), sizeof(keydata));
    auto it = samplers_.find(key);
    if (it != samplers_.end())
        return it->second;
    VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = t[kTssMagFilter] >= 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.minFilter = t[kTssMinFilter] >= 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = t[kTssMipFilter] >= 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = Address(t[kTssAddressU]);
    ci.addressModeV = Address(t[kTssAddressV]);
    ci.addressModeW = Address(t[kTssAddressW]);
    ci.minLod = float(t[kTssMaxMipLevel]);
    ci.maxLod = t[kTssMipFilter] ? 32.0f : ci.minLod;
    ci.mipLodBias = std::clamp(Bits(t[kTssMipLodBias]), -2.0f, 2.0f);
    ci.compareEnable = shadow;
    ci.compareOp = Compare(s.rs[kRsShadowFunc]);
    ci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler;
    Check(vkCreateSampler(device_, &ci, nullptr, &sampler), "sampler");
    samplers_[key] = sampler;
    return sampler;
}
VkRenderPass Vulkan::Pass(bool color, bool depth) {
    uint32_t key = uint32_t(color) | uint32_t(depth) << 1;
    auto it = passes_.find(key);
    if (it != passes_.end())
        return it->second;
    VkAttachmentDescription a[2]{};
    uint32_t n = 0;
    VkAttachmentReference cr{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        dr{VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (color) {
        cr.attachment = n;
        a[n].format = VK_FORMAT_B8G8R8A8_UNORM;
        a[n].samples = VK_SAMPLE_COUNT_1_BIT;
        a[n].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a[n].initialLayout = a[n].finalLayout = cr.layout;
        n++;
    }
    if (depth) {
        dr.attachment = n;
        a[n].format = VK_FORMAT_D24_UNORM_S8_UINT;
        a[n].samples = VK_SAMPLE_COUNT_1_BIT;
        a[n].loadOp = a[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        a[n].storeOp = a[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        a[n].initialLayout = a[n].finalLayout = dr.layout;
        n++;
    }
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = color ? 1 : 0;
    sub.pColorAttachments = color ? &cr : nullptr;
    sub.pDepthStencilAttachment = depth ? &dr : nullptr;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = n;
    ci.pAttachments = a;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    VkRenderPass pass;
    Check(vkCreateRenderPass(device_, &ci, nullptr, &pass), "render pass");
    passes_[key] = pass;
    return pass;
}
VkFramebuffer Vulkan::Framebuffer(Image *color, Image *depth, VkRenderPass pass) {
    auto key = std::make_pair(color ? color->view : VK_NULL_HANDLE, depth ? depth->view : VK_NULL_HANDLE);
    auto it = framebuffers_.find(key);
    if (it != framebuffers_.end())
        return it->second;
    VkImageView views[2];
    uint32_t n = 0;
    if (color)
        views[n++] = color->view;
    if (depth)
        views[n++] = depth->view;
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass = pass;
    ci.attachmentCount = n;
    ci.pAttachments = views;
    ci.width = color ? color->w : depth->w;
    ci.height = color ? color->h : depth->h;
    ci.layers = 1;
    if (depth) {
        ci.width = std::min(ci.width, depth->w);
        ci.height = std::min(ci.height, depth->h);
    }
    VkFramebuffer fb;
    Check(vkCreateFramebuffer(device_, &ci, nullptr, &fb), "framebuffer");
    framebuffers_[key] = fb;
    return fb;
}

VkShaderModule Vulkan::Shader(std::string source, bool vertex, uint64_t &key) {
    auto replace = [&](const std::string &from, const std::string &to) {
        size_t at = 0;
        while ((at = source.find(from, at)) != std::string::npos) {
            source.replace(at, from.size(), to);
            at += to.size();
        }
    };
    // Preserve semantic locations across independently compiled stages, including unused varyings.
    const char *names[] = {"d0 : COLOR0",     "d1 : COLOR1",     "b0 : COLOR2",
                           "b1 : COLOR3",     "tc0 : TEXCOORD0", "tc1 : TEXCOORD1",
                           "tc2 : TEXCOORD2", "tc3 : TEXCOORD3", "fogamt : TEXCOORD4"};
    for (int i = 0; i < 9; i++) {
        std::string decl = (i == 8 ? "float " : "float4 ") + std::string(names[i]);
        replace(decl, "[[vk::location(" + std::to_string(i) + ")]] " + decl);
    }
    if (vertex)
        for (int i = 0; i < 16; i++) {
            std::string decl = "float4 v" + std::to_string(i) + " : ATTR" + std::to_string(i);
            replace(decl, "[[vk::location(" + std::to_string(i) + ")]] " + decl);
        }
    if (vertex) {
        replace("float4 pos : SV_Position;",
                "float4 pos : SV_Position; [[vk::builtin(\"PointSize\")]] float pointsize : PSIZE;");
        replace("o.pos = q;", "o.pos = q; o.pointsize = 1;");
        replace("o.pos = cp;", "o.pos = cp; o.pointsize = 1;");
    }
    replace("1.#INF", "asfloat(0x7f800000u)");
    key = Hash(source.data(), source.size(), vertex ? 0x564b565330303031ull : 0x564b505330303031ull);
    auto it = shaders_.find(key);
    if (it != shaders_.end())
        return it->second;
    auto dir = app::CacheDir() / "shader-cache-vulkan";
    std::filesystem::create_directories(dir);
    std::string name = std::to_string(key);
    auto hlsl = dir / (name + ".hlsl"), spv = dir / (name + ".spv"), log = dir / (name + ".log");
    if (!std::filesystem::exists(spv)) {
        {
            std::ofstream f(hlsl);
            f << source;
            if (!f)
                Fail("cannot write shader source");
        }
        const char *tool = std::getenv("XBE_DXC");
        std::vector<std::string> args = {tool ? tool : "dxc",
                                         "-spirv",
                                         "-fspv-target-env=vulkan1.1",
                                         "-fvk-use-dx-layout",
                                         "-HV",
                                         "2016",
                                         "-T",
                                         vertex ? "vs_6_0" : "ps_6_0",
                                         "-E",
                                         "main",
                                         "-fvk-t-shift",
                                         "2",
                                         "0",
                                         "-fvk-s-shift",
                                         "6",
                                         "0",
                                         "-Fo",
                                         spv.string(),
                                         hlsl.string()};
        if (vertex)
            args.push_back("-fvk-invert-y");
        std::vector<char *> argv;
        for (auto &a : args)
            argv.push_back(a.data());
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        pid_t child = 0;
        int error = posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        int status = 0;
        if (!error) {
            while (waitpid(child, &status, 0) < 0) {
                if (errno != EINTR)
                    Fail("wait for DXC");
            }
        }
        if (error || !WIFEXITED(status) || WEXITSTATUS(status)) {
            std::ifstream f(log);
            std::string text((std::istreambuf_iterator<char>(f)), {});
            LOGE("DXC: %s", text.c_str());
            Fail("shader compilation failed (see shader-cache-vulkan)");
        }
        LOGI("vulkan: compiled %s shader %llu", vertex ? "vertex" : "pixel", static_cast<unsigned long long>(key));
    }
    std::ifstream f(spv, std::ios::binary | std::ios::ate);
    auto size = f.tellg();
    if (size <= 0 || size % 4)
        Fail("invalid SPIR-V file");
    std::vector<uint32_t> words(size_t(size) / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char *>(words.data()), size);
    if (!f)
        Fail("cannot read SPIR-V");
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = words.size() * 4;
    ci.pCode = words.data();
    VkShaderModule module;
    Check(vkCreateShaderModule(device_, &ci, nullptr, &module), "shader module");
    shaders_[key] = module;
    return module;
}

VkPipeline Vulkan::Pipeline(const State &s, const Assembled &a, VkRenderPass pass, bool color, bool depth, uint64_t vs,
                            uint64_t ps) {
    std::vector<uint64_t> key = {
        vs, ps, a.regs, uint64_t(a.topology), uint64_t(a.strip), uint64_t(color), uint64_t(depth)};
    for (auto n : {kRsZEnable, kRsStencilEnable, kRsStencilFail, kRsCullMode, kRsFillMode})
        key.push_back(s.rs[n]);
    for (auto n : {kMBlendEnable, kMSrcBlend, kMDestBlend, kMBlendOp, kMColorWriteEnable, kMZWriteEnable, kMZFunc,
                   kMStencilWriteMask, kMStencilFunc, kMStencilMask, kMStencilZFail, kMStencilPass, kMSolidOffsetEnable,
                   kMSlopeScale, kMDepthOffset})
        key.push_back(SS(s, n));
    auto it = pipelines_.find(key);
    if (it != pipelines_.end())
        return it->second;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shaders_.at(vs);
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shaders_.at(ps);
    stages[1].pName = "main";
    std::vector<VkVertexInputAttributeDescription> attrs;
    for (uint32_t r = 0; r < 16; r++)
        if (a.regs & (1u << r))
            attrs.push_back({r, 0, VK_FORMAT_R32G32B32A32_SFLOAT, a.elements[r].offset});
    VkVertexInputBindingDescription binding{0, a.buffers[0].stride, VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = a.topology == Topology::Points ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                  : a.topology == Topology::Lines
                      ? (a.strip ? VK_PRIMITIVE_TOPOLOGY_LINE_STRIP : VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                      : (a.strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    rs.cullMode = s.rs[kRsCullMode] && a.topology == Topology::Triangles ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = s.rs[kRsCullMode] == 0x900 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    if (s.rs[kRsFillMode] == 0x1b01)
        Fail("wireframe is not implemented in development backend");
    rs.depthBiasEnable = SS(s, kMSolidOffsetEnable) != 0;
    rs.depthBiasConstantFactor = std::round(Bits(SS(s, kMDepthOffset)) * 16777215.0f / s.zscale);
    rs.depthBiasSlopeFactor = Bits(SS(s, kMSlopeScale));
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depth && s.rs[kRsZEnable];
    ds.depthWriteEnable = depth && SS(s, kMZWriteEnable);
    ds.depthCompareOp = Compare(SS(s, kMZFunc));
    ds.stencilTestEnable = depth && s.rs[kRsStencilEnable];
    ds.front = {Stencil(s.rs[kRsStencilFail]),
                Stencil(SS(s, kMStencilPass)),
                Stencil(SS(s, kMStencilZFail)),
                Compare(SS(s, kMStencilFunc)),
                SS(s, kMStencilMask) & 255,
                SS(s, kMStencilWriteMask) & 255,
                0};
    ds.back = ds.front;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = SS(s, kMBlendEnable);
    blend.srcColorBlendFactor = Blend(SS(s, kMSrcBlend), false);
    blend.dstColorBlendFactor = Blend(SS(s, kMDestBlend), false);
    blend.colorBlendOp = BlendOp(SS(s, kMBlendOp));
    blend.srcAlphaBlendFactor = Blend(SS(s, kMSrcBlend), true);
    blend.dstAlphaBlendFactor = Blend(SS(s, kMDestBlend), true);
    blend.alphaBlendOp = blend.colorBlendOp;
    uint32_t mask = SS(s, kMColorWriteEnable);
    blend.colorWriteMask = ((mask >> 16) & 1) | ((mask >> 8) & 1) << 1 | (mask & 1) << 2 | ((mask >> 24) & 1) << 3;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = color ? 1 : 0;
    cb.pAttachments = &blend;
    VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
                                VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 4;
    dy.pDynamicStates = dynamic;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = pipeline_layout_;
    ci.renderPass = pass;
    VkPipeline pipeline;
    Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline), "graphics pipeline");
    pipelines_[key] = pipeline;
    return pipeline;
}

void Multiply(const float *a, const float *b, float *o) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            o[r * 4 + c] = 0;
            for (int i = 0; i < 4; i++)
                o[r * 4 + c] += a[r * 4 + i] * b[i * 4 + c];
        }
}
void Transpose(const float *a, float *b) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            b[c * 4 + r] = a[r * 4 + c];
}
void Vulkan::Constants(const State &s, uint32_t w, uint32_t h, const TexInfo *info, DrawConstants &c) {
    c.surf[0] = float(w);
    c.surf[1] = float(h);
    c.surf[2] = 1 / s.zscale;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SS(s, kMAlphaRef) & 255);
    c.misc[1] = SS(s, kMAlphaTestEnable) ? float(Compare(SS(s, kMAlphaFunc))) : 7;
    c.misc[2] = float(Compare(s.rs[kRsShadowFunc]));
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1 : 0;
    c.dfix[0] = app::Get().precise_depth ? 1 : 0;
    bool program = (s.vs_handle & 1) && s.vs && !s.vs->program.empty();
    if (s.rs[kRsFogEnable]) {
        if (program && !s.rs[kRsFogTableMode]) {
            c.fog[0] = 1;
            c.fog[3] = 3;
        } else {
            c.fog[0] = Bits(s.rs[kRsFogStart]);
            c.fog[1] = Bits(s.rs[kRsFogEnd]);
            c.fog[2] = Bits(s.rs[kRsFogDensity]);
            c.fog[3] = float(s.rs[kRsFogTableMode]);
        }
    }
    for (int i = 0; i < 8; i++) {
        Color(SS(s, kMPsConstant0 + i * 4), c.pc[i * 2]);
        Color(SS(s, kMPsConstant1 + i * 4), c.pc[i * 2 + 1]);
    }
    Color(SS(s, kMPsFinalConstant0), c.pc[16]);
    Color(SS(s, kMPsFinalConstant1), c.pc[17]);
    Color(s.rs[kRsTextureFactor], c.tfactor);
    for (int i = 0; i < 4; i++) {
        if (info[i].flags & kTexTarget)
            c.rtshift[i] = (!program || ReadsViewportOffset(s.vs->program)) ? -0.5f : 0.0f;
        const auto &t = s.tss[size_t(i)];
        TextureDesc d;
        if (s.textures[size_t(i)] && Describe(s.textures[size_t(i)], d)) {
            c.texsize[i][0] = float(d.width);
            c.texsize[i][1] = float(d.height);
            if (app::Get().point_nudge && t[kTssMagFilter] < 2 && t[kTssMinFilter] < 2) {
                c.texsize[i][2] = 1.0f / (1024 * d.width);
                c.texsize[i][3] = 1.0f / (1024 * d.height);
            }
        }
        c.bump[i][0] = Bits(t[kTssBumpEnvMat00]);
        c.bump[i][1] = Bits(t[kTssBumpEnvMat01]);
        c.bump[i][2] = Bits(t[kTssBumpEnvMat10]);
        c.bump[i][3] = Bits(t[kTssBumpEnvMat11]);
        c.bumpl[i][0] = Bits(t[kTssBumpEnvLScale]);
        c.bumpl[i][1] = Bits(t[kTssBumpEnvLOffset]);
    }
    if (program)
        return;
    float wv[16], wvp[16], full[16];
    Multiply(s.transforms[256].data(), s.transforms[2].data(), wv);
    Multiply(wv, s.transforms[3].data(), wvp);
    const auto &v = s.viewport;
    float view[16] = {float(v.w) / w,
                      0,
                      0,
                      0,
                      0,
                      float(v.h) / h,
                      0,
                      0,
                      0,
                      0,
                      v.max_z - v.min_z,
                      0,
                      (2.0f * v.x + v.w) / w - 1,
                      1 - (2.0f * v.y + v.h) / h,
                      v.min_z,
                      1};
    Multiply(wvp, view, full);
    Transpose(full, c.wvp);
    Transpose(wv, c.wv);
    float det = wv[0] * (wv[5] * wv[10] - wv[6] * wv[9]) - wv[1] * (wv[4] * wv[10] - wv[6] * wv[8]) +
                wv[2] * (wv[4] * wv[9] - wv[5] * wv[8]);
    float normal[16]{};
    for (int r = 0; r < 3; r++)
        for (int col = 0; col < 3; col++) {
            int r1 = (r + 1) % 3, r2 = (r + 2) % 3, c1 = (col + 1) % 3, c2 = (col + 2) % 3;
            normal[r * 4 + col] =
                det ? (wv[r1 * 4 + c1] * wv[r2 * 4 + c2] - wv[r1 * 4 + c2] * wv[r2 * 4 + c1]) / det : 0;
        }
    Transpose(normal, c.wvn);
    for (int i = 0; i < 4; i++)
        Transpose(s.transforms[16 + size_t(i)].data(), c.tm[i]);
    std::memcpy(c.mat, s.material.data(), 64);
    c.mat[4][0] = s.material[16];
    Color(s.rs[kRsAmbient], c.amb);
    for (int i = 0; i < 8; i++)
        if (s.light_enable & (1u << i)) {
            float l[26];
            std::memcpy(l, s.lights[size_t(i)].data(), sizeof(l));
            auto &o = c.lt[i];
            std::memcpy(o[0], l + 1, 16);
            std::memcpy(o[1], l + 5, 16);
            std::memcpy(o[2], l + 9, 16);
            const float *vm = s.transforms[2].data();
            for (int j = 0; j < 3; j++) {
                o[3][j] = l[13] * vm[j] + l[14] * vm[4 + j] + l[15] * vm[8 + j] + vm[12 + j];
                o[4][j] = l[16] * vm[j] + l[17] * vm[4 + j] + l[18] * vm[8 + j];
            }
            o[3][3] = l[19];
            o[4][3] = l[20];
            o[5][0] = l[21];
            o[5][1] = l[22];
            o[5][2] = l[23];
            o[6][0] = std::cos(l[24] * 0.5f);
            o[6][1] = std::cos(l[25] * 0.5f);
        }
}

void Vulkan::Draw(const State &s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t *indices) {
    Assembled a;
    if (!Assemble(s, prim, start, count, indices, a))
        return;
    if (a.buffer_count != 1 || a.decode)
        Fail("Linux requires converted float4 vertex assembly");
    if (used_ > kArena - (16ull << 20) || query_next_ >= 8192)
        Submit();
    Begin();
    Image *rt = Target(s.rt, false);
    Image *ds = Target(s.zs, true);
    if (!rt && !ds)
        Fail("draw has no attachment");
    std::array<Image *, 4> images{};
    TexInfo info[4]{};
    for (int i = 0; i < 4; i++) {
        images[size_t(i)] = Texture(s.textures[size_t(i)], s.palettes[size_t(i)]);
        auto *t = images[size_t(i)];
        bool target = targets_.contains(uint64_t(t->desc.data) << 1 | uint64_t(t->z));
        if (s.textures[size_t(i)]) {
            info[i].kind = t->layers == 6 ? kTexCube : t->depth > 1 ? kTex3D : kTex2D;
            info[i].rect = t->desc.linear ? 1 : 0;
            info[i].shadow = t->z ? 1 : 0;
            info[i].flags = target ? kTexTarget : 0;
        }
        if (t == rt || t == ds) {
            auto snapshot = MakeImage(t->w, t->h, 1, 1, 1, t->format, t->z, false);
            Layout(*t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            Layout(*snapshot, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy copy{};
            copy.srcSubresource =
                copy.dstSubresource = {t->z ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.extent = {t->w, t->h, 1};
            vkCmdCopyImage(cmd_, t->image, t->layout, snapshot->image, snapshot->layout, 1, &copy);
            images[size_t(i)] = snapshot.get();
            transient_.push_back(std::move(snapshot));
            t = images[size_t(i)];
        }
        Layout(*t, t->z ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    bool program = (s.vs_handle & 1) && s.vs && !s.vs->program.empty();
    FixedKey fk = MakeFixedKey(s);
    uint64_t vs, ps;
    Shader(program ? VertexProgramHlsl(s.vs->program, a.regs) : FixedVertexHlsl(a.regs, a.pretransformed, 0, fk), true,
           vs);
    PixelKey pk = MakePixelKey(s, info);
    Shader(PixelHlsl(pk), false, ps);
    auto pass = Pass(rt != nullptr, ds != nullptr);
    auto pipeline = Pipeline(s, a, pass, rt != nullptr, ds != nullptr, vs, ps);
    uint32_t w = rt ? rt->w : ds->w, h = rt ? rt->h : ds->h;
    DrawConstants dc{};
    Constants(s, w, h, info, dc);
    if (a.topology == Topology::Points)
        dc.surf[3] = 1;
    VkDescriptorBufferInfo buffers[2] = {
        {arena_, Upload(s.vsc.data(), sizeof(s.vsc), size_t(alignment_)), sizeof(s.vsc)},
        {arena_, Upload(&dc, sizeof(dc), size_t(alignment_)), sizeof(dc)}};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descriptor_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &descriptors_;
    VkDescriptorSet set;
    Check(vkAllocateDescriptorSets(device_, &ai, &set), "draw descriptors");
    VkWriteDescriptorSet writes[10]{};
    VkDescriptorImageInfo im[8]{};
    for (uint32_t i = 0; i < 10; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 2   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : i < 6 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                           : VK_DESCRIPTOR_TYPE_SAMPLER;
        if (i < 2)
            writes[i].pBufferInfo = &buffers[i];
        else
            writes[i].pImageInfo = &im[i - 2];
    }
    for (int i = 0; i < 4; i++) {
        auto *t = images[size_t(i)];
        im[i].imageView = t->sample;
        im[i].imageLayout = t->layout;
        im[4 + i].sampler = Sampler(s, i, info[i].shadow != 0);
    }
    vkUpdateDescriptorSets(device_, 10, writes, 0, nullptr);
    auto vb = Upload(a.buffers[0].src, a.buffers[0].bytes);
    VkDeviceSize ib = 0;
    if (a.index_count)
        ib = Upload(a.index_src, size_t(a.index_count) * (a.index16 ? 2 : 4));
    if (rt)
        Layout(*rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (ds)
        Layout(*ds, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    uint32_t fw = ds ? std::min(w, ds->w) : w, fh = ds ? std::min(h, ds->h) : h;
    uint32_t q = ~0u;
    if (SS(s, kMCountEnable)) {
        q = query_next_++;
        vkCmdResetQueryPool(cmd_, queries_, q, 1);
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass;
    begin.framebuffer = Framebuffer(rt, ds, pass);
    begin.renderArea.extent = {fw, fh};
    vkCmdBeginRenderPass(cmd_, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &set, 0, nullptr);
    VkViewport viewport{0, 0, float(w), float(h), 0, 1};
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
    VkRect2D sc{{0, 0}, {fw, fh}};
    if (s.scissor_count) {
        if (s.scissor_count > 1)
            Fail("multiple scissors not yet implemented");
        int x0 = std::clamp(int(s.scissors[0]), 0, int(fw)), y0 = std::clamp(int(s.scissors[1]), 0, int(fh));
        int x1 = std::clamp(int(s.scissors[2]), x0, int(fw)), y1 = std::clamp(int(s.scissors[3]), y0, int(fh));
        sc = {{x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}};
    }
    int x0 = std::max(sc.offset.x, int(std::min(s.viewport.x, fw))),
        y0 = std::max(sc.offset.y, int(std::min(s.viewport.y, fh)));
    int x1 = std::min(sc.offset.x + int(sc.extent.width),
                      int(std::min(uint64_t(s.viewport.x) + s.viewport.w, uint64_t(fw))));
    int y1 = std::min(sc.offset.y + int(sc.extent.height),
                      int(std::min(uint64_t(s.viewport.y) + s.viewport.h, uint64_t(fh))));
    sc = {{x0, y0}, {uint32_t(std::max(x1 - x0, 0)), uint32_t(std::max(y1 - y0, 0))}};
    vkCmdSetScissor(cmd_, 0, 1, &sc);
    float blend[4];
    Color(SS(s, kMBlendColor), blend);
    vkCmdSetBlendConstants(cmd_, blend);
    vkCmdSetStencilReference(cmd_, VK_STENCIL_FACE_FRONT_AND_BACK, SS(s, kMStencilRef) & 255);
    vkCmdBindVertexBuffers(cmd_, 0, 1, &arena_, &vb);
    if (q != ~0u)
        vkCmdBeginQuery(cmd_, queries_, q, 0);
    if (a.index_count) {
        vkCmdBindIndexBuffer(cmd_, arena_, ib, a.index16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd_, a.index_count, 1, 0, a.base_vertex, 0);
    } else
        vkCmdDraw(cmd_, a.draw_count, 1, 0, 0);
    if (q != ~0u)
        vkCmdEndQuery(cmd_, queries_, q);
    vkCmdEndRenderPass(cmd_);
    if (rt)
        rt->drawn = true;
    if (ds)
        ds->drawn = true;
    draws_++;
}

void Vulkan::Clear(const State &s, uint32_t count, const uint8_t *rects, uint32_t flags, uint32_t color, float z,
                   uint32_t stencil) {
    if ((flags & kClearTarget) && (flags & kClearTarget) != kClearTarget)
        Fail("partial-channel clear not implemented");
    Image *rt = (flags & kClearTarget) ? Target(s.rt, false) : nullptr;
    Image *ds = (flags & (kClearZ | kClearStencil)) ? Target(s.zs, true) : nullptr;
    if (!rt && !ds)
        return;
    Begin();
    if (!count) {
        if (rt) {
            Layout(*rt, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue c;
            Color(color, c.float32);
            VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd_, rt->image, rt->layout, &c, 1, &r);
            rt->drawn = true;
        }
        if (ds) {
            Layout(*ds, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearDepthStencilValue c{z, stencil};
            VkImageAspectFlags aspects = ((flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                                         ((flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
            VkImageSubresourceRange r{aspects, 0, 1, 0, 1};
            vkCmdClearDepthStencilImage(cmd_, ds->image, ds->layout, &c, 1, &r);
            ds->drawn = true;
        }
        return;
    }
    if (rt)
        Layout(*rt, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (ds)
        Layout(*ds, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    uint32_t w = rt ? rt->w : ds->w, h = rt ? rt->h : ds->h;
    if (ds) {
        w = std::min(w, ds->w);
        h = std::min(h, ds->h);
    }
    auto pass = Pass(rt != nullptr, ds != nullptr);
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = pass;
    bi.framebuffer = Framebuffer(rt, ds, pass);
    bi.renderArea.extent = {w, h};
    vkCmdBeginRenderPass(cmd_, &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkClearAttachment attachments[2]{};
    uint32_t n = 0;
    if (rt) {
        attachments[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        Color(color, attachments[n].clearValue.color.float32);
        n++;
        rt->drawn = true;
    }
    if (ds) {
        attachments[n].aspectMask = ((flags & kClearZ) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                                    ((flags & kClearStencil) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        attachments[n].clearValue.depthStencil = {z, stencil};
        n++;
        ds->drawn = true;
    }
    for (uint32_t i = 0; i < count; i++) {
        int32_t r[4];
        std::memcpy(r, rects + i * 16, 16);
        int x0 = std::clamp(r[0], 0, int(w)), y0 = std::clamp(r[1], 0, int(h));
        int x1 = std::clamp(r[2], x0, int(w)), y1 = std::clamp(r[3], y0, int(h));
        if (x1 > x0 && y1 > y0) {
            VkClearRect cr{{{x0, y0}, {uint32_t(x1 - x0), uint32_t(y1 - y0)}}, 0, 1};
            vkCmdClearAttachments(cmd_, n, attachments, 1, &cr);
        }
    }
    vkCmdEndRenderPass(cmd_);
}
void Vulkan::Copy(const Surface &src, const Surface &dst, uint32_t count, uint32_t rects, uint32_t points) {
    LOG_FIRST(12, "vulkan: CopyRects src=%08X dst=%08X count=%u rects=%08X points=%08X", src.data, dst.data, count,
              rects, points);
    TextureDesc a, b;
    if (!DescribeSurface(src, a) || !DescribeSurface(dst, b))
        Fail("invalid CopyRects surface");
    bool az = Format(a.format).depth, bz = Format(b.format).depth;
    auto existing = targets_.find(uint64_t(a.data) << 1 | uint64_t(az));
    if (existing == targets_.end() || !existing->second->drawn || az != bz) {
        if (existing != targets_.end() && existing->second->drawn) {
            if (az)
                Fail("depth-to-color GPU copy requires depth readback");
            Sync(a.data, 0);
        }
        if (!CopySurfaceMemory(src, dst, count, rects, points))
            Fail("unsupported CPU CopyRects format");
        if (bz)
            UploadDepth(*Target(dst, true), b);
        else if (targets_.contains(uint64_t(b.data) << 1))
            Target(dst, false);
        return;
    }
    Image *from = Target(src, az);
    Image *to = Target(dst, bz);
    if (!from || !to)
        Fail("invalid CopyRects surface");
    if (from == to)
        Fail("overlapping CopyRects requires an intermediate image");
    Layout(*from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Layout(*to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    uint32_t copies = count && rects ? count : 1;
    for (uint32_t i = 0; i < copies; i++) {
        int32_t r[4] = {0, 0, int32_t(std::min(from->w, to->w)), int32_t(std::min(from->h, to->h))}, p[2] = {0, 0};
        if (count && rects) {
            std::memcpy(r, kernel::HostPtr(rects + i * 16), 16);
            p[0] = r[0];
            p[1] = r[1];
        }
        if (points)
            std::memcpy(p, kernel::HostPtr(points + i * 8), 8);
        if (r[0] < 0 || r[1] < 0 || p[0] < 0 || p[1] < 0 || r[2] < r[0] || r[3] < r[1] || uint32_t(r[2]) > from->w ||
            uint32_t(r[3]) > from->h || uint32_t(p[0] + r[2] - r[0]) > to->w || uint32_t(p[1] + r[3] - r[1]) > to->h)
            Fail("CopyRects bounds");
        if (r[2] == r[0] || r[3] == r[1])
            continue;
        VkImageCopy c{};
        c.srcSubresource = c.dstSubresource = {az ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.srcOffset = {r[0], r[1], 0};
        c.dstOffset = {p[0], p[1], 0};
        c.extent = {uint32_t(r[2] - r[0]), uint32_t(r[3] - r[1]), 1};
        vkCmdCopyImage(cmd_, from->image, from->layout, to->image, to->layout, 1, &c);
        if (az) {
            c.srcSubresource.aspectMask = c.dstSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
            vkCmdCopyImage(cmd_, from->image, from->layout, to->image, to->layout, 1, &c);
        }
    }
    to->drawn = true;
}
void Vulkan::Read(Image &t, std::vector<uint8_t> &out) {
    if (t.z)
        Fail("CPU depth readback not yet implemented");
    Layout(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    size_t bytes = size_t(t.w) * t.h * 4;
    auto off = Upload(nullptr, bytes, 16);
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {t.w, t.h, 1};
    vkCmdCopyImageToBuffer(cmd_, t.image, t.layout, arena_, 1, &c);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    Submit();
    out.assign(mapped_ + off, mapped_ + off + bytes);
}
void Vulkan::Sync(uint32_t data, uint32_t) {
    auto it = targets_.find(uint64_t(data) << 1);
    if (it == targets_.end() || !it->second->drawn)
        return;
    auto &t = *it->second;
    std::vector<uint8_t> px;
    Read(t, px);
    if (Format(t.desc.format).host != Host::BGRA8 || !t.desc.linear)
        Fail("CPU readback requires a linear 32-bit color surface");
    auto *guest = static_cast<uint8_t *>(kernel::HostPtr(data));
    for (uint32_t y = 0; y < t.h; y++)
        std::memcpy(guest + size_t(y) * t.desc.pitch, px.data() + size_t(y) * t.w * 4, t.w * 4);
    t.signature = Hash(guest, TotalSize(t.desc));
    t.drawn = false;
}
void Vulkan::GetReport(uint32_t va) {
    Submit();
    kernel::W32(va, uint32_t(frame_));
    kernel::W32(va + 4, 0);
    kernel::W32(va + 8, uint32_t(std::min<uint64_t>(query_sum_, 0xffffffffu)));
    std::atomic_thread_fence(std::memory_order_release);
    kernel::W32(va + 12, 0);
}
void Vulkan::SetGamma(const uint8_t *ramp) {
    std::memcpy(gamma_, ramp, 768);
    gamma_identity_ = true;
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++)
            gamma_identity_ &= ramp[c * 256 + i] == i;
}
void Vulkan::Resize() {
    auto size = app::LinuxWindowSize();
    if (!size.width || !size.height)
        return;
    if (swap_ && extent_.width == size.width && extent_.height == size.height)
        return;
    Submit();
    Check(vkQueueWaitIdle(queue_), "swapchain idle");
    VkSurfaceCapabilitiesKHR caps;
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps), "surface capabilities");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        Fail("swapchain transfer destination unavailable");
    uint32_t n = 0;
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr), "surface formats");
    std::vector<VkSurfaceFormatKHR> formats(n);
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data()), "surface formats");
    auto choice = formats.front();
    for (auto f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            choice = f;
            break;
        }
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = std::max(2u, caps.minImageCount);
    if (caps.maxImageCount)
        ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
    ci.imageFormat = choice.format;
    ci.imageColorSpace = choice.colorSpace;
    ci.imageExtent = caps.currentExtent.width != 0xffffffffu
                         ? caps.currentExtent
                         : VkExtent2D{std::clamp(size.width, caps.minImageExtent.width, caps.maxImageExtent.width),
                                      std::clamp(size.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (!ci.imageExtent.width || !ci.imageExtent.height)
        return;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & ci.compositeAlpha))
        ci.compositeAlpha =
            VkCompositeAlphaFlagBitsKHR(caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swap_;
    VkSwapchainKHR next;
    Check(vkCreateSwapchainKHR(device_, &ci, nullptr, &next), "swapchain");
    if (swap_)
        vkDestroySwapchainKHR(device_, swap_, nullptr);
    swap_ = next;
    extent_ = ci.imageExtent;
    swap_format_ = choice.format;
    Check(vkGetSwapchainImagesKHR(device_, swap_, &n, nullptr), "swapchain images");
    swap_images_.resize(n);
    Check(vkGetSwapchainImagesKHR(device_, swap_, &n, swap_images_.data()), "swapchain images");
    LOGR("vulkan: swapchain %ux%u", extent_.width, extent_.height);
}
void Vulkan::Capture(Image &t) {
    auto dir = diag::CaptureDir();
    if (dir.empty())
        return;
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> px;
    Read(t, px);
    auto *surface = SDL_CreateSurfaceFrom(int(t.w), int(t.h), SDL_PIXELFORMAT_BGRA32, px.data(), int(t.w * 4));
    auto file = dir / ("frame-" + std::to_string(frame_) + ".bmp");
    if (!surface || !SDL_SaveBMP(surface, file.c_str()))
        Fail("capture failed");
    SDL_DestroySurface(surface);
    LOGR("vulkan: captured %s", file.c_str());
}
void Vulkan::Present(const Surface &front) {
    Image *t = Target(front, false);
    if (!t)
        Fail("present has no front surface");
    if (g_capture.exchange(false) || frame_ == 0 || frame_ == 120 || frame_ == 600 || frame_ == 1800 || frame_ == 3000)
        Capture(*t);
    Resize();
    auto size = app::LinuxWindowSize();
    if (!swap_ || !size.width || !size.height) {
        Submit();
        return;
    }
    uint32_t index = 0;
    VkResult result = vkAcquireNextImageKHR(device_, swap_, 30000000000ull, acquired_, VK_NULL_HANDLE, &index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        extent_ = {};
        Resize();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        Check(result, "acquire swapchain");
    std::unique_ptr<Image> adjusted;
    if (!gamma_identity_) {
        std::vector<uint8_t> px;
        Read(*t, px);
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = gamma_[512 + px[i]];
            px[i + 1] = gamma_[256 + px[i + 1]];
            px[i + 2] = gamma_[px[i + 2]];
        }
        adjusted = MakeImage(t->w, t->h, 1, 1, 1, VK_FORMAT_B8G8R8A8_UNORM, false, false);
        Layout(*adjusted, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy c{};
        c.bufferOffset = Upload(px.data(), px.size());
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {t->w, t->h, 1};
        vkCmdCopyBufferToImage(cmd_, arena_, adjusted->image, adjusted->layout, 1, &c);
    }
    Image &source = adjusted ? *adjusted : *t;
    Layout(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Image back;
    back.image = swap_images_[index];
    back.w = extent_.width;
    back.h = extent_.height;
    back.format = swap_format_;
    Layout(back, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue black{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd_, back.image, back.layout, &black, 1, &range);
    float aspect = app::FrameAspect();
    int w = int(extent_.width), h = int(extent_.height);
    if (aspect > 0) {
        if (float(w) / h > aspect)
            w = int(std::lround(h * aspect));
        else
            h = int(std::lround(w / aspect));
    }
    int x = (int(extent_.width) - w) / 2, y = (int(extent_.height) - h) / 2;
    float crop = app::Get().overscan * 0.01f;
    int cx = int(std::lround(source.w * crop)), cy = int(std::lround(source.h * crop));
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {cx, cy, 0};
    blit.srcOffsets[1] = {int(source.w) - cx, int(source.h) - cy, 1};
    blit.dstOffsets[0] = {x, y, 0};
    blit.dstOffsets[1] = {x + w, y + h, 1};
    vkCmdBlitImage(cmd_, source.image, source.layout, back.image, back.layout, 1, &blit, VK_FILTER_LINEAR);
    Layout(back, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    Submit(acquired_);
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap_;
    pi.pImageIndices = &index;
    result = vkQueuePresentKHR(queue_, &pi);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
        Check(result, "present");
    if (adjusted)
        DestroyImage(*adjusted);
    frame_++;
    if (frame_ % 60 == 0)
        LOGR("vulkan: frames=%llu draws=%llu pipelines=%zu textures=%zu", static_cast<unsigned long long>(frame_),
             static_cast<unsigned long long>(draws_), pipelines_.size(), textures_.size());
    // Submitted work has retired; cached sampled textures can now be reclaimed safely.
    for (auto it = textures_.begin(); it != textures_.end();)
        if (frame_ - it->second->used > 120) {
            DestroyImage(*it->second);
            it = textures_.erase(it);
        } else
            ++it;
}
Vulkan::~Vulkan() {
    if (!device_)
        return;
    vkDeviceWaitIdle(device_);
    for (auto [k, v] : framebuffers_)
        vkDestroyFramebuffer(device_, v, nullptr);
    for (auto [k, v] : pipelines_)
        vkDestroyPipeline(device_, v, nullptr);
    for (auto [k, v] : passes_)
        vkDestroyRenderPass(device_, v, nullptr);
    for (auto [k, v] : shaders_)
        vkDestroyShaderModule(device_, v, nullptr);
    for (auto [k, v] : samplers_)
        vkDestroySampler(device_, v, nullptr);
    for (auto &[k, v] : targets_)
        DestroyImage(*v);
    for (auto &[k, v] : textures_)
        DestroyImage(*v);
    for (auto &t : transient_)
        DestroyImage(*t);
    if (white_)
        DestroyImage(*white_);
    if (swap_)
        vkDestroySwapchainKHR(device_, swap_, nullptr);
    vkDestroyQueryPool(device_, queries_, nullptr);
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, descriptors_, nullptr);
    vkUnmapMemory(device_, arena_mem_);
    vkDestroyBuffer(device_, arena_, nullptr);
    vkFreeMemory(device_, arena_mem_, nullptr);
    vkDestroySemaphore(device_, acquired_, nullptr);
    vkDestroyFence(device_, fence_, nullptr);
    vkDestroyCommandPool(device_, pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}
} // namespace
std::unique_ptr<Backend> CreateBackend() {
    return std::make_unique<Vulkan>();
}
// The experimental backend executes guest calls in order on the caller's guest thread.
std::unique_ptr<Backend> CreateThreadedBackend(std::unique_ptr<Backend> inner) {
    return inner;
}
} // namespace gpu
