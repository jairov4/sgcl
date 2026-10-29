//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "types.h"

namespace sgcl::detail {
    class ObjectAllocatorBase {
    public:
        explicit ObjectAllocatorBase(std::atomic<Page*>& pages) noexcept
        : _pages(pages) {
            collector_init();
        }

        virtual ~ObjectAllocatorBase() noexcept = default;

    protected:
        std::atomic<Page*>& _pages;
    };
}
