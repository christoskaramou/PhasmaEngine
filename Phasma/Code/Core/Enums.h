#pragma once

namespace pe
{
    class FlagsBase
    {
    };

    template <class T, class = typename std::enable_if_t<std::is_enum_v<T>>>
    class Flags : public FlagsBase
    {
    public:
        using Type = uint64_t;

        Flags() : m_value(0) {}
        Flags(T value) : m_value(static_cast<Type>(value)) {}

        operator bool() const { return m_value != 0; }
        bool operator!() const { return m_value == 0; }

        bool operator==(T other) const { return m_value == static_cast<Type>(other); }
        bool operator==(const Flags &other) const { return m_value == other.m_value; }

        bool operator!=(T other) const { return m_value != static_cast<Type>(other); }
        bool operator!=(const Flags &other) const { return m_value != other.m_value; }

        void operator=(T other) { m_value = static_cast<Type>(other); }
        void operator=(const Flags &other) { m_value = other.m_value; }

        Flags operator|(T other) const { return Flags(m_value | static_cast<Type>(other)); }
        Flags operator|(const Flags &other) const { return Flags(m_value | other.m_value); }

        void operator|=(T other) { m_value |= static_cast<Type>(other); }
        void operator|=(const Flags &other) { m_value |= other.m_value; }

        Flags operator&(T other) const { return Flags(m_value & static_cast<Type>(other)); }
        Flags operator&(const Flags &other) const { return Flags(m_value & other.m_value); }

        void operator&=(T other) { m_value &= static_cast<Type>(other); }
        void operator&=(const Flags &other) { m_value &= other.m_value; }

        Flags operator^(T other) const { return Flags(m_value ^ static_cast<Type>(other)); }
        Flags operator^(const Flags &other) const { return Flags(m_value ^ other.m_value); }

        void operator^=(T other) { m_value ^= static_cast<Type>(other); }
        void operator^=(const Flags &other) { m_value ^= other.m_value; }

        Flags operator<<(unsigned int shift) const { return Flags(m_value << shift); }
        Flags operator>>(unsigned int shift) const { return Flags(m_value >> shift); }

        void operator<<=(unsigned int shift) { m_value <<= shift; }
        void operator>>=(unsigned int shift) { m_value >>= shift; }

        Flags operator~() const { return Flags(~m_value); }

        Type Value() const { return m_value; }
        const Type Value() { return m_value; }

    private:
        // In class use only, for casting Type to Flags
        Flags(Type value) : m_value(value) {}

        Type m_value;
    };

#define DEFINE_FLAGS_OPERATORS(Enum)                                                                    \
    inline Flags<##Enum> operator|(##Enum a, ##Enum b) { return Flags<##Enum>(a) | b; }                 \
    inline Flags<##Enum> operator&(##Enum a, ##Enum b) { return Flags<##Enum>(a) & b; }                 \
    inline Flags<##Enum> operator^(##Enum a, ##Enum b) { return Flags<##Enum>(a) ^ b; }                 \
    inline Flags<##Enum> operator<<(##Enum a, unsigned int shift) { return Flags<##Enum>(a) << shift; } \
    inline Flags<##Enum> operator>>(##Enum a, unsigned int shift) { return Flags<##Enum>(a) >> shift; } \
    inline Flags<##Enum> operator|(##Enum a, const Flags<##Enum> &b) { return Flags<##Enum>(a) | b; }   \
    inline Flags<##Enum> operator&(##Enum a, const Flags<##Enum> &b) { return Flags<##Enum>(a) & b; }   \
    inline Flags<##Enum> operator^(##Enum a, const Flags<##Enum> &b) { return Flags<##Enum>(a) ^ b; }   \
    inline Flags<##Enum> operator~(##Enum a) { return ~Flags<##Enum>(a); }

    // Vulkan template specializations in Code\Renderer\Vulkan\HelpersVK.cpp
    template <class T, class U>
    T Translate(U u);

