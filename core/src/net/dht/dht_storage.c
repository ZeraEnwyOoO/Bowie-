/*
 * Wingo — P2P Internet Sharing Tool (Repo: Bowie)
 * Copyright (C) 2024 ASBM Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * DHT storage implementation for Bowie.
 *
 * Storage stores info hash → peer mappings.
 *
 * Data structures:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    STORAGE                                  │
 *   │                                                             │
 *   │   Info Hash 1                                               │
 *   │   ├── Peer A (10.0.0.1:6881)                                │
 *   │   ├── Peer B (10.0.0.2:6881)                                │
 *   │   └── Peer C (10.0.0.3:6881)                                │
 *   │                                                             │
 *   │   Info Hash 2                                               │
 *   │   ├── Peer D (10.0.0.4:6881)                                │
 *   │   └── Peer E (10.0.0.5:6881)                                │
 *   │                                                             │
 *   │   ...                                                       │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_storage.h"
#include "wingo/log.h"
#include "wingo/util/time.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Peer entry.
 */
typedef struct peer_entry {
    wingo_addr_t           *addr;           /* Peer address */
    wingo_i64               announced_at;   /* When announced */
    wingo_i64               expires_at;     /* When to expire */
    struct peer_entry      *next;
} peer_entry_t;

/*
 * Info hash entry.
 */
typedef struct hash_entry {
    wingo_info_hash_t       infohash;       /* Info hash */
    peer_entry_t           *peers;          /* Peer list */
    wingo_size              peer_count;     /* Number of peers */
    wingo_i64               created_at;     /* When created */
    wingo_i64               last_update;    /* Last announcement */
    struct hash_entry      *next;           /* Next in bucket */
} hash_entry_t;

/*
 * Storage (concrete).
 */
struct wingo_dht_storage {
    /* ----- Hash table ----- */
    hash_entry_t          **buckets;
    wingo_size              bucket_count;
    wingo_size              hash_count;     /* Total info hashes */

    /* ----- Config ----- */
    wingo_size              max_hashes;
    wingo_size              max_peers_per_hash;
    wingo_i64               peer_ttl_s;
    wingo_i64               hash_ttl_s;

    /* ----- Statistics ----- */
    wingo_u64               announces;
    wingo_u64               lookups;
    wingo_u64               removes;
    wingo_u64               expires;
    wingo_i64               oldest_hash_created;
    wingo_i64               newest_hash_created;
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Initial bucket count for hash table.
 */
#define STORAGE_INIT_BUCKETS        64

/*
 * Maximum bucket count.
 */
#define STORAGE_MAX_BUCKETS         65536

/* ============================================================================
 * INTERNAL HELPERS — HASH TABLE
 * ============================================================================ */

/*
 * Hash an info hash to bucket index.
 *
 * Uses FNV-1a hash.
 */
static wingo_size hash_to_bucket(const wingo_info_hash_t *infohash,
                                  wingo_size bucket_count)
{
    const wingo_u8 *bytes = infohash->bytes;
    wingo_u32 hash = 2166136261U;  /* FNV offset basis */
    wingo_size i;

    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        hash ^= (wingo_u32)bytes[i];
        hash *= 16777619U;  /* FNV prime */
    }

    return (wingo_size)hash & (bucket_count - 1);
}

/*
 * Find hash entry by info hash.
 */
static hash_entry_t *storage_find_hash(wingo_dht_storage_t *storage,
                                        const wingo_info_hash_t *infohash)
{
    wingo_size bucket_idx;
    hash_entry_t *entry;

    if (storage == NULL || infohash == NULL) {
        return NULL;
    }

    bucket_idx = hash_to_bucket(infohash, storage->bucket_count);

    for (entry = storage->buckets[bucket_idx]; entry != NULL; entry = entry->next) {
        if (memcmp(entry->infohash.bytes, infohash->bytes,
                   WINGO_DHT_ID_SIZE) == 0) {
            return entry;
        }
    }

    return NULL;
}

/*
 * Create a new hash entry.
 */
static hash_entry_t *hash_entry_new(const wingo_info_hash_t *infohash)
{
    hash_entry_t *entry;

    entry = calloc(1, sizeof(hash_entry_t));
    if (entry == NULL) {
        return NULL;
    }

    memcpy(&entry->infohash, infohash, sizeof(wingo_info_hash_t));
    entry->peers = NULL;
    entry->peer_count = 0;
    entry->created_at = wingo_time_now();
    entry->last_update = entry->created_at;
    entry->next = NULL;

    return entry;
}

