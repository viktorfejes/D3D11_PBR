#include "idmap.hpp"
#include <cstdlib>

static int entry_compare(const void *a, const void *b);
static void sort_idmap(IdMap *map);

void idmap::add(IdMap *map, uint8_t json_id, Id engine_id) {
    IdEntry entry = {json_id, engine_id};
    map->entries.push_back(entry);
    map->sorted = false;
}

Id idmap::get(IdMap *map, uint8_t json_id) {
    // Linear search for smaller maps
    if (map->entries.size() < 32) {
        for (size_t i = 0; i < map->entries.size(); ++i) {
            if (map->entries[i].json_id == json_id) {
                return map->entries[i].engine_id;
            }
        }
    } else {
        // Binary search for bigger maps
        sort_idmap(map);
        int low = 0;
        int high = map->entries.size() - 1;
        while (low <= high) {
            int mid = (low + high) / 2;
            int mid_id = map->entries[mid].json_id;
            if (mid_id == json_id) {
                return map->entries[mid].engine_id;
            }
            if (json_id < mid_id) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
    }

    // Something went wrong...
    return id::invalid();
}

static int entry_compare(const void *a, const void *b) {
    int a_id = reinterpret_cast<const IdEntry *>(a)->json_id;
    int b_id = reinterpret_cast<const IdEntry *>(b)->json_id;
    return (a_id > b_id) - (a_id < b_id);
}

static void sort_idmap(IdMap *map) {
    if (!map->sorted) {
        qsort(map->entries.data(), map->entries.size(), sizeof(IdEntry), entry_compare);
        map->sorted = true;
    }
}
