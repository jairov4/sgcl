//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "block_allocator.h"
#include "fatal.h"
#include "object_allocator_base.h"
#include "pointer_pool_base.h"

namespace sgcl::detail {
    // One thread's allocator of one pooled type (PageInfo<T>::Pooled). Not a template: everything it needs of the
    // type is in the type's Metadata, so the code is shared by every type.
    class ObjectPoolAllocator : public ObjectAllocatorBase {
    public:
        using IsPoolAllocator = std::true_type;

        // Created and destroyed by the owner (sgcl/state_impl.h), so its allocation and deallocation pair up in one
        // image whichever image first allocated the type on this thread.
        SGCL_API static ObjectPoolAllocator* create(BlockAllocator& ba, std::atomic<Page*>& pages, Metadata& metadata) noexcept;

        SGCL_API ObjectPoolAllocator(BlockAllocator& ba, std::atomic<Page*>& pages, Metadata& metadata) noexcept;

        // Collector only.
        SGCL_API static void free(Page* pages, Metadata& metadata) noexcept;

        // At thread exit: return the slots still in the pool.
        SGCL_API ~ObjectPoolAllocator() noexcept override;

        void* alloc() noexcept {
            if (_pointer_pool.is_empty()) [[unlikely]] {
                _refill();
            }
            assert(!_pointer_pool.is_empty());
            return _pointer_pool.alloc();
        }

        // Refills the empty pool from a partly free page of the type, or a new one.
        SGCL_API void _refill() noexcept;

    private:
        BlockAllocator& _block_allocator;
        Metadata& _metadata;
        PointerPoolBase _pointer_pool;
        PageBuffer& _pages_buffer;
        Page* _current_page = {nullptr};

        SGCL_API Page* _create_page_parameters(DataPage* data) noexcept;

        SGCL_API Page* _alloc_page() noexcept;

    protected:
        // Collector only: moves the pages whose slots are all free from `pages` to `empty_pages`.
        SGCL_API static void _remove_empty(Page*& pages, Page*& empty_pages) noexcept;

        SGCL_API static void _free(Page* pages) noexcept;

        // Collector only: merges `pages` into the buffer, frees the ones that became entirely free.
        SGCL_API static void _free(Page* pages, PageBuffer& pages_buffer) noexcept;
    };
}
