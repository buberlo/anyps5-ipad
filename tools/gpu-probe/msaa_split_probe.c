/* Own offscreen proof of two programmable 4-sample coverage subsets.
 * --array preserves and reads eight logical samples in two native array layers.
 * This is not an AnyPS5 renderer or a guest multisample image implementation. */
#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define SIDE 16u
#define PIXELS (SIDE * SIDE)
#define CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    printf("[msaa-split] failed=%s code=%d\n", #call, r_); goto cleanup; } } while (0)
static unsigned positions[8][2] = {
    {2,2}, {6,2}, {10,2}, {14,2}, {2,10}, {6,10}, {10,10}, {14,10}
};
static uint32_t memory_type(VkPhysicalDevice p, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties m;
    vkGetPhysicalDeviceMemoryProperties(p, &m);
    for (uint32_t i=0; i<m.memoryTypeCount; ++i)
        if ((bits & (1u<<i)) && (m.memoryTypes[i].propertyFlags & flags)==flags) return i;
    return UINT32_MAX;
}
static VkResult shader(VkDevice device, const char *path, VkShaderModule *module) {
    FILE *f=fopen(path,"rb"); if (!f) return VK_ERROR_INITIALIZATION_FAILED;
    if (fseek(f,0,SEEK_END)) { fclose(f); return VK_ERROR_INITIALIZATION_FAILED; }
    long n=ftell(f);
    if (n<=0 || n>1024*1024 || n%4 || fseek(f,0,SEEK_SET)) { fclose(f); return VK_ERROR_INITIALIZATION_FAILED; }
    uint32_t *code=malloc((size_t)n); if (!code) { fclose(f); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    if (fread(code,1,(size_t)n,f)!=(size_t)n) { free(code); fclose(f); return VK_ERROR_INITIALIZATION_FAILED; }
    fclose(f);
    VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)n,.pCode=code};
    VkResult rc=vkCreateShaderModule(device,&ci,NULL,module); free(code); return rc;
}
static int coverage(unsigned x, unsigned y, unsigned first, unsigned samples) {
    const int v[3][2]={{20,18},{215,43},{63,229}};
    int covered=0;
    for (unsigned s=first;s<first+samples;++s) {
        const int px=(int)(16*x+positions[s][0]), py=(int)(16*y+positions[s][1]);
        int inside=1;
        for (unsigned e=0;e<3;++e) {
            const int *a=v[e], *b=v[(e+1)%3];
            const int edge=(b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]);
            if (!edge) return -1; /* The fixture avoids top-left boundary ties. */
            if (edge<0) inside=0;
        }
        covered+=inside;
    }
    return covered;
}
int main(int argc, char **argv) {
    const int array = argc>1 && !strcmp(argv[1],"--array");
    if (argc>2+array) return 64;
    if (argc==2+array) {
        unsigned char bytes[16]; FILE *f=fopen(argv[1+array],"rb");
        if (!f) return 64;
        const size_t read=fread(bytes,1,sizeof(bytes),f);
        const int extra=fgetc(f); fclose(f);
        if (read!=sizeof(bytes) || extra!=EOF) return 64;
        for (unsigned i=0;i<8;++i) {
            if (bytes[2*i]>15 || bytes[2*i+1]>15) return 64;
            positions[i][0]=bytes[2*i]; positions[i][1]=bytes[2*i+1];
            for (unsigned j=0;j<i;++j)
                if (positions[j][0]==positions[i][0] && positions[j][1]==positions[i][1]) return 64;
        }
    }
    int result=1;
    VkInstance instance=VK_NULL_HANDLE; VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE; VkQueue queue=VK_NULL_HANDLE;
    VkImage images[2]={0}; VkDeviceMemory image_memory[2]={0}; VkImageView views[2]={0};
    VkImageView second_view=VK_NULL_HANDLE, sample_view=VK_NULL_HANDLE;
    VkBuffer readback=VK_NULL_HANDLE; VkDeviceMemory readback_memory=VK_NULL_HANDLE; void *mapped=NULL;
    VkRenderPass pass=VK_NULL_HANDLE; VkFramebuffer framebuffers[2]={0};
    VkPipelineLayout layout=VK_NULL_HANDLE, compute_layout=VK_NULL_HANDLE;
    VkPipeline pipelines[2]={0}, compute_pipeline=VK_NULL_HANDLE; VkShaderModule modules[3]={0};
    VkDescriptorSetLayout set_layout=VK_NULL_HANDLE; VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="MSAA split proof",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    CHECK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));
    if (!count) goto cleanup;
    VkPhysicalDevice *devices=calloc(count,sizeof(*devices)); if (!devices) goto cleanup;
    VkResult enumerated=vkEnumeratePhysicalDevices(instance,&count,devices);
    if (enumerated==VK_SUCCESS) physical=devices[0];
    free(devices);
    if (!physical) goto cleanup;
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical,&properties);
    if (properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU) { result=77; goto cleanup; }
    count=0; CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL));
    VkExtensionProperties *exts=calloc(count,sizeof(*exts)); if (!exts) goto cleanup;
    VkResult er=vkEnumerateDeviceExtensionProperties(physical,NULL,&count,exts);
    int locations_present=0, portability_present=0;
    if (er==VK_SUCCESS) for (unsigned i=0;i<count;++i) {
        locations_present |= !strcmp(exts[i].extensionName,"VK_EXT_sample_locations");
        portability_present |= !strcmp(exts[i].extensionName,"VK_KHR_portability_subset");
    }
    free(exts); if (!locations_present) { result=77; goto cleanup; }
    VkPhysicalDevicePortabilitySubsetFeaturesKHR portable={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR};
    if (array && portability_present) {
        VkPhysicalDeviceFeatures2 feature2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&portable};
        vkGetPhysicalDeviceFeatures2(physical,&feature2);
        if (!portable.multisampleArrayImage) {result=77;goto cleanup;}
        memset(&portable,0,sizeof(portable));
        portable.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
        portable.multisampleArrayImage=VK_TRUE;
    }
    VkPhysicalDeviceSampleLocationsPropertiesEXT locprops={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 prop2={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&locprops};
    vkGetPhysicalDeviceProperties2(physical,&prop2);
    if (!(locprops.sampleLocationSampleCounts & VK_SAMPLE_COUNT_4_BIT) || locprops.sampleLocationSubPixelBits<4 ||
        locprops.maxSampleLocationGridSize.width<1 || locprops.maxSampleLocationGridSize.height<1 ||
        locprops.sampleLocationCoordinateRange[0]>0 || locprops.sampleLocationCoordinateRange[1]<0.9375f) { result=77; goto cleanup; }
    count=0; vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);
    VkQueueFamilyProperties *q=calloc(count,sizeof(*q)); if (!q) goto cleanup;
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,q);
    uint32_t family=UINT32_MAX;
    const VkQueueFlags required=VK_QUEUE_GRAPHICS_BIT|(array?VK_QUEUE_COMPUTE_BIT:0);
    for (unsigned i=0;i<count;++i) if (q[i].queueCount && (q[i].queueFlags & required)==required) {family=i;break;}
    free(q); if (family==UINT32_MAX) goto cleanup;
    float priority=1;
    VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extensions[]={"VK_EXT_sample_locations","VK_KHR_portability_subset"};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,
        .pNext=array && portability_present?&portable:NULL,
        .enabledExtensionCount=portability_present?2:1,.ppEnabledExtensionNames=extensions};
    CHECK(vkCreateDevice(physical,&dci,NULL,&device)); vkGetDeviceQueue(device,family,0,&queue);
    for (unsigned i=0;i<2;++i) {
        VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_SFLOAT,
            .extent={SIDE,SIDE,1},.mipLevels=1,.arrayLayers=array && i==0?2:1,.samples=i==0?VK_SAMPLE_COUNT_4_BIT:VK_SAMPLE_COUNT_1_BIT,
            .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|(i==1?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0)|(array && i==0?VK_IMAGE_USAGE_SAMPLED_BIT:0)};
        CHECK(vkCreateImage(device,&ci,NULL,&images[i]));
        VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device,images[i],&requirements);
        uint32_t type=memory_type(physical,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type==UINT32_MAX) goto cleanup;
        VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=type};
        CHECK(vkAllocateMemory(device,&ai,NULL,&image_memory[i])); CHECK(vkBindImageMemory(device,images[i],image_memory[i],0));
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[i],.viewType=VK_IMAGE_VIEW_TYPE_2D,
            .format=VK_FORMAT_R32_SFLOAT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        CHECK(vkCreateImageView(device,&vi,NULL,&views[i]));
    }
    if (array) {
        VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[0],.viewType=VK_IMAGE_VIEW_TYPE_2D,
            .format=VK_FORMAT_R32_SFLOAT,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,1,1}};
        CHECK(vkCreateImageView(device,&vi,NULL,&second_view));
        vi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; vi.subresourceRange.baseArrayLayer=0; vi.subresourceRange.layerCount=2;
        CHECK(vkCreateImageView(device,&vi,NULL,&sample_view));
    }
    VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=(4*PIXELS+(array?2*PIXELS*9:0))*sizeof(float),
        .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT|(array?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:0)};
    CHECK(vkCreateBuffer(device,&bci,NULL,&readback));
    VkMemoryRequirements br; vkGetBufferMemoryRequirements(device,readback,&br);
    uint32_t mt=memory_type(physical,br.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt==UINT32_MAX) goto cleanup;
    VkMemoryAllocateInfo bai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=br.size,.memoryTypeIndex=mt};
    CHECK(vkAllocateMemory(device,&bai,NULL,&readback_memory)); CHECK(vkBindBufferMemory(device,readback,readback_memory,0));
    VkAttachmentDescription attachments[2]={
        {.format=VK_FORMAT_R32_SFLOAT,.samples=VK_SAMPLE_COUNT_4_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=array?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout=array?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {.format=VK_FORMAT_R32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}};
    VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},resolve={1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color,.pResolveAttachments=&resolve};
    VkSubpassDependency dependencies[2]={
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT|(array?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:0),
         .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|(array?VK_ACCESS_SHADER_READ_BIT:0)}};
    VkRenderPassCreateInfo rpci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=2,.pAttachments=attachments,
        .subpassCount=1,.pSubpasses=&sub,.dependencyCount=2,.pDependencies=dependencies};
    CHECK(vkCreateRenderPass(device,&rpci,NULL,&pass));
    VkFramebufferCreateInfo fbci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=2,.pAttachments=views,.width=SIDE,.height=SIDE,.layers=1};
    CHECK(vkCreateFramebuffer(device,&fbci,NULL,&framebuffers[0]));
    if (array) {
        const VkImageView second_attachments[]={second_view,views[1]};
        fbci.pAttachments=second_attachments;
        CHECK(vkCreateFramebuffer(device,&fbci,NULL,&framebuffers[1]));
    }
    CHECK(shader(device,"msaa_split.vert.spv",&modules[0])); CHECK(shader(device,"msaa_split.frag.spv",&modules[1]));
    VkPushConstantRange push={VK_SHADER_STAGE_FRAGMENT_BIT,0,4};
    VkPipelineLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    CHECK(vkCreatePipelineLayout(device,&lci,NULL,&layout));
    if (array) {
        CHECK(shader(device,"msaa_split_read.comp.spv",&modules[2]));
        const VkDescriptorSetLayoutBinding bindings[]={
            {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL}};
        VkDescriptorSetLayoutCreateInfo sci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
        CHECK(vkCreateDescriptorSetLayout(device,&sci,NULL,&set_layout));
        VkPushConstantRange cp={VK_SHADER_STAGE_COMPUTE_BIT,0,4};
        VkPipelineLayoutCreateInfo clci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&cp};
        CHECK(vkCreatePipelineLayout(device,&clci,NULL,&compute_layout));
        VkComputePipelineCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=compute_layout,
            .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=modules[2],.pName="main"}};
        CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cpi,NULL,&compute_pipeline));
        const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
        VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
        CHECK(vkCreateDescriptorPool(device,&dpci,NULL,&descriptor_pool));
        VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
        CHECK(vkAllocateDescriptorSets(device,&dsai,&descriptor_set));
        VkDescriptorImageInfo image_info={.imageView=sample_view,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo buffer_info={readback,4*PIXELS*sizeof(float),2*PIXELS*9*sizeof(float)};
        VkWriteDescriptorSet writes[]={
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptor_set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,.pImageInfo=&image_info},
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptor_set,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffer_info}};
        vkUpdateDescriptorSets(device,2,writes,0,NULL);
    }
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={0,0,SIDE,SIDE,0,1}; VkRect2D scissor={{0,0},{SIDE,SIDE}};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1};
    VkPipelineColorBlendAttachmentState blend={.colorWriteMask=VK_COLOR_COMPONENT_R_BIT};
    VkPipelineColorBlendStateCreateInfo blends={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
    for (unsigned group=0;group<2;++group) {
        VkSampleLocationEXT points[4]; for (unsigned i=0;i<4;++i) points[i]=(VkSampleLocationEXT){positions[4*group+i][0]/16.0f,positions[4*group+i][1]/16.0f};
        VkPipelineSampleLocationsStateCreateInfoEXT locations={.sType=VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT,.sampleLocationsEnable=VK_TRUE,
            .sampleLocationsInfo={.sType=VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT,.sampleLocationsPerPixel=VK_SAMPLE_COUNT_4_BIT,.sampleLocationGridSize={1,1},.sampleLocationsCount=4,.pSampleLocations=points}};
        VkPipelineMultisampleStateCreateInfo multi={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.pNext=&locations,.rasterizationSamples=VK_SAMPLE_COUNT_4_BIT};
        VkGraphicsPipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vertex,
            .pInputAssemblyState=&assembly,.pViewportState=&vp,.pRasterizationState=&raster,.pMultisampleState=&multi,.pColorBlendState=&blends,.layout=layout,.renderPass=pass};
        CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipelines[group]));
    }
    VkCommandPoolCreateInfo poolci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    CHECK(vkCreateCommandPool(device,&poolci,NULL,&pool)); VkCommandBuffer commands;
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    CHECK(vkAllocateCommandBuffers(device,&cai,&commands));
    VkCommandBufferBeginInfo cbi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(commands,&cbi));
    for (uint32_t mode=0;mode<2;++mode) {
      for (unsigned group=0;group<2;++group) {
        VkClearValue clear={.color={{0,0,0,0}}};
        VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=framebuffers[array?group:0],
            .renderArea={{0,0},{SIDE,SIDE}},.clearValueCount=1,.pClearValues=&clear};
        vkCmdBeginRenderPass(commands,&begin,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[group]);
        vkCmdPushConstants(commands,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,4,&mode); vkCmdDraw(commands,3,1,0,0); vkCmdEndRenderPass(commands);
        VkBufferImageCopy region={.bufferOffset=(mode*2+group)*PIXELS*sizeof(float),.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={SIDE,SIDE,1}};
        vkCmdCopyImageToBuffer(commands,images[1],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback,1,&region);
      }
      if (array) {
        vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,compute_pipeline);
        vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,compute_layout,0,1,&descriptor_set,0,NULL);
        vkCmdPushConstants(commands,compute_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&mode);
        vkCmdDispatch(commands,SIDE/8,SIDE/8,1);
      }
    }
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|(array?VK_ACCESS_SHADER_WRITE_BIT:0),.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT|(array?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:0),VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(commands)); VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; CHECK(vkCreateFence(device,&fci,NULL,&fence));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&commands}; CHECK(vkQueueSubmit(queue,1,&submit,fence));
    CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,UINT64_C(10000000000)));
    CHECK(vkMapMemory(device,readback_memory,0,VK_WHOLE_SIZE,0,&mapped));
    unsigned errors=0,partial=0,group_errors=0,sample_errors=0,gpu_resolve_errors=0;
    for (unsigned y=0;y<SIDE;++y) for (unsigned x=0;x<SIDE;++x) {
        const int covered=coverage(x,y,0,8); if (covered<0) {puts("[msaa-split] ambiguous reference edge");goto cleanup;}
        partial+=covered>0 && covered<8;
        for (unsigned mode=0;mode<2;++mode) {
            float *data=mapped; const unsigned index=y*SIDE+x;
            const float actual=(data[(mode*2)*PIXELS+index]+data[(mode*2+1)*PIXELS+index])*0.5f;
            const float center=mode==0?1.0f:(x+0.5f+2*(y+0.5f))/64.0f;
            const float expected=(covered/8.0f)*center;
            if (array) {
                const unsigned base=4*PIXELS+(mode*PIXELS+index)*9;
                for (unsigned sample=0;sample<8;++sample) {
                    const float value=data[base+sample];
                    const float reference=coverage(x,y,sample,1)*center;
                    if (!isfinite(value) || fabsf(value-reference)>0.00001f) {
                        if (sample_errors<8) printf("[msaa-split] sample mismatch x=%u y=%u mode=%u sample=%u actual=%.9g expected=%.9g\n",x,y,mode,sample,value,reference);
                        ++sample_errors;
                    }
                }
                if (!isfinite(data[base+8]) || fabsf(data[base+8]-expected)>0.00001f) ++gpu_resolve_errors;
            }
            for (unsigned group=0;group<2;++group) {
                const float value=data[(mode*2+group)*PIXELS+index];
                const float reference=(coverage(x,y,group*4,4)/4.0f)*center;
                if (!isfinite(value) || fabsf(value-reference)>0.00001f) ++group_errors;
            }
            if (!isfinite(actual) || fabsf(actual-expected)>0.00001f) {
                if (errors<8) printf("[msaa-split] mismatch x=%u y=%u mode=%u actual=%.9g expected=%.9g\n",x,y,mode,actual,expected);
                ++errors;
            }
        }
    }
    printf("[msaa-split] pixels=%u checked_values=%u checked_group_values=%u partial_coverage_pixels=%u errors=%u group_errors=%u\n",
           PIXELS,2*PIXELS,4*PIXELS,partial,errors,group_errors);
    if (array) printf("[msaa-split] sample_value_checks=%u sample_errors=%u gpu_resolve_checks=%u gpu_resolve_errors=%u\n",
                      2*PIXELS*8,sample_errors,2*PIXELS,gpu_resolve_errors);
    result=errors || group_errors || sample_errors || gpu_resolve_errors?1:0;