/*
 * Free a hash entry and its peers.
 */
static void hash_entry_free(hash_entry_t *entry)
{
    peer_entry_t *peer, *next;

    if (entry == NULL) {
        return;
    }

    /* Free all peers */
    peer = entry->peers;
    while (peer != NULL) {
        next = peer->next;
        if (peer->addr != NULL) {
            wingo_addr_free(peer->addr);
        }
        free(peer);
        peer = next;
    }

    free(entry);
}

/*
 * Remove hash entry from storage.
 */
static void storage_remove_hash(wingo_dht_storage_t *storage,
                                 hash_entry_t *entry)
{
    wingo_size bucket_idx;
    hash_entry_t *prev, *cur;

    if (storage == NULL || entry == NULL) {
        return;
    }

    bucket_idx = hash_to_bucket(&entry->infohash, storage->bucket_count);

    prev = NULL;
    cur = storage->buckets[bucket_idx];

    while (cur != NULL) {
        if (cur == entry) {
            if (prev == NULL) {
                storage->buckets[bucket_idx] = cur->next;
            } else {
                prev->next = cur->next;
            }

            hash_entry_free(cur);
            storage->hash_count--;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/*
 * Grow hash table (rehash).
 */
static wingo_error_t storage_grow(wingo_dht_storage_t *storage)
{
    wingo_size new_count;
    hash_entry_t **new_buckets;
    wingo_size i;

    new_count = storage->bucket_count * 2;
    if (new_count > STORAGE_MAX_BUCKETS) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    new_buckets = calloc(new_count, sizeof(hash_entry_t *));
    if (new_buckets == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Rehash all entries */
    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry = storage->buckets[i];

        while (entry != NULL) {
            hash_entry_t *next = entry->next;
            wingo_size new_idx = hash_to_bucket(&entry->infohash, new_count);

            entry->next = new_buckets[new_idx];
            new_buckets[new_idx] = entry;

            entry = next;
        }
    }

    free(storage->buckets);
    storage->buckets = new_buckets;
    storage->bucket_count = new_count;

    WINGO_LOG_DEBUG("DHT storage: grew to %zu buckets", new_count);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * INTERNAL HELPERS — PEER LIST
 * ============================================================================ */

/*
 * Find peer entry by address.
 */
static peer_entry_t *peer_find(hash_entry_t *entry,
                                const wingo_addr_t *addr)
{
    peer_entry_t *peer;

    if (entry == NULL || addr == NULL) {
        return NULL;
    }

    for (peer = entry->peers; peer != NULL; peer = peer->next) {
        if (peer->addr != NULL && wingo_addr_cmp(peer->addr, addr) == 0) {
            return peer;
        }
    }

    return NULL;
}

/*
 * Add peer to hash entry.
 *
 * Returns:
 *   WINGO_SUCCESS       — added or refreshed
 *   WINGO_ERR_BUSY      — peer list full
 *   WINGO_ERR_NOMEM     — out of memory
 */
static wingo_error_t peer_add(hash_entry_t *entry,
                               const wingo_addr_t *addr,
                               wingo_size max_peers,
                               wingo_i64 peer_ttl_s)
{
    peer_entry_t *peer;
    wingo_i64 now;

    if (entry == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    now = wingo_time_now();

    /* Check if already exists */
    peer = peer_find(entry, addr);
    if (peer != NULL) {
        /* Refresh TTL */
        peer->announced_at = now;
        peer->expires_at = now + peer_ttl_s;
        return WINGO_SUCCESS;
    }

    /* Check capacity */
    if (entry->peer_count >= max_peers) {
        /* Try to replace an expired peer */
        peer_entry_t *prev = NULL;
        peer_entry_t *cur = entry->peers;

        while (cur != NULL) {
            if (cur->expires_at > 0 && now >= cur->expires_at) {
                /* Remove expired peer */
                if (prev == NULL) {
                    entry->peers = cur->next;
                } else {
                    prev->next = cur->next;
                }

                wingo_addr_free(cur->addr);
                free(cur);
                entry->peer_count--;
                break;
            }
            prev = cur;
            cur = cur->next;
        }

        /* Still full? */
        if (entry->peer_count >= max_peers) {
            return WINGO_ERR_BUSY;
        }
    }

    /* Create new peer */
    peer = calloc(1, sizeof(peer_entry_t));
    if (peer == NULL) {
        return WINGO_ERR_NOMEM;
    }

    peer->addr = wingo_addr_copy(addr);
    if (peer->addr == NULL) {
        free(peer);
        return WINGO_ERR_NOMEM;
    }

    peer->announced_at = now;
    peer->expires_at = now + peer_ttl_s;

    /* Add to list */
    peer->next = entry->peers;
    entry->peers = peer;
    entry->peer_count++;

    return WINGO_SUCCESS;
}

/*
 * Remove peer from hash entry.
 */
static wingo_error_t peer_remove(hash_entry_t *entry,
                                  const wingo_addr_t *addr)
{
    peer_entry_t *peer, *prev;

    if (entry == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    prev = NULL;
    for (peer = entry->peers; peer != NULL; peer = peer->next) {
        if (peer->addr != NULL && wingo_addr_cmp(peer->addr, addr) == 0) {
            if (prev == NULL) {
                entry->peers = peer->next;
            } else {
                prev->next = peer->next;
            }

            wingo_addr_free(peer->addr);
            free(peer);
            entry->peer_count--;
            return WINGO_SUCCESS;
        }
        prev = peer;
    }

    return WINGO_ERR_NOT_FOUND;
}

/* ============================================================================
 * DHT STORAGE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT storage.
 */
wingo_dht_storage_t *wingo_dht_storage_new(wingo_size max_hashes,
                                            wingo_size max_peers_per_hash,
                                            wingo_i64 peer_ttl_s,
                                            wingo_i64 hash_ttl_s)
{
    wingo_dht_storage_t *storage;

    storage = calloc(1, sizeof(wingo_dht_storage_t));
    if (storage == NULL) {
        return NULL;
    }

    /* Set config */
    if (max_hashes == 0) {
        max_hashes = WINGO_DHT_STORAGE_MAX_HASHES;
    }
    if (max_peers_per_hash == 0) {
        max_peers_per_hash = WINGO_DHT_STORAGE_MAX_PEERS;
    }
    if (peer_ttl_s <= 0) {
        peer_ttl_s = WINGO_DHT_STORAGE_PEER_TTL;
    }
    if (hash_ttl_s <= 0) {
        hash_ttl_s = WINGO_DHT_STORAGE_HASH_TTL;
    }

    storage->max_hashes = max_hashes;
    storage->max_peers_per_hash = max_peers_per_hash;
    storage->peer_ttl_s = peer_ttl_s;
    storage->hash_ttl_s = hash_ttl_s;

    /* Allocate hash table */
    storage->bucket_count = STORAGE_INIT_BUCKETS;
    storage->buckets = calloc(storage->bucket_count, sizeof(hash_entry_t *));
    if (storage->buckets == NULL) {
        free(storage);
        return NULL;
    }

    storage->hash_count = 0;

    WINGO_LOG_DEBUG("DHT storage created (max_hashes=%zu, max_peers=%zu)",
                    max_hashes, max_peers_per_hash);

    return storage;
}

/*
 * Free a DHT storage.
 */
void wingo_dht_storage_free(wingo_dht_storage_t *storage)
{
    wingo_size i;

    if (storage == NULL) {
        return;
    }

    if (storage->buckets != NULL) {
        for (i = 0; i < storage->bucket_count; i++) {
            hash_entry_t *entry = storage->buckets[i];

            while (entry != NULL) {
                hash_entry_t *next = entry->next;
                hash_entry_free(entry);
                entry = next;
            }
        }
        free(storage->buckets);
    }

    free(storage);
}

/*
 * Clear all storage entries.
 */
wingo_error_t wingo_dht_storage_clear(wingo_dht_storage_t *storage)
{
    wingo_size i;

    if (storage == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry = storage->buckets[i];

        while (entry != NULL) {
            hash_entry_t *next = entry->next;
            hash_entry_free(entry);
            entry = next;
        }

        storage->buckets[i] = NULL;
    }

    storage->hash_count = 0;

    return WINGO_SUCCESS;
}
/* ============================================================================
 * DHT STORAGE ANNOUNCE
 * ============================================================================ */

/*
 * Announce a peer for an info hash.
 */
wingo_error_t wingo_dht_storage_announce(wingo_dht_storage_t *storage,
                                          const wingo_info_hash_t *infohash,
                                          const wingo_addr_t *addr)
{
    hash_entry_t *entry;
    wingo_error_t rc;
    wingo_size bucket_idx;

    if (storage == NULL || infohash == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Find or create hash entry */
    entry = storage_find_hash(storage, infohash);

    if (entry == NULL) {
        /* Check max hashes */
        if (storage->hash_count >= storage->max_hashes) {
            /* Try to grow hash table (doesn't help with max_hashes) */
            WINGO_LOG_WARN("DHT storage: max hashes reached (%zu)",
                           storage->max_hashes);
            return WINGO_ERR_OUT_OF_RANGE;
        }

        /* Create new entry */
        entry = hash_entry_new(infohash);
        if (entry == NULL) {
            return WINGO_ERR_NOMEM;
        }

        /* Add to bucket */
        bucket_idx = hash_to_bucket(infohash, storage->bucket_count);
        entry->next = storage->buckets[bucket_idx];
        storage->buckets[bucket_idx] = entry;

        storage->hash_count++;

        /* Update stats */
        if (storage->oldest_hash_created == 0 ||
            entry->created_at < storage->oldest_hash_created) {
            storage->oldest_hash_created = entry->created_at;
        }
        if (entry->created_at > storage->newest_hash_created) {
            storage->newest_hash_created = entry->created_at;
        }

        /* Grow if needed */
        if (storage->hash_count > storage->bucket_count * 3 / 4) {
            storage_grow(storage);
        }
    }

    /* Add peer */
    rc = peer_add(entry, addr, storage->max_peers_per_hash,
                  storage->peer_ttl_s);
    if (rc != WINGO_SUCCESS) {
        /* If new entry and no peer added, remove it */
        if (entry->peer_count == 0) {
            storage_remove_hash(storage, entry);
        }
        return rc;
    }

    entry->last_update = wingo_time_now();
    storage->announces++;

    return WINGO_SUCCESS;
}

/*
 * Announce a peer with authentication token.
 */
wingo_error_t wingo_dht_storage_announce_token(wingo_dht_storage_t *storage,
                                                const wingo_info_hash_t *infohash,
                                                const wingo_addr_t *addr,
                                                const wingo_u8 *token,
                                                wingo_size token_len)
{
    /*
     * Token verification is done at the security layer.
     * Storage just stores the peer.
     */
    (void)token;
    (void)token_len;

    return wingo_dht_storage_announce(storage, infohash, addr);
}

/*
 * Remove a peer from an info hash.
 */
wingo_error_t wingo_dht_storage_remove_peer(wingo_dht_storage_t *storage,
                                             const wingo_info_hash_t *infohash,
                                             const wingo_addr_t *addr)
{
    hash_entry_t *entry;
    wingo_error_t rc;

    if (storage == NULL || infohash == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    entry = storage_find_hash(storage, infohash);
    if (entry == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    rc = peer_remove(entry, addr);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    storage->removes++;

    /* Remove empty hash entry */
    if (entry->peer_count == 0) {
        storage_remove_hash(storage, entry);
    }

    return WINGO_SUCCESS;
}

/*
 * Remove an entire info hash.
 */
wingo_error_t wingo_dht_storage_remove_hash(wingo_dht_storage_t *storage,
                                             const wingo_info_hash_t *infohash)
{
    hash_entry_t *entry;

    if (storage == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    entry = storage_find_hash(storage, infohash);
    if (entry == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    storage_remove_hash(storage, entry);
    storage->removes++;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT STORAGE GET PEERS
 * ============================================================================ */

/*
 * Get peers for an info hash.
 */
wingo_size wingo_dht_storage_get_peers(wingo_dht_storage_t *storage,
                                        const wingo_info_hash_t *infohash,
                                        wingo_addr_t **out,
                                        wingo_size max)
{
    hash_entry_t *entry;
    peer_entry_t *peer;
    wingo_size count = 0;
    wingo_i64 now;

    if (storage == NULL || infohash == NULL || out == NULL || max == 0) {
        return 0;
    }

    storage->lookups++;

    entry = storage_find_hash(storage, infohash);
    if (entry == NULL) {
        return 0;
    }

    now = wingo_time_now();

    for (peer = entry->peers; peer != NULL && count < max; peer = peer->next) {
        /* Skip expired */
        if (peer->expires_at > 0 && now >= peer->expires_at) {
            continue;
        }

        out[count] = wingo_addr_copy(peer->addr);
        if (out[count] != NULL) {
            count++;
        }
    }

    return count;
}

/*
 * Get peer count for an info hash.
 */
wingo_size wingo_dht_storage_peer_count(wingo_dht_storage_t *storage,
                                         const wingo_info_hash_t *infohash)
{
    hash_entry_t *entry;
    peer_entry_t *peer;
    wingo_size count = 0;
    wingo_i64 now;

    if (storage == NULL || infohash == NULL) {
        return 0;
    }

    entry = storage_find_hash(storage, infohash);
    if (entry == NULL) {
        return 0;
    }

    now = wingo_time_now();

    for (peer = entry->peers; peer != NULL; peer = peer->next) {
        if (peer->expires_at > 0 && now >= peer->expires_at) {
            continue;
        }
        count++;
    }

    return count;
}

/*
 * Check if an info hash exists.
 */
bool wingo_dht_storage_has_hash(const wingo_dht_storage_t *storage,
                                 const wingo_info_hash_t *infohash)
{
    if (storage == NULL || infohash == NULL) {
        return false;
    }

    return storage_find_hash((wingo_dht_storage_t *)storage, infohash) != NULL;
}

/*
 * Check if a peer exists for an info hash.
 */
bool wingo_dht_storage_has_peer(const wingo_dht_storage_t *storage,
                                 const wingo_info_hash_t *infohash,
                                 const wingo_addr_t *addr)
{
    hash_entry_t *entry;

    if (storage == NULL || infohash == NULL || addr == NULL) {
        return false;
    }

    entry = storage_find_hash((wingo_dht_storage_t *)storage, infohash);
    if (entry == NULL) {
        return false;
    }

    return peer_find(entry, addr) != NULL;
}

/* ============================================================================
 * DHT STORAGE ITERATION
 * ============================================================================ */

/*
 * Iterate over all info hashes.
 */
void wingo_dht_storage_foreach_hash(wingo_dht_storage_t *storage,
                                     wingo_dht_storage_hash_cb_t callback,
                                     void *userdata)
{
    wingo_size i;

    if (storage == NULL || callback == NULL) {
        return;
    }

    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry = storage->buckets[i];

        while (entry != NULL) {
            hash_entry_t *next = entry->next;

            if (!callback(&entry->infohash, entry->peer_count, userdata)) {
                return;
            }

            entry = next;
        }
    }
}

/*
 * Iterate over all peers for an info hash.
 */
wingo_size wingo_dht_storage_foreach_peer(wingo_dht_storage_t *storage,
                                           const wingo_info_hash_t *infohash,
                                           wingo_dht_storage_peer_cb_t callback,
                                           void *userdata)
{
    hash_entry_t *entry;
    peer_entry_t *peer;
    peer_entry_t *next;
    wingo_size count = 0;
    wingo_i64 now;

    if (storage == NULL || infohash == NULL || callback == NULL) {
        return 0;
    }

    entry = storage_find_hash(storage, infohash);
    if (entry == NULL) {
        return 0;
    }

    now = wingo_time_now();

    peer = entry->peers;
    while (peer != NULL) {
        next = peer->next;

        /* Skip expired */
        if (peer->expires_at > 0 && now >= peer->expires_at) {
            peer = next;
            continue;
        }

        count++;

        if (!callback(peer->addr, userdata)) {
            break;
        }

        peer = next;
    }

    return count;
}

/* ============================================================================
 * DHT STORAGE EXPIRY
 * ============================================================================ */

/*
 * Expire old peers and hashes.
 */
wingo_size wingo_dht_storage_expire(wingo_dht_storage_t *storage)
{
    wingo_size i;
    wingo_size expired = 0;
    wingo_i64 now;

    if (storage == NULL) {
        return 0;
    }

    now = wingo_time_now();

    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry = storage->buckets[i];
        hash_entry_t *prev = NULL;

        while (entry != NULL) {
            hash_entry_t *next = entry->next;

            /* Expire peers */
            {
                peer_entry_t *peer = entry->peers;
                peer_entry_t *peer_prev = NULL;

                while (peer != NULL) {
                    peer_entry_t *peer_next = peer->next;

                    if (peer->expires_at > 0 && now >= peer->expires_at) {
                        /* Remove expired peer */
                        if (peer_prev == NULL) {
                            entry->peers = peer->next;
                        } else {
                            peer_prev->next = peer->next;
                        }

                        wingo_addr_free(peer->addr);
                        free(peer);
                        entry->peer_count--;
                        expired++;
                    } else {
                        peer_prev = peer;
                    }

                    peer = peer_next;
                }
            }

            /* Expire empty or old hash entries */
            if (entry->peer_count == 0) {
                /* Remove empty entry */
                if (prev == NULL) {
                    storage->buckets[i] = entry->next;
                } else {
                    prev->next = entry->next;
                }

                hash_entry_free(entry);
                storage->hash_count--;
                expired++;

                entry = next;
                continue;
            }

            /* Check hash TTL */
            if (now - entry->last_update >= storage->hash_ttl_s) {
                /* Hash too old — but only remove if no recent peers */
                bool has_recent = false;
                peer_entry_t *peer;

                for (peer = entry->peers; peer != NULL; peer = peer->next) {
                    if (peer->announced_at > entry->last_update) {
                        has_recent = true;
                        break;
                    }
                }

                if (!has_recent) {
                    /* Remove old entry */
                    if (prev == NULL) {
                        storage->buckets[i] = entry->next;
                    } else {
                        prev->next = entry->next;
                    }

                    hash_entry_free(entry);
                    storage->hash_count--;
                    expired++;

                    entry = next;
                    continue;
                }
            }

            prev = entry;
            entry = next;
        }
    }

    storage->expires += expired;

    return expired;
}

/*
 * Get time until next expiry check.
 */
wingo_i64 wingo_dht_storage_next_expire(const wingo_dht_storage_t *storage)
{
    if (storage == NULL) {
        return 0;
    }

    /*
     * Expiry should run every 5 minutes.
     * Return absolute time.
     */
    return wingo_time_now() + 300;
}

/*
 * Check if expiry is needed.
 */
bool wingo_dht_storage_needs_expire(const wingo_dht_storage_t *storage)
{
    (void)storage;

    /*
     * We don't track last expire time, so always return true
     * and let caller call expire() at its own interval.
     */
    return true;
}

/*
 * Refresh a peer's TTL.
 */
wingo_error_t wingo_dht_storage_refresh_peer(wingo_dht_storage_t *storage,
                                              const wingo_info_hash_t *infohash,
                                              const wingo_addr_t *addr)
{
    hash_entry_t *entry;
    peer_entry_t *peer;
    wingo_i64 now;

    if (storage == NULL || infohash == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    entry = storage_find_hash(storage, infohash);
    if (entry == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    peer = peer_find(entry, addr);
    if (peer == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    now = wingo_time_now();
    peer->announced_at = now;
    peer->expires_at = now + storage->peer_ttl_s;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT STORAGE STATISTICS
 * ============================================================================ */

/*
 * Get storage statistics.
 */
wingo_error_t wingo_dht_storage_get_stats(const wingo_dht_storage_t *storage,
                                           wingo_dht_storage_stats_t *stats)
{
    wingo_i64 now;
    wingo_size total_peers = 0;
    wingo_size i;

    if (storage == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    stats->hash_count = storage->hash_count;
    stats->max_hashes = storage->max_hashes;
    stats->max_peers_per_hash = storage->max_peers_per_hash;

    stats->announces = storage->announces;
    stats->lookups = storage->lookups;
    stats->removes = storage->removes;
    stats->expires = storage->expires;

    /* Count total peers */
    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry;

        for (entry = storage->buckets[i]; entry != NULL; entry = entry->next) {
            total_peers += entry->peer_count;
        }
    }

    stats->peer_count = total_peers;

    /* Ages */
    now = wingo_time_now();

    if (storage->oldest_hash_created > 0) {
        stats->oldest_hash_age = now - storage->oldest_hash_created;
    } else {
        stats->oldest_hash_age = 0;
    }

    if (storage->newest_hash_created > 0) {
        stats->newest_hash_age = now - storage->newest_hash_created;
    } else {
        stats->newest_hash_age = 0;
    }

    return WINGO_SUCCESS;
}

/*
 * Reset storage statistics.
 */
void wingo_dht_storage_reset_stats(wingo_dht_storage_t *storage)
{
    if (storage == NULL) {
        return;
    }

    storage->announces = 0;
    storage->lookups = 0;
    storage->removes = 0;
    storage->expires = 0;
    storage->oldest_hash_created = 0;
    storage->newest_hash_created = 0;
}

/* ============================================================================
 * DHT STORAGE PERSISTENCE
 * ============================================================================ */

/*
 * Save storage to file.
 *
 * Simple format:
 *   H <infohash-hex>
 *   P <ip>:<port>
 *   P <ip>:<port>
 *   H <infohash-hex>
 *   ...
 */
wingo_error_t wingo_dht_storage_save(const wingo_dht_storage_t *storage,
                                      const char *path)
{
    FILE *f;
    wingo_size i;

    if (storage == NULL || path == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    f = fopen(path, "w");
    if (f == NULL) {
        WINGO_LOG_ERROR("DHT storage: cannot open '%s' for writing", path);
        return WINGO_ERR_FILE_OPEN;
    }

    fprintf(f, "# Bowie DHT storage\n");
    fprintf(f, "# Format: H <infohash-hex>, P <ip>:<port>\n");
    fprintf(f, "\n");

    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry;

        for (entry = storage->buckets[i]; entry != NULL; entry = entry->next) {
            char hash_hex[WINGO_DHT_ID_HEX_SIZE];
            peer_entry_t *peer;
            static const char hex[] = "0123456789abcdef";
            wingo_size j;

            /* Hash hex */
            for (j = 0; j < WINGO_DHT_ID_SIZE; j++) {
                hash_hex[j * 2]     = hex[(entry->infohash.bytes[j] >> 4) & 0x0F];
                hash_hex[j * 2 + 1] = hex[entry->infohash.bytes[j] & 0x0F];
            }
            hash_hex[WINGO_DHT_ID_SIZE * 2] = '\0';

            fprintf(f, "H %s\n", hash_hex);

            for (peer = entry->peers; peer != NULL; peer = peer->next) {
                char addr_str[WINGO_ADDR_STR_MAX];

                if (peer->addr == NULL) {
                    continue;
                }

                if (wingo_addr_str(peer->addr, addr_str, sizeof(addr_str)) != WINGO_SUCCESS) {
                    continue;
                }

                fprintf(f, "P %s\n", addr_str);
            }

            fprintf(f, "\n");
        }
    }

    fclose(f);

    WINGO_LOG_DEBUG("DHT storage: saved to '%s'", path);

    return WINGO_SUCCESS;
}

/*
 * Load storage from file.
 */
wingo_error_t wingo_dht_storage_load(wingo_dht_storage_t *storage,
                                      const char *path)
{
    FILE *f;
    char line[512];
    wingo_info_hash_t current_hash;
    bool has_current = false;
    int line_num = 0;

    if (storage == NULL || path == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        WINGO_LOG_DEBUG("DHT storage: file '%s' not found", path);
        return WINGO_ERR_FILE_NOT_FOUND;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        char *p = line;
        char *end;

        line_num++;

        /* Trim whitespace */
        while (*p == ' ' || *p == '\t') p++;
        end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' ||
                            end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';

        if (*p == '\0' || *p == '#') {
            continue;
        }

        if (p[0] == 'H' && p[1] == ' ') {
            /* Info hash */
            const char *hex = p + 2;
            wingo_size j;

            if (strlen(hex) != WINGO_DHT_ID_SIZE * 2) {
                WINGO_LOG_WARN("DHT storage: invalid hash on line %d", line_num);
                has_current = false;
                continue;
            }

            for (j = 0; j < WINGO_DHT_ID_SIZE; j++) {
                unsigned int byte;
                if (sscanf(hex + j * 2, "%2x", &byte) != 1) {
                    has_current = false;
                    break;
                }
                current_hash.bytes[j] = (wingo_u8)byte;
            }

            if (j == WINGO_DHT_ID_SIZE) {
                has_current = true;
            }
        } else if (p[0] == 'P' && p[1] == ' ' && has_current) {
            /* Peer address */
            const char *addr_str = p + 2;
            wingo_addr_t *addr;
            char *colon;
            char ip[WINGO_ADDR_STR_MAX];
            unsigned int port;

            /* Find last colon (for IPv6) */
            colon = strrchr(addr_str, ':');
            if (colon == NULL) {
                continue;
            }

            /* Extract IP */
            if (addr_str[0] == '[') {
                /* IPv6 [ip]:port */
                char *close = strchr(addr_str, ']');
                if (close == NULL || close > colon) {
                    continue;
                }

                wingo_size ip_len = (wingo_size)(close - addr_str - 1);
                if (ip_len >= sizeof(ip)) {
                    continue;
                }

                memcpy(ip, addr_str + 1, ip_len);
                ip[ip_len] = '\0';
            } else {
                /* IPv4 ip:port */
                wingo_size ip_len = (wingo_size)(colon - addr_str);
                if (ip_len >= sizeof(ip)) {
                    continue;
                }

                memcpy(ip, addr_str, ip_len);
                ip[ip_len] = '\0';
            }

            if (sscanf(colon + 1, "%u", &port) != 1 || port > 65535) {
                continue;
            }

            /* Create address */
            if (strchr(ip, ':') != NULL) {
                addr = wingo_addr_new_ipv6(ip, (wingo_u16)port);
            } else {
                addr = wingo_addr_new_ipv4(ip, (wingo_u16)port);
            }

            if (addr == NULL) {
                continue;
            }

            /* Add to storage */
            wingo_dht_storage_announce(storage, &current_hash, addr);
            wingo_addr_free(addr);
        }
    }

    fclose(f);

    WINGO_LOG_DEBUG("DHT storage: loaded %d lines from '%s'", line_num, path);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT STORAGE UTILITY
 * ============================================================================ */

/*
 * Get storage result name.
 */
const char *wingo_dht_storage_result_name(wingo_dht_storage_result_t result)
{
    switch (result) {
    case WINGO_DHT_STORAGE_OK:         return "OK";
    case WINGO_DHT_STORAGE_FULL:       return "FULL";
    case WINGO_DHT_STORAGE_NOT_FOUND:  return "NOT_FOUND";
    case WINGO_DHT_STORAGE_EXPIRED:    return "EXPIRED";
    case WINGO_DHT_STORAGE_DUPLICATE:  return "DUPLICATE";
    case WINGO_DHT_STORAGE_ERROR:      return "ERROR";
    default:                           return "UNKNOWN";
    }
}

/*
 * Print storage status.
 */
void wingo_dht_storage_print(const wingo_dht_storage_t *storage, FILE *f)
{
    wingo_dht_storage_stats_t stats;

    if (f == NULL) {
        f = stderr;
    }

    if (storage == NULL) {
        fprintf(f, "DHT storage: (null)\n");
        return;
    }

    wingo_dht_storage_get_stats(storage, &stats);

    fprintf(f, "DHT Storage:\n");
    fprintf(f, "  Hashes:      %zu / %zu\n",
            stats.hash_count, stats.max_hashes);
    fprintf(f, "  Peers:       %zu\n", stats.peer_count);
    fprintf(f, "  Max per hash: %zu\n", stats.max_peers_per_hash);
    fprintf(f, "  Buckets:     %zu\n", storage->bucket_count);
    fprintf(f, "\n");

    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Announces: %llu\n", (unsigned long long)stats.announces);
    fprintf(f, "    Lookups:   %llu\n", (unsigned long long)stats.lookups);
    fprintf(f, "    Removes:   %llu\n", (unsigned long long)stats.removes);
    fprintf(f, "    Expires:   %llu\n", (unsigned long long)stats.expires);
}

/*
 * Print all info hashes.
 */
void wingo_dht_storage_print_hashes(const wingo_dht_storage_t *storage,
                                     FILE *f)
{
    wingo_size i;
    wingo_size index = 0;

    if (f == NULL) {
        f = stderr;
    }

    if (storage == NULL) {
        fprintf(f, "DHT storage: (null)\n");
        return;
    }

    fprintf(f, "DHT Storage Hashes:\n");

    for (i = 0; i < storage->bucket_count; i++) {
        hash_entry_t *entry;

        for (entry = storage->buckets[i]; entry != NULL; entry = entry->next) {
            char hash_hex[WINGO_DHT_ID_HEX_SIZE];
            static const char hex[] = "0123456789abcdef";
            wingo_size j;

            for (j = 0; j < WINGO_DHT_ID_SIZE; j++) {
                hash_hex[j * 2]     = hex[(entry->infohash.bytes[j] >> 4) & 0x0F];
                hash_hex[j * 2 + 1] = hex[entry->infohash.bytes[j] & 0x0F];
            }
            hash_hex[WINGO_DHT_ID_SIZE * 2] = '\0';

            fprintf(f, "  [%zu] %s  peers=%zu\n",
                    index++, hash_hex, entry->peer_count);
        }
    }
}

/*
 * Print peers for an info hash.
 */
void wingo_dht_storage_print_peers(const wingo_dht_storage_t *storage,
                                    const wingo_info_hash_t *infohash,
                                    FILE *f)
{
    hash_entry_t *entry;
    peer_entry_t *peer;
    wingo_size index = 0;
    wingo_i64 now;

    if (f == NULL) {
        f = stderr;
    }

    if (storage == NULL || infohash == NULL) {
        fprintf(f, "DHT storage: (null)\n");
        return;
    }

    entry = storage_find_hash((wingo_dht_storage_t *)storage, infohash);
    if (entry == NULL) {
        fprintf(f, "Info hash not found\n");
        return;
    }

    now = wingo_time_now();

    fprintf(f, "Peers for info hash:\n");

    for (peer = entry->peers; peer != NULL; peer = peer->next) {
        char addr_str[WINGO_ADDR_STR_MAX];
        wingo_i64 ttl;

        if (peer->addr == NULL) {
            continue;
        }

        wingo_addr_str(peer->addr, addr_str, sizeof(addr_str));

        ttl = peer->expires_at - now;

        fprintf(f, "  [%zu] %s  ttl=%llds\n",
                index++, addr_str, (long long)ttl);
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
