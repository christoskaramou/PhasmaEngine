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

#define DEFINE_FLAGS_OPERATORS(Enum)                                                              \
    inline Flags<Enum> operator|(Enum a, Enum b) { return Flags<Enum>(a) | b; }                   \
    inline Flags<Enum> operator&(Enum a, Enum b) { return Flags<Enum>(a) & b; }                   \
    inline Flags<Enum> operator^(Enum a, Enum b) { return Flags<Enum>(a) ^ b; }                   \
    inline Flags<Enum> operator<<(Enum a, unsigned int shift) { return Flags<Enum>(a) << shift; } \
    inline Flags<Enum> operator>>(Enum a, unsigned int shift) { return Flags<Enum>(a) >> shift; } \
    inline Flags<Enum> operator|(Enum a, const Flags<Enum> &b) { return Flags<Enum>(a) | b; }     \
    inline Flags<Enum> operator&(Enum a, const Flags<Enum> &b) { return Flags<Enum>(a) & b; }     \
    inline Flags<Enum> operator^(Enum a, const Flags<Enum> &b) { return Flags<Enum>(a) ^ b; }     \
    inline Flags<Enum> operator~(Enum a) { return ~Flags<Enum>(a); }

    template <class U, size_t N>
    U GetFlags(uint64_t flags, const U (&translator)[N])
    {
        static_assert(std::is_enum_v<U> || std::is_integral_v<U>, "U must be enum or integral");

        if (!flags)
            return static_cast<U>(0);

        uint64_t result = 0;
        for (size_t i = 0; i < N; ++i)
        {
            if (flags & (1ULL << i))
                result |= static_cast<uint64_t>(translator[i]);
        }

        return static_cast<U>(result);
    }

    enum class ShaderCodeType
    {
        HLSL,
        GLSL
    };

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

    enum class ResourceAccess : uint64_t
    {
        Unused = 0,
        ReadOnly = 1 << 0,
        WriteOnly = 1 << 1,
        ReadWrite = ReadOnly | WriteOnly,
    };

    enum class TextureType
    {
        BaseColor,
        MetallicRoughness,
        Normal,
        Occlusion,
        Emissive,
    };
}
