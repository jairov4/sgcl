//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "../config.h"
#include "data_page.h"
#include "fatal.h"
#include <new>

namespace sgcl::detail {
    struct Block {
        static constexpr size_t PageCount = 15;

        Block() noexcept {
            DataPage* data = (DataPage*)(this + 1);
            for (size_t i = 0; i < PageCount; ++i) {
                data[i].block = this;
            }
        }

        static void* operator new(size_t) noexcept {
            constexpr size_t size = sizeof(void*) + sizeof(Block) + sizeof(DataPage) * (PageCount + 1);
            void* mem = checked_malloc(size);
            uintptr_t addres = (uintptr_t)mem + sizeof(void*) + sizeof(Block) + config::PageSize;
            addres = addres & ~(config::PageSize - 1);
            Block* block = (Block*)addres - 1;
            *((void**)block - 1) = mem;
            return block;
        }

        static void operator delete(void* p, size_t) noexcept {
            std::free(*((void**)p - 1));
        }

        Block* next = {nullptr};
        unsigned page_count = {0};
    };
}
