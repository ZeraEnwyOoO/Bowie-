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

#ifndef WINGO_CRYPTO_HASH_H
#define WINGO_CRYPTO_HASH_H

/*
 * ============================================================================
 * WINGO CRYPTO HASH
 * ============================================================================
 *
 * This header provides cryptographic hash functions for Bowie.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    HASH LAYER                               │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │   SHA-1     │  │  SHA-256    │  │   (future)  │        │
 *   │   │             │  │             │  │             │        │
 *   │   │  160-bit    │  │  256-bit    │  │  BLAKE2     │        │
 *   │   │  output     │  │  output     │  │  SHA-512    │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │                                                             │
 *   │   Implementation: OpenSSL EVP (wrapper)                     │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * HASH ALGORITHM
 * ============================================================================ */

/*
 * Hash algorithm.
 *
 * NOTE: SHA-1 is used for DHT info hashes (BEP 5 compatibility).
 *       SHA-256 is used for Bowie-specific security.
 */
typedef enum {
    WINGO_HASH_SHA1     = 0,
    WINGO_HASH_SHA256   = 1,
} wingo_hash_algo_t;

/* ============================================================================
 * HASH STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Hash context.
 *
 * This is an opaque type. Use wingo_hash_*() functions.
 */
typedef struct wingo_hash wingo_hash_t;

/* ============================================================================
 * HASH LIFECYCLE
 * ============================================================================ */

/*
 * Create a new hash context.
 *
 * @param algo      Hash algorithm
 * @return          Hash context, or NULL on error
 */
wingo_hash_t *wingo_hash_new(wingo_hash_algo_t algo);

/*
 * Free a hash context.
 *
 * @param hash      Hash context (NULL is safe)
 */
void wingo_hash_free(wingo_hash_t *hash);

/*
 * Reset a hash context for reuse.
 *
 * This allows reusing the same context without reallocation.
 *
 * @param hash      Hash context
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_hash_reset(wingo_hash_t *hash);

/* ============================================================================
 * HASH OPERATIONS
 * ============================================================================ */

/*
 * Update hash with data.
 *
 * Can be called multiple times to hash data incrementally.
 *
 * @param hash      Hash context
 * @param data      Data to hash
 * @param len       Length of data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_hash_update(wingo_hash_t *hash,
                                 const void *data,
                                 wingo_size len);

/*
 * Finalize hash and get digest.
 *
 * After this call, the context cannot be updated further
 * until reset with wingo_hash_reset().
 *
 * @param hash      Hash context
 * @param out       Output buffer
 * @param out_len   Output buffer size (must be >= hash size)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_hash_final(wingo_hash_t *hash,
                                void *out,
                                wingo_size out_len);

/*
 * One-shot hash.
 *
 * Convenience function for hashing data in a single call.
 *
 * @param algo      Hash algorithm
 * @param data      Data to hash
 * @param len       Length of data
 * @param out       Output buffer
 * @param out_len   Output buffer size (must be >= hash size)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_hash_once(wingo_hash_algo_t algo,
                               const void *data,
                               wingo_size len,
                               void *out,
                               wingo_size out_len);

/* ============================================================================
 * HASH QUERY
 * ============================================================================ */

/*
 * Get hash output size for an algorithm.
 *
 * @param algo      Hash algorithm
 * @return          Output size in bytes, or 0 on error
 */
wingo_size wingo_hash_size(wingo_hash_algo_t algo);

/*
 * Get hash algorithm name.
 *
 * @param algo      Hash algorithm
 * @return          Static string (never NULL)
 */
const char *wingo_hash_name(wingo_hash_algo_t algo);

/*
 * Get hash algorithm from name.
 *
 * @param name      Algorithm name (e.g., "sha1", "sha256")
 * @return          Hash algorithm, or WINGO_HASH_SHA1 on unknown
 */
wingo_hash_algo_t wingo_hash_from_name(const char *name);

/* ============================================================================
 * HASH UTILITY
 * ============================================================================ */

/*
 * Hash data and output as hex string.
 *
 * @param algo      Hash algorithm
 * @param data      Data to hash
 * @param len       Length of data
 * @param out       Output buffer (must be >= 2 * hash_size + 1)
 * @param out_len   Output buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_hash_hex(wingo_hash_algo_t algo,
                              const void *data,
                              wingo_size len,
                              char *out,
                              wingo_size out_len);

/*
 * Compare two hashes in constant time.
 *
 * This is important for security — regular memcmp() can leak
 * timing information.
 *
 * @param a         First hash
 * @param b         Second hash
 * @param len       Length of hash
 * @return          true if equal, false otherwise
 */
bool wingo_hash_equal(const void *a, const void *b, wingo_size len);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CRYPTO_HASH_H */