    template <class T, class U>
    U GetFlags(T flags, std::unordered_map<T, U> &translator)
    {
        if (!flags)
            return U{};

        auto it = translator.find(flags);
        if (it == translator.end())
        {
            // Emplace the new flags pair into translator

            // template< class... Args >
            // std::pair<iterator,bool> emplace( Args&&... args );
            it = translator.emplace(flags, U{}).first;

            // Check each existing flag to see if it is included in the new flags
            for (auto &[existing_flags, value] : translator)
            {
                if ((flags & existing_flags) == existing_flags)
                    it->second |= value;
            }
        }

        return it->second;
    }

    template <class U, size_t N>
    U GetFlags(uint64_t flags, const U (&translator)[N])
    {
        uint64_t result = 0;

        if (!flags)
            return (U)result;

        for (size_t i = 0; i < N; ++i)
        {
            if ((flags & (1ULL << i)) == (1ULL << i))
                result = result | (uint64_t)translator[i];
        }

        return (U)result;
    }

    enum class Launch
    {
        Async,
        AsyncDeferred,
        AsyncNoWait, // Does not block the main thread, useful for loading.
        Sync,
        SyncDeferred,
        All, // Used for convenience to call all the above in one go.
    };

    enum class ShaderStage
    {
        VertexBit = 1 << 0,
        TesslationControlBit = 1 << 1,
        TesslationEvaluationBit = 1 << 2,
        GeometryBit = 1 << 3,
        FragmentBit = 1 << 4,
        ComputeBit = 1 << 5,
        AllGraphics = 1 << 6,
        All = 1 << 7,
        RaygenBit = 1 << 8,
        AnyHitBit = 1 << 9,
        ClosestHitBit = 1 << 10,
        MissBit = 1 << 11,
        IntersectionBit = 1 << 12,
        CallableBit = 1 << 13,
    };
    using ShaderStageFlags = Flags<ShaderStage>;
    DEFINE_FLAGS_OPERATORS(ShaderStage)

    enum class AnimationPathType
    {
        TRANSLATION,
        ROTATION,
        SCALE,
    };

    enum class AnimationInterpolationType
    {
        LINEAR,
        STEP,
        CUBICSPLINE,
    };

    enum class MaterialType
    {
        BaseColor,
        MetallicRoughness,
        Normal,
        Occlusion,
        Emissive,
    };

    // It is invalid to have both 'matrix' and any of 'translation'/'rotation'/'scale'
    //   spec: "A node can have either a 'matrix' or any combination of 'translation'/'rotation'/'scale' (TRS) properties"
    enum class TransformationType
    {
        Identity,
        Matrix,
        TRS,
    };

    enum class BarrierType
    {
        Memory,
        Buffer,
        Image,
    };

    enum class ImageLayout
    {
        Undefined,
        General,
        ShaderReadOnly,
        TransferSrc,
        TransferDst,
        Preinitialized,
        DepthAttachmentStencilReadOnly,
        DepthReadOnlyStencilAttachment,
        StencilAttachment,
        StencilReadOnly,
        ReadOnly,
        Attachment,
        PresentSrc,
        SharedPresent,
        FragmentDensityMap,
        FragmentShadingRateAttachment,
    };

    enum class CullMode
    {
        None = 0,
        Front = 1,
        Back = 2,
        FrontAndBack = 3,
    };

    enum class QueueType
    {
        GraphicsBit = 1 << 0,
        ComputeBit = 1 << 1,
        TransferBit = 1 << 2,
        SparseBindingBit = 1 << 3,
        ProtectedBit = 1 << 4,
        PresentBit = 1 << 5,
    };
    using QueueTypeFlags = Flags<QueueType>;
    DEFINE_FLAGS_OPERATORS(QueueType)

    enum class CameraDirection
    {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT,
    };

    enum class RenderType
    {
        Opaque = 1,
        AlphaCut = 2,
        AlphaBlend = 3,
    };

    enum class ReflectionVariableType
    {
        None,
        SInt,
        UInt,
        SFloat,
    };

