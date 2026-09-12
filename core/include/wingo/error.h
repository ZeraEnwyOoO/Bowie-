/*
 * Wingo — P2P Internet Sharing Tool
 * Copyright (C) 2024 Wingo Project
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

#ifndef WINGO_ERROR_H
#define WINGO_ERROR_H

/*
 * ============================================================================
 * WINGO ERROR HANDLING
 * ============================================================================
 *
 * This header provides:
 *   - Error codes
 *   - Error messages
 *   - Error handling functions
 *   - Error macros
 *
 * ============================================================================
 */

#include "wingo/common.h"

/* ============================================================================
 * ERROR CODES
 * ============================================================================ */

/*
 * Wingo error codes.
 *
 * All error codes are negative to distinguish from success (0) and
 * positive return values.
 *
 * Error codes are grouped by category:
 *   - Generic errors:      -1 to -99
 *   - Memory errors:       -100 to -199
 *   - Network errors:      -200 to -299
 *   - Crypto errors:       -300 to -399
 *   - DHT errors:          -400 to -499
 *   - Tunnel errors:       -500 to -599
 *   - Config errors:       -600 to -699
 *   - File errors:         -700 to -799
 *   - Protocol errors:     -800 to -899
 *   - Internal errors:     -900 to -999
 */

typedef enum {

    /* --------------------------------------------------------------------- */
    /* Success                                                                */
    /* --------------------------------------------------------------------- */
    WINGO_SUCCESS               = 0,

    /* --------------------------------------------------------------------- */
    /* Generic errors (-1 to -99)                                             */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_GENERIC           = -1,   /* Generic error */
    WINGO_ERR_INVALID_ARG       = -2,   /* Invalid argument */
    WINGO_ERR_INVALID_STATE     = -3,   /* Invalid state */
    WINGO_ERR_NOT_IMPLEMENTED   = -4,   /* Not implemented */
    WINGO_ERR_NOT_SUPPORTED     = -5,   /* Not supported */
    WINGO_ERR_BUSY              = -6,   /* Resource busy */
    WINGO_ERR_AGAIN             = -7,   /* Try again */
    WINGO_ERR_TIMEOUT           = -8,   /* Operation timed out */
    WINGO_ERR_CANCELED          = -9,   /* Operation canceled */
    WINGO_ERR_ABORTED           = -10,  /* Operation aborted */
    WINGO_ERR_OVERFLOW          = -11,  /* Buffer overflow */
    WINGO_ERR_UNDERFLOW         = -12,  /* Buffer underflow */
    WINGO_ERR_OUT_OF_RANGE      = -13,  /* Out of range */
    WINGO_ERR_ALREADY_EXISTS    = -14,  /* Already exists */
    WINGO_ERR_NOT_FOUND         = -15,  /* Not found */
    WINGO_ERR_PERMISSION        = -16,  /* Permission denied */
    WINGO_ERR_READ_ONLY         = -17,  /* Read-only */
    WINGO_ERR_WRITE_ONLY        = -18,  /* Write-only */
    WINGO_ERR_UNKNOWN           = -19,  /* Unknown error */

    /* --------------------------------------------------------------------- */
    /* Memory errors (-100 to -199)                                           */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_NOMEM             = -100, /* Out of memory */
    WINGO_ERR_NOMEM_POOL        = -101, /* Memory pool exhausted */
    WINGO_ERR_NOMEM_HEAP        = -102, /* Heap allocation failed */
    WINGO_ERR_NOMEM_STACK       = -103, /* Stack allocation failed */
    WINGO_ERR_MEM_CORRUPT       = -104, /* Memory corruption detected */
    WINGO_ERR_MEM_LEAK          = -105, /* Memory leak detected */
    WINGO_ERR_MEM_ALIGN         = -106, /* Memory alignment error */

    /* --------------------------------------------------------------------- */
    /* Network errors (-200 to -299)                                          */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_NET               = -200, /* Generic network error */
    WINGO_ERR_NET_SOCKET        = -201, /* Socket creation failed */
    WINGO_ERR_NET_BIND          = -202, /* Bind failed */
    WINGO_ERR_NET_LISTEN        = -203, /* Listen failed */
    WINGO_ERR_NET_ACCEPT        = -204, /* Accept failed */
    WINGO_ERR_NET_CONNECT       = -205, /* Connect failed */
    WINGO_ERR_NET_SEND          = -206, /* Send failed */
    WINGO_ERR_NET_RECV          = -207, /* Receive failed */
    WINGO_ERR_NET_CLOSE         = -208, /* Close failed */
    WINGO_ERR_NET_RESOLVE       = -209, /* Name resolution failed */
    WINGO_ERR_NET_UNREACHABLE   = -210, /* Network unreachable */
    WINGO_ERR_NET_HOST_UNREACH  = -211, /* Host unreachable */
    WINGO_ERR_NET_CONN_REFUSED  = -212, /* Connection refused */
    WINGO_ERR_NET_CONN_RESET    = -213, /* Connection reset */
    WINGO_ERR_NET_CONN_ABORTED  = -214, /* Connection aborted */
    WINGO_ERR_NET_CONN_TIMEOUT  = -215, /* Connection timed out */
    WINGO_ERR_NET_ALREADY_CONN  = -216, /* Already connected */
    WINGO_ERR_NET_NOT_CONN      = -217, /* Not connected */
    WINGO_ERR_NET_ADDR_IN_USE   = -218, /* Address already in use */
    WINGO_ERR_NET_ADDR_NOT_AVAIL= -219, /* Address not available */
    WINGO_ERR_NET_MSG_TOO_LONG  = -220, /* Message too long */
    WINGO_ERR_NET_NO_BUFS       = -221, /* No buffer space available */
    WINGO_ERR_NET_WOULD_BLOCK   = -222, /* Operation would block */
    WINGO_ERR_NET_IN_PROGRESS   = -223, /* Operation in progress */
    WINGO_ERR_NET_ALREADY       = -224, /* Operation already in progress */
    WINGO_ERR_NET_AF_NOT_SUPP   = -225, /* Address family not supported */
    WINGO_ERR_NET_PROTO_NOT_SUPP= -226, /* Protocol not supported */
    WINGO_ERR_NET_SOCK_NOT_SUPP = -227, /* Socket type not supported */

    /* --------------------------------------------------------------------- */
    /* Crypto errors (-300 to -399)                                           */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_CRYPTO            = -300, /* Generic crypto error */
    WINGO_ERR_CRYPTO_INIT       = -301, /* Crypto initialization failed */
    WINGO_ERR_CRYPTO_KEY        = -302, /* Invalid key */
    WINGO_ERR_CRYPTO_KEY_GEN    = -303, /* Key generation failed */
    WINGO_ERR_CRYPTO_KEY_EXCH   = -304, /* Key exchange failed */
    WINGO_ERR_CRYPTO_ENCRYPT    = -305, /* Encryption failed */
    WINGO_ERR_CRYPTO_DECRYPT    = -306, /* Decryption failed */
    WINGO_ERR_CRYPTO_HASH       = -307, /* Hash failed */
    WINGO_ERR_CRYPTO_SIGN       = -308, /* Signature failed */
    WINGO_ERR_CRYPTO_VERIFY     = -309, /* Verification failed */
    WINGO_ERR_CRYPTO_RANDOM     = -310, /* Random generation failed */
    WINGO_ERR_CRYPTO_AUTH       = -311, /* Authentication failed */
    WINGO_ERR_CRYPTO_TAG        = -312, /* Invalid authentication tag */
    WINGO_ERR_CRYPTO_IV         = -313, /* Invalid initialization vector */
    WINGO_ERR_CRYPTO_SALT       = -314, /* Invalid salt */
    WINGO_ERR_CRYPTO_NONCE      = -315, /* Invalid nonce */
    WINGO_ERR_CRYPTO_VERSION    = -316, /* Unsupported crypto version */
    WINGO_ERR_CRYPTO_ALGO       = -317, /* Unsupported algorithm */

    /* --------------------------------------------------------------------- */
    /* DHT errors (-400 to -499)                                              */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_DHT               = -400, /* Generic DHT error */
    WINGO_ERR_DHT_INIT          = -401, /* DHT initialization failed */
    WINGO_ERR_DHT_BUCKET        = -402, /* Bucket operation failed */
    WINGO_ERR_DHT_NODE          = -403, /* Node operation failed */
    WINGO_ERR_DHT_SEARCH        = -404, /* Search operation failed */
    WINGO_ERR_DHT_STORAGE       = -405, /* Storage operation failed */
    WINGO_ERR_DHT_TOKEN         = -406, /* Invalid token */
    WINGO_ERR_DHT_BLACKLIST     = -407, /* Node is blacklisted */
    WINGO_ERR_DHT_MARTIAN       = -408, /* Martian address */
    WINGO_ERR_DHT_PARSE         = -409, /* Message parse failed */
    WINGO_ERR_DHT_SERIALIZE     = -410, /* Message serialize failed */
    WINGO_ERR_DHT_TID           = -411, /* Invalid transaction ID */
    WINGO_ERR_DHT_NO_PEERS      = -412, /* No peers found */
    WINGO_ERR_DHT_FULL          = -413, /* DHT is full */
    WINGO_ERR_DHT_EXPIRED       = -414, /* DHT entry expired */

    /* --------------------------------------------------------------------- */
    /* Tunnel errors (-500 to -599)                                           */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_TUNNEL            = -500, /* Generic tunnel error */
    WINGO_ERR_TUNNEL_INIT       = -501, /* Tunnel initialization failed */
    WINGO_ERR_TUNNEL_OPEN       = -502, /* TUN/TAP open failed */
    WINGO_ERR_TUNNEL_CONFIG     = -503, /* TUN/TAP configuration failed */
    WINGO_ERR_TUNNEL_READ       = -504, /* Tunnel read failed */
    WINGO_ERR_TUNNEL_WRITE      = -505, /* Tunnel write failed */
    WINGO_ERR_TUNNEL_PACKET     = -506, /* Invalid packet */
    WINGO_ERR_TUNNEL_MTU        = -507, /* MTU exceeded */
    WINGO_ERR_TUNNEL_FRAG       = -508, /* Fragmentation failed */
    WINGO_ERR_TUNNEL_REASSEMBLE = -509, /* Reassembly failed */
    WINGO_ERR_TUNNEL_GATEWAY    = -510, /* Gateway error */
    WINGO_ERR_TUNNEL_ROUTE      = -511, /* Routing error */
    WINGO_ERR_TUNNEL_NO_ROUTE   = -512, /* No route to host */
    WINGO_ERR_TUNNEL_CLOSED     = -513, /* Tunnel closed */

    /* --------------------------------------------------------------------- */
    /* Config errors (-600 to -699)                                           */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_CONFIG            = -600, /* Generic config error */
    WINGO_ERR_CONFIG_PARSE      = -601, /* Config parse failed */
    WINGO_ERR_CONFIG_SYNTAX     = -602, /* Config syntax error */
    WINGO_ERR_CONFIG_MISSING    = -603, /* Required config missing */
    WINGO_ERR_CONFIG_INVALID    = -604, /* Invalid config value */
    WINGO_ERR_CONFIG_RANGE      = -605, /* Config value out of range */
    WINGO_ERR_CONFIG_TYPE       = -606, /* Config type mismatch */
    WINGO_ERR_CONFIG_DUPLICATE  = -607, /* Duplicate config entry */
    WINGO_ERR_CONFIG_UNKNOWN    = -608, /* Unknown config key */

    /* --------------------------------------------------------------------- */
    /* File errors (-700 to -799)                                             */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_FILE              = -700, /* Generic file error */
    WINGO_ERR_FILE_OPEN         = -701, /* File open failed */
    WINGO_ERR_FILE_CLOSE        = -702, /* File close failed */
    WINGO_ERR_FILE_READ         = -703, /* File read failed */
    WINGO_ERR_FILE_WRITE        = -704, /* File write failed */
    WINGO_ERR_FILE_SEEK         = -705, /* File seek failed */
    WINGO_ERR_FILE_STAT         = -706, /* File stat failed */
    WINGO_ERR_FILE_NOT_FOUND    = -707, /* File not found */
    WINGO_ERR_FILE_EXISTS       = -708, /* File already exists */
    WINGO_ERR_FILE_PERM         = -709, /* File permission denied */
    WINGO_ERR_FILE_TOO_LARGE    = -710, /* File too large */
    WINGO_ERR_FILE_EOF          = -711, /* End of file */

    /* --------------------------------------------------------------------- */
    /* Protocol errors (-800 to -899)                                         */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_PROTO             = -800, /* Generic protocol error */
    WINGO_ERR_PROTO_VERSION     = -801, /* Protocol version mismatch */
    WINGO_ERR_PROTO_HANDSHAKE   = -802, /* Handshake failed */
    WINGO_ERR_PROTO_MESSAGE     = -803, /* Invalid message */
    WINGO_ERR_PROTO_FRAMING     = -804, /* Framing error */
    WINGO_ERR_PROTO_SERIALIZE   = -805, /* Serialization failed */
    WINGO_ERR_PROTO_DESERIALIZE = -806, /* Deserialization failed */
    WINGO_ERR_PROTO_CHECKSUM    = -807, /* Checksum mismatch */
    WINGO_ERR_PROTO_SEQUENCE    = -808, /* Sequence error */
    WINGO_ERR_PROTO_ACK         = -809, /* Acknowledgment error */
    WINGO_ERR_PROTO_STATE       = -810, /* Protocol state error */
    WINGO_ERR_PROTO_PEER        = -811, /* Peer protocol error */

    /* --------------------------------------------------------------------- */
    /* Internal errors (-900 to -999)                                         */
    /* --------------------------------------------------------------------- */
    WINGO_ERR_INTERNAL          = -900, /* Internal error */
    WINGO_ERR_INTERNAL_ASSERT   = -901, /* Assertion failed */
    WINGO_ERR_INTERNAL_STATE    = -902, /* Internal state error */
    WINGO_ERR_INTERNAL_THREAD   = -903, /* Thread error */
    WINGO_ERR_INTERNAL_MUTEX    = -904, /* Mutex error */
    WINGO_ERR_INTERNAL_COND     = -905, /* Condition variable error */
    WINGO_ERR_INTERNAL_ATOMIC   = -906, /* Atomic operation error */
    WINGO_ERR_INTERNAL_PANIC    = -907, /* Panic */
    WINGO_ERR_INTERNAL_BUG      = -908, /* Bug detected */

} wingo_error_t;

