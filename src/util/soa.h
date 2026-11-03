// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <meta>
#include <cstddef>

template <size_t N>
struct transform_to_array {
    template <typename T>
    static consteval auto fn()
    {
        using ArrayType = T[N];
        return ^^ArrayType;
    }
};

struct transform_to_ref {
    template <typename T>
    static consteval auto fn()
    {
        using Ref = T&;
        return ^^Ref;
    }
};

struct transform_to_const_ref {
    template <typename T>
    static consteval auto fn()
    {
        using Ref = T const&;
        return ^^Ref;
    }
};

template <typename T, typename Strt, typename TransformFn>
consteval auto define()
{
    static constexpr auto access_level = std::meta::access_context::unprivileged();
    static constexpr auto SMembers = define_static_array(nonstatic_data_members_of(^^Strt, access_level));
    std::vector<std::meta::info> members;
    template for (constexpr auto member : SMembers) {
        using MemberT = [:type_of(member):];
        static constexpr auto transformed = TransformFn::template fn<MemberT>();
        members.push_back(data_member_spec(transformed, { .name = identifier_of(member), }));
    }
    return std::meta::define_aggregate(^^T, members);;
}

template <typename T, size_t N>
struct Fields {
    struct F;
    consteval {
        define<F,T,transform_to_array<N>>();
    };
};

template <typename T, size_t N>
struct SOA : public Fields<T,N>::F
{
    struct R;
    consteval {
        define<R,T,transform_to_ref>();
    };

    struct CR;
    consteval {
        define<CR,T,transform_to_const_ref>();
    };

    constexpr R operator[](size_t n) {
        auto& [...refs] = *this;
        return {refs[n]...};
    }

    constexpr CR operator[](size_t n) const {
        auto const& [...refs] = *this;
        return {refs[n]...};
    }

    constexpr size_t size() const {
        return N;
    }
};
