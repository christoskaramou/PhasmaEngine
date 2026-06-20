#include "API/QueryPool.h"
#include "API/QueryPool_Internal.h"

namespace pe
{
    QueryPool::QueryPool(const QueryPoolDesc &desc)
        : m_desc{desc}
    {
        m_impl = CreateQueryPoolImpl(desc);
        PE_ERROR_IF(!m_impl, "QueryPool: backend returned null Impl from CreateQueryPoolImpl");
        m_apiHandle = m_impl.get(); // mirror GpuTimer: expose the impl for PeHandle tracking
    }

    QueryPool::~QueryPool() = default;
} // namespace pe
