#include "id.hpp"

Id id::invalid() {
    Id id{INVALID_ID, 0};
    return id;
}

void id::invalidate(Id *id) {
    id->id = INVALID_ID;
    id->generation = 0;
}

void id::gen_increment(Id *id) {
    id->generation++;
}

bool id::is_valid(Id id) {
    return id.id != INVALID_ID;
}

bool id::is_invalid(Id id) {
    return id.id == INVALID_ID;
}

bool id::is_fresh(Id a, Id b) {
    return a.id == b.id && a.generation == b.generation;
}

bool id::is_stale(Id a, Id b) {
    return a.id != b.id || a.generation != b.generation;
}
