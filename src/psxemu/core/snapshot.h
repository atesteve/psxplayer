// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <type_traits>
#include <vector>
#include <utility>
#include <span>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <meta>
#include <algorithm>
#include <ranges>

#include <fmt/format.h>
#include <boost/align/aligned_allocator.hpp>

class SnapshotBuffer {
public:
    decltype(auto) operator[](this auto&& self, size_t p) { return self._storage[p]; }
    decltype(auto) data(this auto&& self) { return self._storage.data(); }
    bool empty() const { return _storage.empty(); }
    auto size() const { return _storage.size(); }
    void reserve(size_t new_cap) { _storage.reserve(new_cap); }
    auto capacity() const { return _storage.capacity(); }
    void shrink_to_fit() { _storage.shrink_to_fit(); }

    void clear()
    {
        _storage.clear();
        _decompressed_size = 0;
    }

    void push_back(std::byte b)
    {
        _storage.push_back(b);
    }

    void resize(size_t count)
    {
        _storage.resize(count);
    }

    SnapshotBuffer compress() const;
    SnapshotBuffer decompress() const;

    bool is_compressed() const {
        return _decompressed_size != 0;
    }

private:
    std::vector<std::byte, boost::alignment::aligned_allocator<std::byte, alignof(max_align_t)>>
        _storage;
    size_t _decompressed_size{};
};

template<typename T>
consteval auto remove_template_const()
{
    static constexpr auto t_dealias = dealias(^^T);
    if constexpr (has_template_arguments(t_dealias)) {
        static constexpr auto templ = template_of(t_dealias);
        auto template_arguments = template_arguments_of(t_dealias);
        for (auto& t : template_arguments) {
            if (!is_type(t)) {
                continue;
            }
            t = remove_const(t);
        }
        return substitute(templ, template_arguments);
    } else {
        return t_dealias;
    }
}

template<typename T>
consteval auto all_nonstatic_data_members_of() {
    static constexpr auto ctx = std::meta::access_context::unchecked();
    auto members = nonstatic_data_members_of(^^T, ctx);
    template for (constexpr auto const& b : define_static_array(bases_of(^^T, ctx))) {
        using Base = [:type_of(b):];
        auto base_members = all_nonstatic_data_members_of<Base>();
        members.append_range(base_members);
    }
    return members;
}

template<typename T>
consteval bool is_trivially_copyable()
{
    if constexpr (!std::is_trivially_copyable_v<T>) {
        return false;
    }
    else if constexpr (!std::is_class_v<T>) {
        return true;
    } else {
        auto const member_types = std::views::all(all_nonstatic_data_members_of<T>())
            | std::views::transform(std::meta::type_of);
        return std::ranges::none_of(member_types, std::meta::is_reference_type);
    }
}

template<typename T>
concept trivially_copiable = is_trivially_copyable<T>();

template<typename T>
concept nontrivial_container = !trivially_copiable<T> && requires(T&& c) { c.begin(); };

template<typename T>
concept nontrivial_bindable = !trivially_copiable<T> && !nontrivial_container<T>
    && (std::is_aggregate_v<T> || requires() { std::tuple_size<T>{}; });

struct SnapshotSerializer {
    void serialize(std::span<uint8_t> span)
    {
        auto* ptr = allocate_bytes(span.size());
        memcpy(ptr, span.data(), span.size());
    }

    template<trivially_copiable T>
    void serialize(T const& o)
    {
        auto* ptr = allocate<T>();
        memcpy(ptr, &o, sizeof(o));
    }

    template<nontrivial_container T>
    void serialize(T const& c)
    {
        size_t const size = c.size();
        serialize(size);
        for (auto&& e : c) {
            serialize(e);
        }
    }

    template<nontrivial_bindable T>
    void serialize(T const& c)
    {
        auto const& [... members] = c;
        template for (auto const& m : {members...})
        {
            serialize(m);
        }
    }

    SnapshotBuffer* _b;

private:
    template<typename T>
    std::byte* allocate()
    {
        if (_b->empty()) {
            _b->resize(sizeof(T));
            return _b->data();
        }
        auto const misalignment = _b->size() % alignof(T);
        if (misalignment != 0) {
            _b->resize(_b->size() + (alignof(T) - misalignment));
        }
        _b->resize(_b->size() + sizeof(T));
        return &(*_b)[_b->size() - sizeof(T)];
    }

    std::byte* allocate_bytes(size_t n)
    {
        if (_b->empty()) {
            _b->resize(n);
            return _b->data();
        }
        _b->resize(_b->size() + n);
        return &(*_b)[_b->size() - n];
    }
};

struct SnapshotDeserializer {
    void deserialize(std::span<uint8_t> span)
    {
        auto* ptr = advance_ptr(span.size());
        memcpy(span.data(), ptr, span.size());
    }

    template<trivially_copiable T>
    void deserialize(T& o)
    {
        auto const* ptr = advance_ptr<T>();
        memcpy(&o, ptr, sizeof(o));
    }

    template<nontrivial_container T>
    void deserialize(T& c)
    {
        size_t n_elements;
        deserialize(n_elements);
        c.clear();
        auto it = std::inserter(c, c.begin());
        for (size_t i = 0; i < n_elements; i++) {
            using value_type = [:remove_template_const<typename T::value_type>():];
            value_type e;
            deserialize(e);
            *it = std::move(e);
        }
    }

    template<nontrivial_bindable T>
    void deserialize(T& c)
    {
        auto& [... members] = c;
        template for (auto& m : {members...})
        {
            deserialize(m);
        }
    }

    SnapshotBuffer const* _b;
    size_t _p{};

private:
    template<typename T>
    std::byte const* advance_ptr()
    {
        auto const misalignment = _p % alignof(T);
        if (misalignment != 0) {
            _p += alignof(T) - misalignment;
        }
        auto* ptr = &(*_b)[_p];
        _p += sizeof(T);
        return ptr;
    }

    std::byte const* advance_ptr(size_t n)
    {
        auto* ptr = &(*_b)[_p];
        _p += n;
        return ptr;
    }
};