/* ============================================================================
 * ERROR TYPE
 * ============================================================================ */

/*
 * Error context structure.
 *
 * This structure holds detailed information about an error,
 * including the error code, message, file, line, and function.
 */

#define WINGO_ERROR_MESSAGE_SIZE    256
#define WINGO_ERROR_CONTEXT_SIZE    64

typedef struct {
    wingo_error_t   code;                               /* Error code */
    char            message[WINGO_ERROR_MESSAGE_SIZE];  /* Error message */
    char            file[WINGO_ERROR_CONTEXT_SIZE];     /* Source file */
    int             line;                               /* Source line */
    char            func[WINGO_ERROR_CONTEXT_SIZE];     /* Function name */
    wingo_i64       timestamp;                          /* When error occurred */
} wingo_error_context_t;

/* ============================================================================
 * ERROR FUNCTIONS
 * ============================================================================ */

/*
 * Get error message for an error code.
 *
 * @param code  Error code
 * @return      Static error message string
 */

const char *wingo_error_str(wingo_error_t code);

/*
 * Get error name for an error code.
 *
 * @param code  Error code
 * @return      Static error name string (e.g., "WINGO_ERR_NOMEM")
 */

const char *wingo_error_name(wingo_error_t code);

/*
 * Set error context.
 *
 * @param ctx   Error context to fill
 * @param code  Error code
 * @param file  Source file
 * @param line  Source line
 * @param func  Function name
 * @param fmt   Format string for message
 * @param ...   Format arguments
 */

