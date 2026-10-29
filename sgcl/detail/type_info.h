//------------------------------------------------------------------------------
// SGCL: Smart Garbage Collection Library
// Copyright (c) 2022-2025 Sebastian Nibisz
// SPDX-License-Identifier: Apache-2.0
//------------------------------------------------------------------------------
#pragma once

#include "page_info.h"

namespace sgcl::detail {
    template<class T>
    struct TypeInfo : PageInfo<std::remove_cv_t<T>> {
        using Type = std::remove_cv_t<T>;
        using BaseType = std::remove_extent_t<Type>;
        static constexpr bool IsTracked = std::is_base_of_v<Tracked, BaseType>;
        static constexpr bool IsArray = std::is_base_of_v<ArrayBase, BaseType>;
        static constexpr bool MayContainTracked = detail::MayContainTracked<T>::value;
    };
}
