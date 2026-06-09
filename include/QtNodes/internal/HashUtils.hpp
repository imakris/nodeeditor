#pragma once

#include <cstddef>
#include <functional>

namespace QtNodes::detail {

/// Boost-style variadic hash combiner. Use to fold the hashes of several
/// fields into a single `std::size_t` seed:
///
///   std::size_t h = 0;
///   QtNodes::detail::hash_combine(h, a, b, c);
template<typename T, typename... Rest>
inline void hash_combine(std::size_t &seed, const T &v, Rest... rest)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    if constexpr (sizeof...(rest) > 0) {
        hash_combine(seed, rest...);
    }
}

} // namespace QtNodes::detail
