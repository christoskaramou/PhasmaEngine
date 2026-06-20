#pragma once

#include "API/QueryPool.h"

namespace pe
{
    // Backend implementations own the native query-pool resource and create/destroy
    // it in their ctor/dtor. CommandBuffer query methods reach the native handle via
    // the backend's From(QueryPool*) accessor.
    class QueryPool::Impl
    {
    public:
        virtual ~Impl() = default;
    };

    std::unique_ptr<QueryPool::Impl> CreateQueryPoolImpl(const QueryPoolDesc &desc);
} // namespace pe
