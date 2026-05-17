#include "vulkan/vulkan.h"

#include <cstddef>
#include <vector>

namespace
{
    constexpr uint32_t kSpirvMagic = 0x07230203;
    constexpr uint32_t kOpCapability = 17;
    constexpr uint32_t kOpTypeImage = 25;
    constexpr uint32_t kImageFormatUnknown = 0;
    constexpr uint32_t kImageFormatRgba32f = 1;
    constexpr uint32_t kImageFormatR32f = 3;
    constexpr uint32_t kImageFormatRg32f = 6;
    constexpr uint32_t kImageSampledStorage = 2;
    constexpr uint32_t kCapabilityStorageImageWriteWithoutFormat = 56;

    uint32_t MakeInstruction(uint32_t wordCount, uint32_t opcode)
    {
        return (wordCount << 16u) | opcode;
    }

    bool ShouldPatchImageFormat(uint32_t imageFormat)
    {
        return imageFormat == kImageFormatRgba32f || imageFormat == kImageFormatR32f || imageFormat == kImageFormatRg32f;
    }

    bool PatchCacaoStorageImageFormats(const uint32_t *sourceWords, size_t wordCount, std::vector<uint32_t> &patchedWords)
    {
        if (!sourceWords || wordCount < 5 || sourceWords[0] != kSpirvMagic)
            return false;

        patchedWords.assign(sourceWords, sourceWords + wordCount);

        bool changed = false;
        bool hasWriteWithoutFormat = false;
        bool scanningCapabilities = true;
        size_t capabilityInsertPos = 5;

        for (size_t cursor = 5; cursor < patchedWords.size();)
        {
            const uint32_t instruction = patchedWords[cursor];
            const uint32_t instructionWordCount = instruction >> 16u;
            const uint32_t opcode = instruction & 0xffffu;

            if (instructionWordCount == 0 || cursor + instructionWordCount > patchedWords.size())
            {
                patchedWords.clear();
                return false;
            }

            if (opcode == kOpCapability && instructionWordCount >= 2)
            {
                const uint32_t capability = patchedWords[cursor + 1];
                hasWriteWithoutFormat |= capability == kCapabilityStorageImageWriteWithoutFormat;
                if (scanningCapabilities)
                    capabilityInsertPos = cursor + instructionWordCount;
            }
            else
            {
                scanningCapabilities = false;
            }

            if (opcode == kOpTypeImage && instructionWordCount >= 9)
            {
                const size_t sampledOperand = cursor + 7;
                const size_t imageFormatOperand = cursor + 8;
                if (patchedWords[sampledOperand] == kImageSampledStorage && ShouldPatchImageFormat(patchedWords[imageFormatOperand]))
                {
                    patchedWords[imageFormatOperand] = kImageFormatUnknown;
                    changed = true;
                }
            }

            cursor += instructionWordCount;
        }

        if (!changed)
        {
            patchedWords.clear();
            return false;
        }

        std::vector<uint32_t> missingCapabilities;
        if (!hasWriteWithoutFormat)
        {
            missingCapabilities.push_back(MakeInstruction(2, kOpCapability));
            missingCapabilities.push_back(kCapabilityStorageImageWriteWithoutFormat);
        }

        patchedWords.insert(patchedWords.begin() + static_cast<std::ptrdiff_t>(capabilityInsertPos),
                            missingCapabilities.begin(),
                            missingCapabilities.end());
        return true;
    }
} // namespace

extern "C" VKAPI_ATTR VkResult VKAPI_CALL PeCacaoVkCreateShaderModule(
    VkDevice device,
    const VkShaderModuleCreateInfo *createInfo,
    const VkAllocationCallbacks *allocator,
    VkShaderModule *shaderModule)
{
    if (!createInfo || !createInfo->pCode || createInfo->codeSize % sizeof(uint32_t) != 0)
        return vkCreateShaderModule(device, createInfo, allocator, shaderModule);

    std::vector<uint32_t> patchedWords;
    if (!PatchCacaoStorageImageFormats(createInfo->pCode, createInfo->codeSize / sizeof(uint32_t), patchedWords))
        return vkCreateShaderModule(device, createInfo, allocator, shaderModule);

    VkShaderModuleCreateInfo patchedCreateInfo = *createInfo;
    patchedCreateInfo.codeSize = patchedWords.size() * sizeof(uint32_t);
    patchedCreateInfo.pCode = patchedWords.data();
    return vkCreateShaderModule(device, &patchedCreateInfo, allocator, shaderModule);
}
