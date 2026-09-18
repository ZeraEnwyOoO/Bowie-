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

#ifndef WINGO_CRYPTO_AES_H
#define WINGO_CRYPTO_AES_H

/*
 * ============================================================================
 * WINGO CRYPTO AES
 * ============================================================================
 *
 * This header provides AES-GCM authenticated encryption for Bowie.
 *
 * Why AES-GCM?
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    AES-GCM                                  │
 *   │                                                             │
 *   │   GCM = Galois/Counter Mode                                 │
 *   │   ├── Confidentiality (AES-CTR)                             │
 *   │   ├── Authenticity (GHASH)                                  │
 *   │   └── Associated Data (AAD)                                 │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  Plaintext + AAD                                    │   │
 *   │   │       │                                             │   │
 *   │   │       ▼                                             │   │
 *   │   │  ┌─────────────┐                                    │   │
 *   │   │  │  AES-GCM    │                                    │   │
 *   │   │  │  Encrypt    │                                    │   │
 *   │   │  └──────┬──────┘                                    │   │
 *   │   │         │                                           │   │
 *   │   │         ▼                                           │   │
 *   │   │  Ciphertext + Tag                                  │   │
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
 * AES CONSTANTS
 * ============================================================================ */

/*
 * AES-GCM key size (256-bit).
 */
#define WINGO_AES_KEY_SIZE      32

/*
 * AES-GCM IV/nonce size (96-bit).
 */
#define WINGO_AES_IV_SIZE       12

/*
 * AES-GCM tag size (128-bit).
 */
#define WINGO_AES_TAG_SIZE      16

/*
 * AES-GCM block size.
 */
#define WINGO_AES_BLOCK_SIZE    16

/*
 * AES-GCM maximum plaintext size (2^39 - 256 bits).
 */
#define WINGO_AES_MAX_PLAINTEXT ((1ULL << 36) - 32)

/*
 * AES-GCM maximum AAD size (2^64 - 1 bits).
 */
#define WINGO_AES_MAX_AAD       ((1ULL << 61) - 1)

/* ============================================================================
 * AES STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * AES context.
 *
 * This is an opaque type. Use wingo_aes_*() functions.
 */
typedef struct wingo_aes wingo_aes_t;

/* ============================================================================
 * AES LIFECYCLE
 * ============================================================================ */

/*
 * Create a new AES context.
 *
 * @return          AES context, or NULL on error
 */
wingo_aes_t *wingo_aes_new(void);

/*
 * Free an AES context.
 *
 * @param aes       AES context (NULL is safe)
 */
void wingo_aes_free(wingo_aes_t *aes);

/*
 * Set the encryption/decryption key.
 *
 * @param aes       AES context
 * @param key       Key bytes (WINGO_AES_KEY_SIZE bytes)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_set_key(wingo_aes_t *aes, const void *key);

/*
 * Clear the key from the context.
 *
 * @param aes       AES context
 */
void wingo_aes_clear_key(wingo_aes_t *aes);

/* ============================================================================
 * AES ENCRYPTION
 * ============================================================================ */

/*
 * Encrypt data with AES-GCM.
 *
 * @param aes       AES context
 * @param iv        IV/nonce (WINGO_AES_IV_SIZE bytes)
 * @param plaintext Plaintext to encrypt
 * @param plaintext_len Plaintext length
 * @param aad       Associated data (may be NULL)
 * @param aad_len   AAD length
 * @param ciphertext Output ciphertext (must be >= plaintext_len)
 * @param tag       Output authentication tag (WINGO_AES_TAG_SIZE bytes)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_encrypt(wingo_aes_t *aes,
                                 const void *iv,
                                 const void *plaintext,
                                 wingo_size plaintext_len,
                                 const void *aad,
                                 wingo_size aad_len,
                                 void *ciphertext,
                                 void *tag);

/*
 * Decrypt data with AES-GCM.
 *
 * @param aes       AES context
 * @param iv        IV/nonce (WINGO_AES_IV_SIZE bytes)
 * @param ciphertext Ciphertext to decrypt
 * @param ciphertext_len Ciphertext length
 * @param aad       Associated data (may be NULL)
 * @param aad_len   AAD length
 * @param tag       Authentication tag (WINGO_AES_TAG_SIZE bytes)
 * @param plaintext Output plaintext (must be >= ciphertext_len)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_decrypt(wingo_aes_t *aes,
                                 const void *iv,
                                 const void *ciphertext,
                                 wingo_size ciphertext_len,
                                 const void *aad,
                                 wingo_size aad_len,
                                 const void *tag,
                                 void *plaintext);

/* ============================================================================
 * AES ONE-SHOT
 * ============================================================================ */

/*
 * One-shot AES-GCM encryption.
 *
 * @param key       Key (WINGO_AES_KEY_SIZE bytes)
 * @param iv        IV/nonce (WINGO_AES_IV_SIZE bytes)
 * @param plaintext Plaintext to encrypt
 * @param plaintext_len Plaintext length
 * @param aad       Associated data (may be NULL)
 * @param aad_len   AAD length
 * @param ciphertext Output ciphertext
 * @param tag       Output authentication tag
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_encrypt_once(const void *key,
                                      const void *iv,
                                      const void *plaintext,
                                      wingo_size plaintext_len,
                                      const void *aad,
                                      wingo_size aad_len,
                                      void *ciphertext,
                                      void *tag);

/*
 * One-shot AES-GCM decryption.
 *
 * @param key       Key (WINGO_AES_KEY_SIZE bytes)
 * @param iv        IV/nonce (WINGO_AES_IV_SIZE bytes)
 * @param ciphertext Ciphertext to decrypt
 * @param ciphertext_len Ciphertext length
 * @param aad       Associated data (may be NULL)
 * @param aad_len   AAD length
 * @param tag       Authentication tag
 * @param plaintext Output plaintext
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_decrypt_once(const void *key,
                                      const void *iv,
                                      const void *ciphertext,
                                      wingo_size ciphertext_len,
                                      const void *aad,
                                      wingo_size aad_len,
                                      const void *tag,
                                      void *plaintext);

/* ============================================================================
 * AES UTILITY
 * ============================================================================ */

/*
 * Generate a random AES key.
 *
 * @param key       Output key (WINGO_AES_KEY_SIZE bytes)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_generate_key(void *key);

/*
 * Generate a random IV/nonce.
 *
 * @param iv        Output IV (WINGO_AES_IV_SIZE bytes)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_aes_generate_iv(void *iv);

/*
 * Increment an IV/nonce.
 *
 * This is useful for counter mode.
 *
 * @param iv        IV to increment
 */
void wingo_aes_increment_iv(void *iv);

/*
 * Check if AES is available.
 *
 * @return          true if available, false otherwise
 */
bool wingo_aes_is_available(void);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CRYPTO_AES_H */
