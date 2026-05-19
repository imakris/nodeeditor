#pragma once

#include <functional>

#include "Definitions.hpp"
#include "HashUtils.hpp"

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