void wingo_error_set(wingo_error_context_t *ctx,
                     wingo_error_t code,
                     const char *file,
                     int line,
                     const char *func,
                     const char *fmt, ...)
    WINGO_ATTR_FORMAT(6, 7);

/*
 * Clear error context.
 *
 * @param ctx   Error context to clear
 */

void wingo_error_clear(wingo_error_context_t *ctx);

/*
 * Print error context to stderr.
 *
 * @param ctx   Error context to print
 */

void wingo_error_print(const wingo_error_context_t *ctx);

/*
 * Format error context to string.
 *
 * @param ctx   Error context
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return      Number of bytes written (excluding null terminator)
 */

int wingo_error_format(const wingo_error_context_t *ctx,
                       char *buf, wingo_size size);

/*
 * Check if error code indicates success.
 *
 * @param code  Error code
 * @return      true if success, false otherwise
 */

static inline bool wingo_error_is_ok(wingo_error_t code)
{
    return code == WINGO_SUCCESS;
}

/*
 * Check if error code indicates failure.
 *
 * @param code  Error code
 * @return      true if failure, false otherwise
 */

static inline bool wingo_error_is_err(wingo_error_t code)
{
    return code != WINGO_SUCCESS;
}

/* ============================================================================
 * ERROR MACROS
 * ============================================================================ */

