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

#ifndef WINGO_CRYPTO_KEYEXCHANGE_H
#define WINGO_CRYPTO_KEYEXCHANGE_H

/*
 * ============================================================================
 * WINGO CRYPTO KEY EXCHANGE
 * ============================================================================
 *
 * This header provides key exchange for Bowie.
 *
 * Why X25519?
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    X25519 (ECDH)                            │
 *   │                                                             │
 *   │   ├── 256-bit key size                                      │
 *   │   ├── Fast (Montgomery ladder)                              │
 *   │   ├── Constant-time                                         │
 *   │   ├── No patents                                            │
 *   │   └── Used by TLS 1.3, Signal, WireGuard                    │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  Alice                          Bob                 │   │
 *   │   │  ─────                          ───                 │   │
 *   │   │  a = random                     b = random          │   │
 *   │   │  A = a * G                      B = b * G           │   │
 *   │   │                                                      │   │
 *   │   │         A ────────────────────>                      │   │
 *   │   │         <──────────────────── B                      │   │
 *   │   │                                                      │   │
 *   │   │  K = a * B                      K = b * A           │   │
 *   │   │  (same shared secret)                                │   │
 *   │   └─────────────────────────────────────────────────────┘   │
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
 * KEY EXCHANGE CONSTANTS
 * ============================================================================ */

/*
 * X25519 key size (256-bit).
 */
#define WINGO_KEYEXCHANGE_KEY_SIZE      32

/*
 * X25519 shared secret size (256-bit).
 */
#define WINGO_KEYEXCHANGE_SECRET_SIZE   32

/*
 * X25519 public key size.
 */
#define WINGO_KEYEXCHANGE_PUBLIC_SIZE   32

/*
 * X25519 private key size.
 */
#define WINGO_KEYEXCHANGE_PRIVATE_SIZE  32

/* ============================================================================
 * KEY EXCHANGE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Key exchange context.
 *
 * This is an opaque type. Use wingo_keyexchange_*() functions.
 */
typedef struct wingo_keyexchange wingo_keyexchange_t;

/* ============================================================================
 * KEY EXCHANGE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new key exchange context.
 *
 * @return          Context, or NULL on error
 */
wingo_keyexchange_t *wingo_keyexchange_new(void);

/*
 * Free a key exchange context.
 *
 * @param kx        Context (NULL is safe)
 */
void wingo_keyexchange_free(wingo_keyexchange_t *kx);

/*
 * Generate a new key pair.
 *
 * This replaces any existing key pair in the context.
 *
 * @param kx        Context
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_generate(wingo_keyexchange_t *kx);

/*
 * Set an existing private key.
 *
 * @param kx        Context
 * @param private_key Private key bytes (WINGO_KEYEXCHANGE_PRIVATE_SIZE)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_set_private(wingo_keyexchange_t *kx,
                                             const void *private_key);

/*
 * Clear the key pair from the context.
 *
 * @param kx        Context
 */
void wingo_keyexchange_clear(wingo_keyexchange_t *kx);

/* ============================================================================
 * KEY EXCHANGE QUERY
 * ============================================================================ */

/*
 * Get the public key.
 *
 * @param kx        Context
 * @param out       Output buffer (WINGO_KEYEXCHANGE_PUBLIC_SIZE bytes)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_get_public(const wingo_keyexchange_t *kx,
                                            void *out);

/*
 * Get the private key.
 *
 * WARNING: Handle with care. Only use for serialization.
 *
 * @param kx        Context
 * @param out       Output buffer (WINGO_KEYEXCHANGE_PRIVATE_SIZE bytes)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_get_private(const wingo_keyexchange_t *kx,
                                             void *out);

/*
 * Check if a key pair is set.
 *
 * @param kx        Context
 * @return          true if key pair is set, false otherwise
 */
bool wingo_keyexchange_has_key(const wingo_keyexchange_t *kx);

/* ============================================================================
 * KEY EXCHANGE OPERATIONS
 * ============================================================================ */

/*
 * Compute shared secret with peer's public key.
 *
 * This is the core ECDH operation.
 *
 * @param kx        Context (must have private key)
 * @param peer_public Peer's public key (WINGO_KEYEXCHANGE_PUBLIC_SIZE)
 * @param out       Output shared secret (WINGO_KEYEXCHANGE_SECRET_SIZE)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_shared(wingo_keyexchange_t *kx,
                                        const void *peer_public,
                                        void *out);

/*
 * One-shot key exchange.
 *
 * @param my_private My private key
 * @param peer_public Peer's public key
 * @param out       Output shared secret
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_shared_once(const void *my_private,
                                             const void *peer_public,
                                             void *out);

/*
 * Derive a key from a shared secret using HKDF.
 *
 * @param shared    Shared secret
 * @param shared_len Length of shared secret
 * @param salt      Salt (may be NULL)
 * @param salt_len  Salt length
 * @param info      Info string (may be NULL)
 * @param info_len  Info length
 * @param out       Output key
 * @param out_len   Output key length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_derive(const void *shared,
                                        wingo_size shared_len,
                                        const void *salt,
                                        wingo_size salt_len,
                                        const void *info,
                                        wingo_size info_len,
                                        void *out,
                                        wingo_size out_len);

/* ============================================================================
 * KEY EXCHANGE UTILITY
 * ============================================================================ */

/*
 * Generate a random key pair (standalone).
 *
 * @param private_key Output private key (may be NULL)
 * @param public_key  Output public key (may be NULL)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_generate_pair(void *private_key,
                                               void *public_key);

/*
 * Derive public key from private key.
 *
 * @param private_key Private key
 * @param public_key  Output public key
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_keyexchange_public_from_private(const void *private_key,
                                                     void *public_key);

/*
 * Validate a public key.
 *
 * @param public_key Public key to validate
 * @return          true if valid, false otherwise
 */
bool wingo_keyexchange_validate_public(const void *public_key);

/*
 * Check if key exchange is available.
 *
 * @return          true if available, false otherwise
 */
bool wingo_keyexchange_is_available(void);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CRYPTO_KEYEXCHANGE_H */
