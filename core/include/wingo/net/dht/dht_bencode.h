
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

#ifndef WINGO_NET_DHT_BENCODE_H
#define WINGO_NET_DHT_BENCODE_H

/*
 * ============================================================================
 * WINGO DHT BENCODE
 * ============================================================================
 *
 * This header provides Bencode encoding/decoding for Bowie DHT.
 *
 * Bencode Format:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    BENCODE TYPES                            │
 *   │                                                             │
 *   │   Integer:     i<number>e                                   │
 *   │   Byte String: <length>:<data>                              │
 *   │   List:        l<items>e                                    │
 *   │   Dictionary:  d<key><value>...e                            │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * Examples:
 *
 *   Integer:      i42e              = 42
 *   Byte String:  5:hello           = "hello"
 *   List:         l5:helloi42ee     = ["hello", 42]
 *   Dict:         d3:foo3:bare      = {"foo": "bar"}
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/util/buffer.h"

/* ============================================================================
 * BENCODE TYPES
 * ============================================================================ */

/*
 * Bencode value type.
 */
typedef enum {
    WINGO_BENCODE_INTEGER  = 0,
    WINGO_BENCODE_STRING   = 1,
    WINGO_BENCODE_LIST     = 2,
    WINGO_BENCODE_DICT     = 3,
} wingo_bencode_type_t;

/* ============================================================================
 * BENCODE VALUE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Bencode value handle.
 */
typedef struct wingo_bencode wingo_bencode_t;

/* ============================================================================
 * BENCODE DECODE
 * ============================================================================ */

/*
 * Decode a Bencode value from buffer.
 *
 * @param data      Data to decode
 * @param len       Length of data
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_decode(const void *data, wingo_size len);

/*
 * Decode a Bencode value from buffer with position.
 *
 * @param data      Data to decode
 * @param len       Length of data
 * @param pos       Input/output position
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_decode_at(const void *data, wingo_size len,
                                          wingo_size *pos);

/*
 * Free a Bencode value.
 *
 * @param value     Bencode value (NULL is safe)
 */
void wingo_bencode_free(wingo_bencode_t *value);

/* ============================================================================
 * BENCODE QUERY
 * ============================================================================ */

/*
 * Get Bencode value type.
 *
 * @param value     Bencode value
 * @return          Value type
 */
wingo_bencode_type_t wingo_bencode_type(const wingo_bencode_t *value);

/*
 * Check if Bencode value is integer.
 *
 * @param value     Bencode value
 * @return          true if integer, false otherwise
 */
bool wingo_bencode_is_integer(const wingo_bencode_t *value);

/*
 * Check if Bencode value is string.
 *
 * @param value     Bencode value
 * @return          true if string, false otherwise
 */
bool wingo_bencode_is_string(const wingo_bencode_t *value);

/*
 * Check if Bencode value is list.
 *
 * @param value     Bencode value
 * @return          true if list, false otherwise
 */
bool wingo_bencode_is_list(const wingo_bencode_t *value);

/*
 * Check if Bencode value is dict.
 *
 * @param value     Bencode value
 * @return          true if dict, false otherwise
 */
bool wingo_bencode_is_dict(const wingo_bencode_t *value);

/* ============================================================================
 * BENCODE INTEGER
 * ============================================================================ */

/*
 * Create a Bencode integer.
 *
 * @param value     Integer value
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_new_integer(wingo_i64 value);

/*
 * Get integer value.
 *
 * @param value     Bencode value
 * @param out       Output integer
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_get_integer(const wingo_bencode_t *value,
                                         wingo_i64 *out);

/* ============================================================================
 * BENCODE STRING
 * ============================================================================ */

/*
 * Create a Bencode string.
 *
 * @param data      String data
 * @param len       Length of data
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_new_string(const void *data, wingo_size len);

/*
 * Create a Bencode string from C string.
 *
 * @param str       C string
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_new_cstring(const char *str);

/*
 * Get string data.
 *
 * @param value     Bencode value
 * @param out       Output data pointer
 * @param len       Output length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_get_string(const wingo_bencode_t *value,
                                        const void **out,
                                        wingo_size *len);

/*
 * Get string as C string (null-terminated).
 *
 * @param value     Bencode value
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_get_cstring(const wingo_bencode_t *value,
                                         char *buf,
                                         wingo_size size);

/*
 * Compare string with another string.
 *
 * @param value     Bencode value
 * @param str       String to compare
 * @param len       Length of string
 * @return          true if equal, false otherwise
 */
bool wingo_bencode_string_eq(const wingo_bencode_t *value,
                              const void *str,
                              wingo_size len);

/* ============================================================================
 * BENCODE LIST
 * ============================================================================ */

/*
 * Create an empty Bencode list.
 *
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_new_list(void);

/*
 * Append a value to list.
 *
 * @param list      List
 * @param value     Value to append
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_list_append(wingo_bencode_t *list,
                                         wingo_bencode_t *value);

/*
 * Get list length.
 *
 * @param list      List
 * @return          Number of items
 */
