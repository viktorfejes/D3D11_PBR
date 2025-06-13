#include "math.hpp"

#define FNV_PRIME_64 1099511628211ULL
#define FNV_OFFSET_64 14695981039346656037ULL

uint64_t hash_fnv1a_64(const void *key, size_t length) {
    uint64_t hash = FNV_OFFSET_64;
    const uint8_t *p = (const uint8_t *)key;
    const uint8_t *end = p + length;
    while (p < end) {
        hash ^= *p++;
        hash *= FNV_PRIME_64;
    }
    return hash;
}