/*
 * Set error context with automatic file/line/func.
 */

#define WINGO_ERROR_SET(ctx, code, ...) \
    wingo_error_set((ctx), (code), __FILE__, __LINE__, __func__, __VA_ARGS__)

/*
 * Return if error.
 */

#define WINGO_RETURN_IF_ERR(expr) \
    do { \
        wingo_error_t _rc = (expr); \
        if (wingo_error_is_err(_rc)) \
            return _rc; \
    } while (0)

/*
 * Return if error with context.
 */

#define WINGO_RETURN_IF_ERR_CTX(ctx, expr) \
    do { \
        wingo_error_t _rc = (expr); \
        if (wingo_error_is_err(_rc)) { \
            WINGO_ERROR_SET((ctx), _rc, "error from %s", #expr); \
            return _rc; \
        } \
    } while (0)

/*
 * Return if null.
 */

#define WINGO_RETURN_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
            return WINGO_ERR_INVALID_ARG; \
    } while (0)

/*
 * Return if condition is true.
 */

#define WINGO_RETURN_IF(cond, err) \
    do { \
        if ((cond)) \
            return (err); \
    } while (0)

/*
 * Goto label if error.
 */

#define WINGO_GOTO_IF_ERR(label, expr) \
    do { \
        wingo_error_t _rc = (expr); \
        if (wingo_error_is_err(_rc)) \
            goto label; \
    } while (0)

