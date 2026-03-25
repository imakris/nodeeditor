#pragma once

#include <functional>

#include "Definitions.hpp"

template<typename T, typename... Rest>
inline void hash_combine(std::size_t &seed, const T &v, Rest... rest)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    if constexpr (sizeof...(rest) > 0) {
        hash_combine(seed, rest...);
    }
}

namespace std {
template<>
struct hash<QtNodes::ConnectionId>
{
    inline std::size_t operator()(QtNodes::ConnectionId const &id) const
    {
        std::size_t h = 0;
        hash_combine(h, id.outNodeId, id.outPortIndex, id.inNodeId, id.inPortIndex);
        return h;
    }
};

} // namespace std
