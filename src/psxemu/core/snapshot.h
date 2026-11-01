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

struct aligned_bytes_t {
    alignas(max_align_t) std::byte storage[alignof(max_align_t)];
};

class SnapshotBuffer {
    static constexpr auto bundle_size = sizeof(aligned_bytes_t);

public:
    decltype(auto) operator[](this auto&& self, size_t p)
    {
        return self._storage[p / bundle_size].storage[p % bundle_size];
    }

    decltype(auto) front(this auto&& self) { return self._storage[0].storage[0]; }

    decltype(auto) back(this auto&& self) { return self[self._size - 1]; }

    decltype(auto) data(this auto&& self) { return &self[0]; }

    decltype(auto) begin(this auto&& self) { return self.data(); }

    decltype(auto) cbegin(this auto const& self) { return self.begin(); }

    decltype(auto) end(this auto&& self) { return &self[self._size]; }

    decltype(auto) cend(this auto const& self) { return self.end(); }

    bool empty() const { return _size == 0; }

    auto size() const { return _size; }

    void reserve(size_t new_cap) { _storage.reserve(new_cap / bundle_size); }

    auto capacity() const { return _storage.capacity() * bundle_size; }

    void shrink_to_fit() { _storage.shrink_to_fit(); }

    void clear()
    {
        _storage.clear();
        _size = 0;
    }

    void push_back(std::byte b)
    {
        if (_size % bundle_size == 0) {
            _storage.emplace_back();
        }
        (*this)[_size] = b;
        _size++;
    }

    void resize(size_t count)
    {
        _storage.resize((count + bundle_size - 1) / bundle_size);
        _size = count;
    }

private:
    std::vector<aligned_bytes_t> _storage;
    size_t _size{};
};

consteval void decompose(auto&& c)
{
    auto const& [... m] = c;
}

template<typename T>
concept trivially_copiable = std::is_trivially_copyable_v<T>;

template<typename T>
concept nontrivial_container = !std::is_trivially_copyable_v<T> && requires(T&& c) { c.begin(); };

template<typename T>
concept nontrivial_bindable = !std::is_trivially_copyable_v<T> && !nontrivial_container<T>
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
            return _b->begin();
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
            return _b->begin();
        }
        _b->resize(_b->size() + n);
        return &(*_b)[_b->size() - n];
    }
};

template<typename T>
auto consteval remove_template_const()
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