/*
 * Goto label if null.
 */

#define WINGO_GOTO_IF_NULL(label, ptr) \
    do { \
        if ((ptr) == NULL) \
            goto label; \
    } while (0)

/*
 * Assert (always active, not compiled out).
 */

#define WINGO_ASSERT(cond) \
    do { \
        if (WINGO_UNLIKELY(!(cond))) { \
            fprintf(stderr, "WINGO ASSERTION FAILED: %s\n", #cond); \
            fprintf(stderr, "  File: %s\n", __FILE__); \
            fprintf(stderr, "  Line: %d\n", __LINE__); \
            fprintf(stderr, "  Func: %s\n", __func__); \
            abort(); \
        } \
    } while (0)

/*
 * Assert with message.
 */

#define WINGO_ASSERT_MSG(cond, fmt, ...) \
    do { \
        if (WINGO_UNLIKELY(!(cond))) { \
            fprintf(stderr, "WINGO ASSERTION FAILED: %s\n", #cond); \
            fprintf(stderr, "  Message: " fmt "\n", ##__VA_ARGS__); \
            fprintf(stderr, "  File: %s\n", __FILE__); \
            fprintf(stderr, "  Line: %d\n", __LINE__); \
            fprintf(stderr, "  Func: %s\n", __func__); \
            abort(); \
        } \
    } while (0)

/*
 * Compile-time assert (C11).
 */

#define WINGO_STATIC_ASSERT(cond, msg) \
    _Static_assert(cond, #msg)

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_ERROR_H */
