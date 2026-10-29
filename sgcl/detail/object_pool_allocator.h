//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "object_pool_allocator_base.h"

namespace sgcl::detail {
    template<class T>
    class ObjectPoolAllocator : public ObjectPoolAllocatorBase {
    public:
        using ValueType = typename TypeInfo<T>::Type;
        using IsPoolAllocator = std::true_type;

        ObjectPoolAllocator(BlockAllocator& a, std::atomic<Page*>& pages) noexcept
        : ObjectPoolAllocatorBase(a, pages, _pointer_pool, _pages_buffer) {
        }

        // Collector only.
        static void free(Page* pages) noexcept {
            _free(pages, _pages_buffer);
        }

    private:
        using PointerPool = detail::PointerPool<TypeInfo<T>::ObjectCount, sizeof(ValueType)>;
        PointerPool _pointer_pool;
        // Per image, like all per-type data: the collector frees a page through its own metadata.
        inline static PageBuffer _pages_buffer;

        Page* _create_page_parameters(DataPage* data) noexcept override {
            auto mem = checked_operator_new(TypeInfo<T>::HeaderSize);
            return new(mem) Page(data->block, (ValueType*)((char*)data + TypeInfo<T>::DataOffset));
        }
    };
}
