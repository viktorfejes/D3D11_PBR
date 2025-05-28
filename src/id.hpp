#pragma once

#include <cstdint>

#define INVALID_ID ((uint8_t)-1)

struct Id {
    uint8_t id;
    uint8_t generation;
};

namespace id {
Id invalid();
void invalidate(Id *id);
void gen_increment(Id *id);

bool is_valid(Id id);
bool is_invalid(Id id);
bool is_fresh(Id a, Id b);
bool is_stale(Id a, Id b);
} // namespace id
