// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utility>
#include <optional>
#include <concepts>

template<std::invocable<> F>
class [[nodiscard]] ScopeGuard {
public:
    template<typename FF>
    explicit constexpr ScopeGuard(FF&& f) noexcept
        : _fn{std::forward<FF>(f)}
    {}

    constexpr void dismiss() noexcept { _fn.reset(); }

    constexpr ~ScopeGuard() noexcept
    {
        if (_fn) {
            (*_fn)();
        }
    }

private:
    std::optional<F> _fn;
};

template<typename F>
ScopeGuard(F&&) -> ScopeGuard<F>;