    enum class Filter
    {
        Nearest,
        Linear,
        Cubic,
    };

    enum class ImageAspect
    {
        ColorBit = 1 << 0,
        DepthBit = 1 << 1,
        StencilBit = 1 << 2,
    };
    using ImageAspectFlags = Flags<ImageAspect>;
    DEFINE_FLAGS_OPERATORS(ImageAspect)

    enum class SamplerMipmapMode
    {
        Nearest,
        Linear,
    };

    enum class SamplerAddressMode
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge,
        ClampToBorder,
        MirrorClampToEdge,
    };

    enum class CompareOp
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    enum class BorderColor
    {
        FloatTransparentBlack,
        IntTransparentBlack,
        FloatOpaqueBlack,
        IntOpaqueBlack,
        FloatOpaqueWhite,
        IntOpaqueWhite,
        FloatCustom,
        IntCustom,
    };

    enum class ImageType
    {
        Type1D,
        Type2D,
        Type3D,
    };

    enum class ImageViewType
    {
        Type1D,
        Type2D,
        Type3D,
        TypeCube,
        Type1DArray,
        Type2DArray,
        TypeCubeArray,
    };

    enum class ImageTiling
    {
        Optimal,
        Linear,
    };

    enum class MemoryProperty
    {
        None = 0,
        DeviceLocalBit = 1 << 0,
        HostVisibleBit = 1 << 1,
        HostCoherentBit = 1 << 2,
        HostCachedBit = 1 << 3,
        LazyAllocBit = 1 << 4,
        ProtectedBit = 1 << 5,
    };
    using MemoryPropertyFlags = Flags<MemoryProperty>;
    DEFINE_FLAGS_OPERATORS(MemoryProperty)

    enum class Format
    {
        Undefined,

        R8SInt,
        R8UInt,
        R8Unorm,
        RG8Unorm,
        RGB8Unorm,
        RGBA8Unorm,

        R16SInt,
        R16UInt,
        R16SFloat,
        R16Unorm,
        RG16SFloat,
        RGB16SInt,
        RGB16UInt,
        RGB16SFloat,
        RGBA16UInt,
        RGBA16SFloat,

        R32SInt,
        R32UInt,
        R32SFloat,
        RG32SInt,
        RG32UInt,
        RG32SFloat,

        RGB32SInt,
        RGB32UInt,
        RGB32SFloat,

        RGBA32SInt,
        RGBA32UInt,
        RGBA32SFloat,

        BGRA8Unorm,

        D24UnormS8UInt,

        D32SFloat,
        D32SFloatS8UInt,

        S8UInt,
    };

    enum class SharingMode
    {
        Exclusive,
        Concurrent,
    };

    enum class SampleCount
    {
        Count1,
        Count2,
        Count4,
        Count8,
        Count16,
        Count32,
        Count64,
    };

    enum class ImageUsage
    {
        None = 0,
        TransferSrcBit = 1 << 0,
        TransferDstBit = 1 << 1,
        SampledBit = 1 << 2,
        StorageBit = 1 << 3,
        ColorAttachmentBit = 1 << 4,
        DepthStencilAttachmentBit = 1 << 5,
        TransientAttachmentBit = 1 << 6,
        InputAttachmentBit = 1 << 7,
    };
    using ImageUsageFlags = Flags<ImageUsage>;
    DEFINE_FLAGS_OPERATORS(ImageUsage)

    enum class ImageCreate
    {
        None = 0,
        SparceBindingBit = 1 << 0,
        SparceResidencyBit = 1 << 1,
        SparceAliasedBit = 1 << 2,
        MutableFormatBit = 1 << 3,
        CubeCompatibleBit = 1 << 4,
        AliasBit = 1 << 5,
        SplitInstanceBindRegionsBit = 1 << 6,
        Array2DCompatibleBit = 1 << 7,
        BlockTexeViewCompatibleBit = 1 << 8,
        ExtendedUsageBit = 1 << 9,
        ProtectedBit = 1 << 10,
        DisjointBit = 1 << 11,
        CornerSampledBit = 1 << 12,
        SampleLocationsCompatibleDepthBit = 1 << 13,
        SubsampledBit = 1 << 14,
        View2DCompatibleBit = 1 << 15,
    };
    using ImageCreateFlags = Flags<ImageCreate>;
    DEFINE_FLAGS_OPERATORS(ImageCreate)

    enum class CommandBufferUsage
    {
        OneTimeSubmitBit = 1 << 0,
        RenderPassContinueBit = 1 << 1,
        SimultaneousUseBit = 1 << 2,
    };
    using CommandBufferUsageFlags = Flags<CommandBufferUsage>;
    DEFINE_FLAGS_OPERATORS(CommandBufferUsage)

    enum class BufferUsage
    {
        None = 0,
        TransferSrcBit = 1 << 0,
        TransferDstBit = 1 << 1,
        UniformTexelBufferBit = 1 << 2,
        StorageTexelBufferBit = 1 << 3,
        UniformBufferBit = 1 << 4,
        StorageBufferBit = 1 << 5,
        IndexBufferBit = 1 << 6,
        VertexBufferBit = 1 << 7,
        IndirectBufferBit = 1 << 8,
        ShaderDeviceAddressBit = 1 << 9,
        TransformFeedbackBufferBit = 1 << 10,
        TransformFeedbackCounterBufferBit = 1 << 11,
        ConditionalRenderingBit = 1 << 12,
        AccelerationStructureBuildInputReadBit = 1 << 13,
        AccelerationStructureStorageBit = 1 << 14,
        ShaderBindingTableBit = 1 << 15,
    };
    using BufferUsageFlags = Flags<BufferUsage>;
    DEFINE_FLAGS_OPERATORS(BufferUsage)

    enum class ColorSpace
    {
        SrgbNonLinear,
        DisplayP3NonLinear,
        ExtendedSrgbLinear,
        DisplayP3Linear,
        DciP3NonLinear,
        BT709Linear,
        BT709NonLinear,
        BT2020Linear,
        HDR10St2084,
        DolbyVision,
        HDR10Hlg,
        AdobeRgbLinear,
        AdobeRgbNonLinear,
        PassThrough,
        ExtendedSrgbNonLinear,
    };

    enum class PresentMode
    {
        Immediate,
        Mailbox,
        Fifo,
        FifoRelaxed,
        SharedDemandRefresh,
        SharedContinuousRefresh,
    };

    enum class PipelineStage : uint64_t
    {
        None = 1 << 0,
        DrawIndirectBit = 1 << 1,
        VertexInputBit = 1 << 2,
        VertexShaderBit = 1 << 3,
        TessellationControlShaderBit = 1 << 4,
        TessellationEvaluationShaderBit = 1 << 5,
        GeometryShaderBit = 1 << 6,
        FragmentShaderBit = 1 << 7,
        EarlyFragmentTestsBit = 1 << 8,
        LateFragmentTestsBit = 1 << 9,
        ColorAttachmentOutputBit = 1 << 10,
        ComputeShaderBit = 1 << 11,
        TransferBit = 1 << 12,
        HostBit = 1 << 13,
        AllGraphicsBit = 1 << 14,
        AllCommandsBit = 1 << 15,
    };
    using PipelineStageFlags = Flags<PipelineStage>;
    DEFINE_FLAGS_OPERATORS(PipelineStage)

    enum class AttachmentLoadOp
    {
        Load,
        Clear,
        DontCare,
    };

    enum class AttachmentStoreOp
    {
        Store,
        DontCare,
    };

    enum class Access : uint64_t
    {
        None = 1 << 0,
        IndirectCommandReadBit = 1 << 1,
        IndexReadBit = 1 << 2,
        VertexAttributeReadBit = 1 << 3,
        UniformReadBit = 1 << 4,
        InputAttachmentReadBit = 1 << 5,
        ShaderReadBit = 1 << 6,
        ShaderWriteBit = 1 << 7,
        ColorAttachmentReadBit = 1 << 8,
        ColorAttachmentWriteBit = 1 << 9,
        DepthStencilAttachmentReadBit = 1 << 10,
        DepthStencilAttachmentWriteBit = 1 << 11,
        TransferReadBit = 1 << 12,
        TransferWriteBit = 1 << 13,
        HostReadBit = 1 << 14,
        HostWriteBit = 1 << 15,
        MemoryReadBit = 1 << 16,
        MemoryWriteBit = 1 << 17,
        TransformFeedbackWriteBit = 1 << 18,
        TransformFeedbackCounterReadBit = 1 << 19,
        TransformFeedbackCounterWriteBit = 1 << 20,
        ShaderSampledReadBit = 1 << 21,
        ShaderStorageReadBit = 1 << 22,
    };
    using AccessFlags = Flags<Access>;
    DEFINE_FLAGS_OPERATORS(Access)

    enum class ResourceAccess : uint64_t
    {
        Unused = 0,
        ReadOnly = 1 << 0,
        WriteOnly = 1 << 1,
        ReadWrite = ReadOnly | WriteOnly,
    };

    enum class AllocationCreate
    {
        None = 0,
        DedicatedMemoryBit = 1 << 0,
        NeverAllocateBit = 1 << 1,
        MappedBit = 1 << 2,
        UserDataCopyStringBit = 1 << 5,
        UpperAddressBit = 1 << 6,
        DontBindBit = 1 << 7,
        WithinBudgetBit = 1 << 8,
        CanAliasBit = 1 << 9,
        HostAccessSequentialWriteBit = 1 << 10,
        HostAccessRandomBit = 1 << 11,
        HostAccessAllowTransferInsteadBit = 1 << 12,
        StrategyMinMemoryBit = 1 << 16,
        StrategyMinTimeBit = 1 << 17,
        StrategyMinOffsetBit = 1 << 18,
        StrategyBestFitBit = StrategyMinMemoryBit,
        StrategyFirstFitBit = StrategyMinTimeBit,
        StrategyMaskBit = StrategyMinMemoryBit | StrategyMinTimeBit | StrategyMinOffsetBit,
    };
    using AllocationCreateFlags = Flags<AllocationCreate>;
    DEFINE_FLAGS_OPERATORS(AllocationCreate)

    enum class BlendFactor
    {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        ConstantAlpha,
        OneMinusConstantAlpha,
        SrcAlphaSaturate,
        Src1Color,
        OneMinusSrc1Color,
        Src1Alpha,
        OneMinusSrc1Alpha,
    };

    enum class BlendOp
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
        Zero,
        Src,
        Dst,
        SrcOver,
        DstOver,
        SrcIn,
        DstIn,
        SrcOut,
        DstOut,
        SrcAtop,
        DstAtop,
        Xor,
        Multiply,
        Screen,
        Overlay,
        Darken,
        Lighten,
        Colordodge,
        Colorburn,
        Hardlight,
        Softlight,
        Difference,
        Exclusion,
        Invert,
        InvertRgb,
        Lineardodge,
        Linearburn,
        Vividlight,
        Linearlight,
        Pinlight,
        Hardmix,
        HslHue,
        HslSaturation,
        HslColor,
        HslLuminosity,
        Plus,
        PlusClamped,
        PlusClampedAlpha,
        PlusDarker,
        Minus,
        MinusClamped,
        Contrast,
        InvertOvg,
        Red,
        Green,
        Blue,
    };

    enum class ColorComponent
    {
        RBit = 1 << 0,
        GBit = 1 << 1,
        BBit = 1 << 2,
        ABit = 1 << 3,
        RGBABit = RBit | GBit | BBit | ABit,
    };
    using ColorComponentFlags = Flags<ColorComponent>;
    DEFINE_FLAGS_OPERATORS(ColorComponent)

    enum class QueryType
    {
        Occlusion,
        PipelineStatistics,
        Timestamp,
        TransformFeedbackStream,
        PerformanceQueryKHR,
        AccelerationStructureCompactedSize,
        AccelerationStructureSerializationSize,
        PrimitivesGenerated,
    };

    enum class DynamicState
    {
        Viewport,
        Scissor,
        LineWidth,
        DepthBias,
        BlendConstants,
        DepthBounds,
        StencilCompareMask,
        StencilWriteMask,
        StencilReference,
        CullMode,
        FrontFace,
        PrimitiveTopology,
        ViewportWithCount,
        ScissorWithCount,
        VertexInputBindingStride,
        DepthTestEnable,
        DepthWriteEnable,
        DepthCompareOp,
        DepthBoundsTestEnable,
        StencilTestEnable,
        StencilOp,
        RasterizerDiscardEnable,
        DepthBiasEnable,
        PrimitiveRestartEnable,
        DiscardRectangle,
        SampleLocations,
        RayTracingPipelineStackSize,
        FragmentShadingRateKHR,
        LineStipple,
        VertexInput,
        PatchControlPoints,
        LogicOp,
        ColorWriteEnable,
    };

    enum class DescriptorType
    {
        Sampler,
        CombinedImageSampler,
        SampledImage,
        StorageImage,
        UniformTexelBuffer,
        StorageTexelBuffer,
        UniformBuffer,
        StorageBuffer,
        UniformBufferDynamic,
        StorageBufferDynamic,
        InputAttachment,
        InlineUniformBlock,
        AccelerationStructure,
    };

    enum class ObjectType
    {
        Unknown,
        Instance,
        PhysicalDevice,
        Device,
        Queue,
        Semaphore,
        CommandBuffer,
        Fence,
        DeviceMemory,
        Buffer,
        Image,
        Event,
        QueryPool,
        BufferView,
        ImageView,
        ShaderModule,
        PipelineCache,
        PipelineLayout,
        RenderPass,
        Pipeline,
        DescriptorSetLayout,
        Sampler,
        DescriptorPool,
        DescriptorSet,
        Framebuffer,
        CommandPool,
        SamplerYcbcrConversion,
        DescriptorUpdateTemplate,
        PrivateDataSlot,
        Surface,
        Swapchain,
        Display,
        DisplayMode,
        DebugReportCallback,
        DebugUtilsMessenger,
        AccelerationStructure,
        ValidationCache,
        DeferredOperation,
    };

    enum class VertexInputRate
    {
        Vertex,
        Instance,
    };

    enum class AttachmentDescription
    {
        None = 0,
        MayAlias = 1 << 0,
    };
    using AttachmentDescriptionFlags = Flags<AttachmentDescription>;
    DEFINE_FLAGS_OPERATORS(AttachmentDescription)

    enum class CommandPoolCreate
    {
        TransientBit = 1 << 0,
        ResetCommandBuffer = 1 << 1,
        Protected = 1 << 2,
    };
    using CommandPoolCreateFlags = Flags<CommandPoolCreate>;
    DEFINE_FLAGS_OPERATORS(CommandPoolCreate);

    enum class PrimitiveTopology
    {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        LineListWithAdjacency,
        LineStripWithAdjacency,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList,
    };

    enum class PolygonMode
    {
        Fill,
        Line,
        Point,
    };

    enum class BlendType
    {
        None,
        Opaque,
        Transparent
    };

    enum class CommandType
    {
        None = 0,
        GraphicsBit = 1 << 0,
        ComputeBit = 1 << 1,
        TransferBit = 1 << 2,
    };
    using CommandTypeFlags = Flags<CommandType>;
    DEFINE_FLAGS_OPERATORS(CommandType);

    enum class StencilOp
    {
        Keep,
        Zero,
        Replace,
        IncrementAndClamp,
        DecrementAndClamp,
        Invert,
        IncrementAndWrap,
        DecrementAndWrap,
    };

    enum class DependencyOp
    {
        DontCare,
        Wait,
    };
}
