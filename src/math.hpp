#pragma once

#include <cstdint>

uint64_t hash_fnv1a_64(const void *key, uint64_t length);
