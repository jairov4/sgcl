//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "detail/unique_ptr.h"
#include "types.h"

#include <type_traits>

namespace sgcl {
    // The owning handle make_tracked returns (SPEC.md §3.2).
    template<class T>
    class unique_ptr : public detail::UniquePtr<T> {
        using Base = detail::UniquePtr<T>;

    public:
        using element_type = T;

        constexpr unique_ptr() noexcept = default;

        constexpr unique_ptr(std::nullptr_t) noexcept
        : Base(nullptr) {
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        unique_ptr(detail::UniquePtr<U>&& p) noexcept
        : Base(static_cast<T*>(p.release())) {
        }

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        unique_ptr(unique_ptr<U>&& p) noexcept
        : Base(static_cast<T*>(p.release())) {
        }

        unique_ptr(unique_ptr&&) noexcept = default;
        unique_ptr& operator=(unique_ptr&&) noexcept = default;

        template<class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        unique_ptr& operator=(unique_ptr<U>&& p) noexcept {
            this->reset(static_cast<T*>(p.release()));
            return *this;
        }

        unique_ptr& operator=(std::nullptr_t) noexcept {
            this->reset();
            return *this;
        }
    };
}
