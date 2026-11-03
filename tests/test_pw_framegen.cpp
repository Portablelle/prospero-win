/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the production present wrapper with delayed presentation waits:
 * rendering fences complete immediately, presentation waits only on reacquire. */
#include "../wine/ps5/pw_framegen.cpp"
#include <cassert>
#include <cstdio>

extern "C" const unsigned char pw_framegen_network[] = {0};
extern "C" const unsigned char pw_framegen_network_end[] = {0};
extern "C" PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance,const char*) { return nullptr; }
extern "C" PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice,const char*) { return nullptr; }
VkResult fg_context_create(const fg_create_info*,fg_context**,char*,size_t) { return VK_ERROR_INITIALIZATION_FAILED; }
void fg_context_destroy(fg_context*) {}
VkResult fg_context_load_network(fg_context*,const void*,size_t,char*,size_t) { return VK_ERROR_INITIALIZATION_FAILED; }
void fg_record_generate(fg_context*,VkCommandBuffer,uint32_t,fg_method,const fg_frame_pair*,VkImageView) {}

static unsigned waits_seen, fence_waits, submits, presents;
static int target = -1;
static bool submit_failed, pending[4];
static Chain chain;
template<typename T> static T handle(uintptr_t n) { return (T)n; }
static VkResult VKAPI_CALL wait_fences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t)
{ fence_waits++; return VK_SUCCESS; }
static VkResult VKAPI_CALL reset_fences(VkDevice,uint32_t,const VkFence*) { return VK_SUCCESS; }
static VkResult VKAPI_CALL reset_command(VkCommandBuffer,VkCommandBufferResetFlags) { return VK_SUCCESS; }
static VkResult VKAPI_CALL begin_command(VkCommandBuffer,const VkCommandBufferBeginInfo*) { return VK_SUCCESS; }
static VkResult VKAPI_CALL end_command(VkCommandBuffer) { return VK_SUCCESS; }
static void VKAPI_CALL pipeline_barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,
 VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*) {}
static void VKAPI_CALL blit_image(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit*,VkFilter) {}
static VkResult VKAPI_CALL acquire(VkDevice,VkSwapchainKHR,uint64_t timeout,VkSemaphore,VkFence,uint32_t *out)
{
    assert(timeout==0); /* application-owned images can exhaust the chain */
    if(target<0) return VK_NOT_READY;
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
    recreate.oldSwapchain=handle<VkSwapchainKHR>(3);VkSwapchainKHR output;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==1);
    recreate.oldSwapchain=VK_NULL_HANDLE;
    assert(fg_CreateSwapchainKHR(handle<VkDevice>(1),&recreate,nullptr,&output)==VK_ERROR_INITIALIZATION_FAILED);
    assert(swapchain_attempts==3);
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