cleanup:
    if (device) vkDeviceWaitIdle(device);
    if (mapped) vkUnmapMemory(device,readback_memory);
    if (fence) vkDestroyFence(device,fence,NULL);
    if (pool) vkDestroyCommandPool(device,pool,NULL);
    if (compute_pipeline) vkDestroyPipeline(device,compute_pipeline,NULL);
    for (unsigned i=0;i<2;++i) if(pipelines[i])vkDestroyPipeline(device,pipelines[i],NULL);
    for (unsigned i=0;i<3;++i) if(modules[i])vkDestroyShaderModule(device,modules[i],NULL);
    if (descriptor_pool) vkDestroyDescriptorPool(device,descriptor_pool,NULL);
    if (compute_layout) vkDestroyPipelineLayout(device,compute_layout,NULL);
    if (set_layout) vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    if (layout) vkDestroyPipelineLayout(device,layout,NULL);
    for (unsigned i=0;i<2;++i) if(framebuffers[i])vkDestroyFramebuffer(device,framebuffers[i],NULL);
    if (pass) vkDestroyRenderPass(device,pass,NULL);
    if (readback) vkDestroyBuffer(device,readback,NULL);
    if (readback_memory) vkFreeMemory(device,readback_memory,NULL);
    if (sample_view) vkDestroyImageView(device,sample_view,NULL);
    if (second_view) vkDestroyImageView(device,second_view,NULL);
    for(unsigned i=0;i<2;++i) {if(views[i])vkDestroyImageView(device,views[i],NULL);if(images[i])vkDestroyImage(device,images[i],NULL);if(image_memory[i])vkFreeMemory(device,image_memory[i],NULL);}
    if (device) vkDestroyDevice(device,NULL);
    if(instance) vkDestroyInstance(instance,NULL);
    printf("[msaa-split] exit=%d scope=%s\n",result,
           array?"synthetic_sample_preservation_gpu_resolve_and_center_interpolation":"synthetic_coverage_and_center_interpolation_only"); fflush(stdout);
    return result;
}
