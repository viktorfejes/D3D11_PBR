#pragma once

#include "id.hpp"

// TODO: eliminate STL
#include <vector>

struct IdEntry {
    uint8_t json_id;
    Id engine_id;
};

struct IdMap {
    std::vector<IdEntry> entries;
    bool sorted;
};

namespace idmap {
void add(IdMap *map, uint8_t json_id, Id engine_id);
Id get(IdMap *map, uint8_t json_id);
} // namespace idmap
