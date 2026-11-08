//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "array_base.h"
#include "child_pointers.h"
#include "spin_lock.h"

namespace sgcl::detail {
    // A type's partly free pages, shared by every thread. Mutators pop, the collector replaces the list; both
    // under the lock (a CAS pop is ABA-prone against the collector's re-sorting).
    struct PageBuffer {
        Page* head = {nullptr};
        SpinLock lock;
    };

    // One per managed type per image (PageInfo<T>::private_metadata); immutable except the page buffer and the
    // collector-only fields at the end. Everything a page and its allocator need of their type, so neither is a
    // template.
    struct Metadata {
        Metadata(ChildPointers& child_pointers, void (*destroy)(void*) noexcept, void (*finalize)(void*) noexcept,
                 void (*free)(Page*, Metadata&) noexcept, size_t object_size, unsigned object_count, bool is_array,
                 size_t data_offset, size_t header_size) noexcept
        : child_pointers(child_pointers)
        , destroy(destroy)
        , finalize(finalize)
        , free(free)
        , object_size(object_size)
        , object_count(object_count)
        , is_array(is_array)
        , data_offset(data_offset)
        , header_size(header_size) {
        }

        ChildPointers& child_pointers;
        void (*const destroy)(void*) noexcept;
        void (*const finalize)(void*) noexcept;
        // Collector only: releases the type's empty pages (PageBuffer for a pooled type).
        void (*const free)(Page*, Metadata&) noexcept;
        const size_t object_size;
        const unsigned object_count;
        const bool is_array;
        // A page's slots start at this offset of its data block; its header takes header_size bytes.
        const size_t data_offset;
        const size_t header_size;
        PageBuffer pages_buffer;
        Page* empty_page = {nullptr};
        Metadata* next = {nullptr};
        bool used = {false};
    };
}
