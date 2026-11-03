/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the production present wrapper with delayed presentation waits:
 * rendering fences complete immediately, presentation waits only on reacquire. */
#include "../wine/ps5/pw_framegen.cpp"
#include <cassert>
#include <cstdio>

extern "C" const unsigned char pw_framegen_network[] = {0};
extern "C" const unsigned char pw_framegen_network_end[] = {0};
static VkResult VKAPI_CALL surface_capabilities(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR*);
static void VKAPI_CALL physical_properties(VkPhysicalDevice,VkPhysicalDeviceProperties*);
extern "C" PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance,const char* name) {
    if(!strcmp(name,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return (PFN_vkVoidFunction)surface_capabilities;
    if(!strcmp(name,"vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)physical_properties;
    return nullptr;
}
extern "C" PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice,const char*) { return nullptr; }
VkResult fg_context_create(const fg_create_info*,fg_context**,char*,size_t) { return VK_ERROR_INITIALIZATION_FAILED; }
void fg_context_destroy(fg_context*) {}
VkResult fg_context_load_network(fg_context*,const void*,size_t,char*,size_t) { return VK_ERROR_INITIALIZATION_FAILED; }
void fg_record_generate(fg_context*,VkCommandBuffer,uint32_t,fg_method,const fg_frame_pair*,VkImageView) {}

static unsigned waits_seen, fence_waits, all_fences, submits, presents;
static int target = -1;
static bool submit_failed, pending[4], acquisition_pending, acquisition_error;
static void VKAPI_CALL destroy_semaphore(VkDevice,VkSemaphore semaphore,const VkAllocationCallbacks*)
{ if((uintptr_t)semaphore==888) assert(!acquisition_pending); }
static void VKAPI_CALL destroy_fence(VkDevice,VkFence fence,const VkAllocationCallbacks*)
{ if((uintptr_t)fence==777) assert(!acquisition_pending); }
static VkResult VKAPI_CALL device_idle(VkDevice) { return VK_SUCCESS; }
static Chain chain;
template<typename T> static T handle(uintptr_t n) { return (T)n; }
static VkResult VKAPI_CALL wait_fences(VkDevice,uint32_t count,const VkFence *fences,VkBool32,uint64_t)
{ for(unsigned i=0;i<count;i++) if((uintptr_t)fences[i]==777) acquisition_pending=false;
  fence_waits++; if(count==kSlots) all_fences++; return VK_SUCCESS; }
static VkResult VKAPI_CALL reset_fences(VkDevice,uint32_t,const VkFence*) { return VK_SUCCESS; }
static VkResult VKAPI_CALL reset_command(VkCommandBuffer,VkCommandBufferResetFlags) { return VK_SUCCESS; }
static VkResult VKAPI_CALL begin_command(VkCommandBuffer,const VkCommandBufferBeginInfo*) { return VK_SUCCESS; }
static VkResult VKAPI_CALL end_command(VkCommandBuffer) { return VK_SUCCESS; }
static void VKAPI_CALL pipeline_barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,
 VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*) {}
static void VKAPI_CALL blit_image(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit*,VkFilter) {}
static VkResult VKAPI_CALL acquire(VkDevice,VkSwapchainKHR,uint64_t timeout,VkSemaphore,VkFence fence,uint32_t *out)
{
    assert(timeout==0); /* application-owned images can exhaust the chain */
    if(target<0) return acquisition_error ? VK_ERROR_SURFACE_LOST_KHR : VK_NOT_READY;
    if((uintptr_t)fence==777) acquisition_pending=true;
    *out=(uint32_t)target; pending[target]=false; target=-1; return VK_SUCCESS;
}
static VkResult VKAPI_CALL submit(VkQueue,uint32_t count,const VkSubmitInfo *info,VkFence)
{
    assert(count==1);submits++;waits_seen=info->waitSemaphoreCount;
    if(submit_failed) return VK_ERROR_OUT_OF_HOST_MEMORY;
    for(uint32_t s=0;s<info->signalSemaphoreCount;s++) {
        unsigned i=(unsigned)((uintptr_t)info->pSignalSemaphores[s]-100);
        assert(i<4 && !pending[i]); /* a rendering fence alone cannot clear this */
    }
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL present(VkQueue,const VkPresentInfoKHR *info)
{
    assert(info->waitSemaphoreCount==1 && info->swapchainCount==1);
    unsigned image=*info->pImageIndices;
    assert(info->pWaitSemaphores[0]==chain.ready[image]);
    assert(!pending[image]);pending[image]=true;presents++;return VK_SUCCESS;
}
static bool allocation_failed;
static unsigned pools_destroyed;
static VkResult VKAPI_CALL create_pool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool *out)
{ *out=handle<VkCommandPool>(999);return VK_SUCCESS; }
static void VKAPI_CALL destroy_pool(VkDevice,VkCommandPool,const VkAllocationCallbacks*) { pools_destroyed++; }
static VkResult VKAPI_CALL allocate_commands(VkDevice,const VkCommandBufferAllocateInfo *info,VkCommandBuffer *out)
{ if(allocation_failed) return VK_ERROR_OUT_OF_HOST_MEMORY;for(unsigned i=0;i<info->commandBufferCount;i++)out[i]=handle<VkCommandBuffer>(1000+i);return VK_SUCCESS; }
static VkSurfaceCapabilitiesKHR capabilities{};
static uint32_t physical_version=VK_API_VERSION_1_3;
static VkResult VKAPI_CALL surface_capabilities(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR *out)
{ *out=capabilities;return VK_SUCCESS; }
static void VKAPI_CALL physical_properties(VkPhysicalDevice,VkPhysicalDeviceProperties *out)
{ out->apiVersion=physical_version; }
static unsigned swapchain_attempts;
static VkResult VKAPI_CALL create_swapchain(VkDevice,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*)
{ swapchain_attempts++; return VK_ERROR_INITIALIZATION_FAILED; }
static VkResult VKAPI_CALL create_device(VkPhysicalDevice,const VkDeviceCreateInfo*,const VkAllocationCallbacks*,VkDevice *device)
{ *device=handle<VkDevice>(77);return VK_SUCCESS; }
static void VKAPI_CALL destroy_device(VkDevice,const VkAllocationCallbacks*) {}
static void VKAPI_CALL get_queue(VkDevice,uint32_t,uint32_t,VkQueue *queue)
{ *queue=handle<VkQueue>(88); }
int main()
{
    pw_fgvk_WaitForFences=wait_fences;pw_fgvk_ResetFences=reset_fences;
    pw_fgvk_ResetCommandBuffer=reset_command;pw_fgvk_BeginCommandBuffer=begin_command;
    pw_fgvk_EndCommandBuffer=end_command;pw_fgvk_CmdPipelineBarrier=pipeline_barrier;
    pw_fgvk_CmdBlitImage=blit_image;pw_fgvk_AcquireNextImageKHR=acquire;pw_fgvk_QueueSubmit=submit;
    real_QueuePresentKHR=present;
    chain.device=handle<VkDevice>(1);chain.handle=handle<VkSwapchainKHR>(2);
    chain.queue=handle<VkQueue>(5);
    chain.pool=handle<VkCommandPool>(3);chain.family=0;chain.factor=3;chain.have_prev=true;
    chain.image_count=4;chain.extent={32,32};
    for(unsigned i=0;i<4;i++) chain.ready[i]=handle<VkSemaphore>(100+i);
    chains[0]=&chain;
    VkSemaphore app_waits[20];for(unsigned i=0;i<20;i++)app_waits[i]=handle<VkSemaphore>(200+i);
    uint32_t image=0;VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount=20;info.pWaitSemaphores=app_waits;info.swapchainCount=1;
    info.pSwapchains=&chain.handle;info.pImageIndices=&image;
    target=2;
    assert(fg_QueuePresentKHR(handle<VkQueue>(5),&info)==VK_SUCCESS);
    assert(waits_seen==21 && presents==2); /* all 20 waits plus one acquisition */
    /* Two more ring advances while old presentation waits remain pending.
     * Ready semaphores follow reacquired images, not the two rendering slots. */
    for(unsigned i=0;i<2;i++) {
        image=i?0:1;pending[image]=false;
        assert(fg_QueuePresentKHR(handle<VkQueue>(5),&info)==VK_SUCCESS);
        assert(waits_seen==20);
    }
    image=3;pending[image]=false;submit_failed=true;
    assert(fg_QueuePresentKHR(handle<VkQueue>(5),&info)==VK_ERROR_OUT_OF_HOST_MEMORY);
    unsigned before=fence_waits;
    assert(fg_QueuePresentKHR(handle<VkQueue>(5),&info)==VK_ERROR_OUT_OF_DATE_KHR);
    assert(fence_waits==before); /* no wait on the failed submission's reset fence */
    chains[0]=nullptr;
    chain.failed=false;
    pw_fgvk_CreateCommandPool=create_pool;pw_fgvk_DestroyCommandPool=destroy_pool;
    pw_fgvk_AllocateCommandBuffers=allocate_commands;
    assert(setup_commands(&chain,0,handle<VkQueue>(6)) && all_fences==1);
    assert(!setup_commands(&chain,1,handle<VkQueue>(7)) && chain.failed);
    Chain fresh;fresh.device=chain.device;
    allocation_failed=true;
    assert(!setup_commands(&fresh,0,handle<VkQueue>(7)));
    assert(!fresh.pool && fresh.family==UINT32_MAX && pools_destroyed==1);
    allocation_failed=false;
    assert(setup_commands(&fresh,0,handle<VkQueue>(7)) && fresh.cmd[0]);
    // Acquire one image, then fail the next acquisition before submission.
    // Queue idle does not retire that WSI signal: destruction must wait its fence.
    Chain *abandoned=new Chain;
    abandoned->device=chain.device;abandoned->handle=chain.handle;
    abandoned->pool=handle<VkCommandPool>(3);abandoned->family=0;abandoned->queue=handle<VkQueue>(5);
    abandoned->image_count=4;abandoned->factor=3;abandoned->have_prev=true;
    abandoned->acquire_fence[0][0]=handle<VkFence>(777);
    abandoned->acquired[0][0]=handle<VkSemaphore>(888);
    chains[0]=abandoned;info.pSwapchains=&abandoned->handle;
    target=2;acquisition_error=true;
    assert(fg_QueuePresentKHR(handle<VkQueue>(5),&info)==VK_ERROR_SURFACE_LOST_KHR);
    assert(acquisition_pending && abandoned->acquire_pending[0][0]);
    chains[0]=nullptr;device_loaded=true;
    pw_fgvk_DeviceWaitIdle=device_idle;pw_fgvk_DestroySemaphore=destroy_semaphore;pw_fgvk_DestroyFence=destroy_fence;
    free_chain(abandoned);assert(!acquisition_pending);device_loaded=false;acquisition_error=false;
    real_CreateDevice=create_device;real_DestroyDevice=destroy_device;real_GetDeviceQueue=get_queue;
    for(unsigned i=0;i<12;i++) {
        VkDevice device;VkQueue queue;
        assert(fg_CreateDevice(handle<VkPhysicalDevice>(50+i),nullptr,nullptr,&device)==VK_SUCCESS);
        assert(physical_of(device)==handle<VkPhysicalDevice>(50+i));
        fg_GetDeviceQueue(device,2,0,&queue);assert(family_of(queue)==2);
        fg_DestroyDevice(device,nullptr);assert(!physical_of(device));
        assert(family_of(queue)==0);
    }
    real_CreateSwapchainKHR=create_swapchain;factor_setting=2;
    VkSwapchainCreateInfoKHR recreate{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    recreate.imageFormat=VK_FORMAT_R8G8B8A8_UNORM;
    recreate.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    recreate.oldSwapchain=handle<VkSwapchainKHR>(3);VkSwapchainKHR output;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==1); // unknown API environment passes through once
    instances[0]={handle<VkInstance>(4),VK_API_VERSION_1_3};
    remember_physical(instances[0].handle,handle<VkPhysicalDevice>(50));
    assert(spirv16_of(handle<VkPhysicalDevice>(50)));
    devices[0]={handle<VkDevice>(1),handle<VkPhysicalDevice>(50),false,true};
    capabilities.maxImageCount=2;
    capabilities.supportedUsageFlags=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    recreate.minImageCount=2;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==2); // insufficient capacity: no augmentation
    capabilities.maxImageCount=0;
    capabilities.supportedUsageFlags=0;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==3); // missing transfer support: no augmentation
    capabilities.supportedUsageFlags=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==4); // old handle cannot be retried
    recreate.oldSwapchain=VK_NULL_HANDLE;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==6);
    instances[0].version=VK_API_VERSION_1_2;
    remember_physical(instances[0].handle,handle<VkPhysicalDevice>(51));
    assert(!spirv16_of(handle<VkPhysicalDevice>(51)));
    instances[0].version=VK_API_VERSION_1_3;physical_version=VK_API_VERSION_1_2;
    remember_physical(instances[0].handle,handle<VkPhysicalDevice>(52));
    assert(!spirv16_of(handle<VkPhysicalDevice>(52)));
    recreate.imageFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
    unsigned attempts_before=swapchain_attempts;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==attempts_before+1); // unsupported HDR passes through once
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkPhysicalDeviceVulkan13Features core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    device_info.pNext=&core;
    assert(!maintenance4_enabled(&device_info));core.maintenance4=VK_TRUE;
    assert(maintenance4_enabled(&device_info));
    VkPhysicalDeviceMaintenance4Features extension{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES};
    device_info.pNext=&extension;extension.maintenance4=VK_TRUE;
    assert(maintenance4_enabled(&device_info));
    puts("framegen: nonblocking acquisition, all waits, image-owned semaphores, failed submit retirement and device reuse passed");
}