wingo_size wingo_bencode_list_len(const wingo_bencode_t *list);

/*
 * Get list item at index.
 *
 * @param list      List
 * @param index     Index
 * @return          Bencode value, or NULL if out of range
 */
const wingo_bencode_t *wingo_bencode_list_get(const wingo_bencode_t *list,
                                               wingo_size index);

/* ============================================================================
 * BENCODE DICTIONARY
 * ============================================================================ */

/*
 * Create an empty Bencode dictionary.
 *
 * @return          Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_new_dict(void);

/*
 * Set a key-value pair in dictionary.
 *
 * @param dict      Dictionary
 * @param key       Key (string)
 * @param value     Value
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_dict_set(wingo_bencode_t *dict,
                                      const char *key,
                                      wingo_bencode_t *value);

/*
 * Get a value from dictionary.
 *
 * @param dict      Dictionary
 * @param key       Key (string)
 * @return          Bencode value, or NULL if not found
 */
const wingo_bencode_t *wingo_bencode_dict_get(const wingo_bencode_t *dict,
                                               const char *key);

/*
 * Check if dictionary has a key.
 *
 * @param dict      Dictionary
 * @param key       Key (string)
 * @return          true if has key, false otherwise
 */
bool wingo_bencode_dict_has(const wingo_bencode_t *dict,
                             const char *key);

/*
 * Get dictionary size.
 *
 * @param dict      Dictionary
 * @return          Number of key-value pairs
 */
wingo_size wingo_bencode_dict_len(const wingo_bencode_t *dict);

/*
 * Get dictionary key at index.
 *
 * @param dict      Dictionary
 * @param index     Index
 * @return          Key, or NULL if out of range
 */
const char *wingo_bencode_dict_key(const wingo_bencode_t *dict,
                                    wingo_size index);

/*
 * Get dictionary value at index.
 *
 * @param dict      Dictionary
 * @param index     Index
 * @return          Value, or NULL if out of range
 */
const wingo_bencode_t *wingo_bencode_dict_value(const wingo_bencode_t *dict,
                                                 wingo_size index);

/* ============================================================================
 * BENCODE ENCODE
 * ============================================================================ */

/*
 * Encode a Bencode value to buffer.
 *
 * @param value     Bencode value
 * @param buf       Output buffer
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_encode(const wingo_bencode_t *value,
                                    wingo_buf_t *buf);

/*
 * Encode a Bencode value to newly allocated buffer.
 *
 * @param value     Bencode value
 * @return          New buffer, or NULL on error
 */
wingo_buf_t *wingo_bencode_encode_new(const wingo_bencode_t *value);

/*
 * Get encoded length of a Bencode value.
 *
 * @param value     Bencode value
 * @return          Encoded length
 */
wingo_size wingo_bencode_encoded_len(const wingo_bencode_t *value);

/* ============================================================================
 * BENCODE CONVENIENCE
 * ============================================================================ */

/*
 * Create a Bencode integer and set in dictionary.
 *
 * @param dict      Dictionary
 * @param key       Key
 * @param value     Integer value
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_dict_set_integer(wingo_bencode_t *dict,
                                              const char *key,
                                              wingo_i64 value);

/*
 * Create a Bencode string and set in dictionary.
 *
 * @param dict      Dictionary
 * @param key       Key
 * @param data      String data
 * @param len       Length of data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_dict_set_string(wingo_bencode_t *dict,
                                             const char *key,
                                             const void *data,
                                             wingo_size len);

/*
 * Get integer from dictionary.
 *
 * @param dict      Dictionary
 * @param key       Key
 * @param out       Output integer
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_dict_get_integer(const wingo_bencode_t *dict,
                                              const char *key,
                                              wingo_i64 *out);

/*
 * Get string from dictionary.
 *
 * @param dict      Dictionary
 * @param key       Key
 * @param out       Output data pointer
 * @param len       Output length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_bencode_dict_get_string(const wingo_bencode_t *dict,
                                             const char *key,
                                             const void **out,
                                             wingo_size *len);

/* ============================================================================
 * BENCODE UTILITY
 * ============================================================================ */

/*
 * Print Bencode value (for debugging).
 *
 * @param value     Bencode value
 * @param f         Output file (NULL = stderr)
 * @param indent    Indentation level
 */
void wingo_bencode_print(const wingo_bencode_t *value, FILE *f, int indent);

/*
 * Duplicate a Bencode value.
 *
 * @param value     Bencode value
 * @return          New Bencode value, or NULL on error
 */
wingo_bencode_t *wingo_bencode_dup(const wingo_bencode_t *value);

/*
 * Compare two Bencode values.
 *
 * @param a         First value
 * @param b         Second value
 * @return          true if equal, false otherwise
 */
bool wingo_bencode_eq(const wingo_bencode_t *a, const wingo_bencode_t *b);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_BENCODE_H */
