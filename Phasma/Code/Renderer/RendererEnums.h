/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
#define FLAGS_OPERATORS(BitType, FlagsType)                 \
	inline FlagsType operator|(BitType bit0, BitType bit1)  \
	{ return FlagsType(bit0) | bit1; }                      \
	inline FlagsType operator&(BitType bit0, BitType bit1)  \
	{ return FlagsType(bit0) & bit1; }
	
	template<class T>
	class Flags
	{
	public:
		using MaskType = typename std::underlying_type<T>::type;
		
		Flags()
			: m_mask(0)
		{}
		
		Flags(T bit)
			: m_mask(static_cast<MaskType>(bit))
		{}
		
		Flags(Flags<T> const& rhs) = default;
		
		Flags(MaskType flags)
			: m_mask(flags)
		{}
		
		Flags<T> operator&(Flags<T> const& rhs) const
		{
			return Flags<T>(m_mask & rhs.m_mask);
		}
		
		Flags<T> operator|(Flags<T> const& rhs) const
		{
			return Flags<T>(m_mask | rhs.m_mask);
		}
		
		explicit operator bool() const
		{
			return !!m_mask;
		}
		
		explicit operator MaskType() const
		{
			return m_mask;
		}
		
		Flags<T>& operator=(Flags<T> const& rhs) = default;
		
		Flags<T>& operator|=(Flags<T> const& rhs)
		{
			m_mask |= rhs.m_mask;
			return *this;
		}
		
		Flags<T>& operator&=(Flags<T> const& rhs)
		{
			m_mask &= rhs.m_mask;
			return *this;
		}
	
	private:
		MaskType m_mask;
	};
	
	enum class MemoryProperty
	{
		DeviceLocal     = 1<<0,
		HostVisible     = 1<<1,
		HostCoherent    = 1<<2,
		HostCached      = 1<<3
	};
	using MemoryPropertyFlags = Flags<MemoryProperty>;
	FLAGS_OPERATORS(MemoryProperty, MemoryPropertyFlags)
	
	enum class BufferUsage
	{
		TransferSrc         = 1<<0,
		TransferDst         = 1<<1,
		UniformTexelBuffer  = 1<<2,
		StorageTexelBuffer  = 1<<3,
		UniformBuffer       = 1<<4,
		StorageBuffer       = 1<<5,
		IndexBuffer         = 1<<6,
		VertexBuffer        = 1<<7
	};
	using BufferUsageFlags = Flags<BufferUsage>;
	FLAGS_OPERATORS(BufferUsage, BufferUsageFlags)
	
	enum class RenderQueue
	{
		Opaque = 1,
		AlphaCut = 2,
		AlphaBlend = 3
	};
}
