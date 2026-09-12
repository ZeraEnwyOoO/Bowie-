
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

#include "wingo/error.h"
#include "wingo/util/time.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * ERROR MESSAGES
 * ============================================================================ */

/*
 * Static error message table.
 *
 * Indexed by error code offset. We use a switch statement
 * instead of a table because:
 *   - Error codes are negative
 *   - Switch is more readable
 *   - Compiler can optimize
 */
const char *wingo_error_str(wingo_error_t code)
{
    switch (code) {
    /* Success */
    case WINGO_SUCCESS:
        return "Success";

    /* --------------------------------------------------------------------- */
    /* Generic errors (-1 to -99)                                             */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_GENERIC:
        return "Generic error";
    case WINGO_ERR_INVALID_ARG:
        return "Invalid argument";
    case WINGO_ERR_INVALID_STATE:
        return "Invalid state";
    case WINGO_ERR_NOT_IMPLEMENTED:
        return "Not implemented";
    case WINGO_ERR_NOT_SUPPORTED:
        return "Not supported";
    case WINGO_ERR_BUSY:
        return "Resource busy";
    case WINGO_ERR_AGAIN:
        return "Try again";
    case WINGO_ERR_TIMEOUT:
        return "Operation timed out";
    case WINGO_ERR_CANCELED:
        return "Operation canceled";
    case WINGO_ERR_ABORTED:
        return "Operation aborted";
    case WINGO_ERR_OVERFLOW:
        return "Buffer overflow";
    case WINGO_ERR_UNDERFLOW:
        return "Buffer underflow";
    case WINGO_ERR_OUT_OF_RANGE:
        return "Out of range";
    case WINGO_ERR_ALREADY_EXISTS:
        return "Already exists";
    case WINGO_ERR_NOT_FOUND:
        return "Not found";
    case WINGO_ERR_PERMISSION:
        return "Permission denied";
    case WINGO_ERR_READ_ONLY:
        return "Read-only";
    case WINGO_ERR_WRITE_ONLY:
        return "Write-only";
    case WINGO_ERR_UNKNOWN:
        return "Unknown error";

    /* --------------------------------------------------------------------- */
    /* Memory errors (-100 to -199)                                           */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_NOMEM:
        return "Out of memory";
    case WINGO_ERR_NOMEM_POOL:
        return "Memory pool exhausted";
    case WINGO_ERR_NOMEM_HEAP:
        return "Heap allocation failed";
    case WINGO_ERR_NOMEM_STACK:
        return "Stack allocation failed";
    case WINGO_ERR_MEM_CORRUPT:
        return "Memory corruption detected";
    case WINGO_ERR_MEM_LEAK:
        return "Memory leak detected";
    case WINGO_ERR_MEM_ALIGN:
        return "Memory alignment error";

    /* --------------------------------------------------------------------- */
    /* Network errors (-200 to -299)                                          */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_NET:
        return "Generic network error";
    case WINGO_ERR_NET_SOCKET:
        return "Socket creation failed";
    case WINGO_ERR_NET_BIND:
        return "Bind failed";
    case WINGO_ERR_NET_LISTEN:
        return "Listen failed";
    case WINGO_ERR_NET_ACCEPT:
        return "Accept failed";
    case WINGO_ERR_NET_CONNECT:
        return "Connect failed";
    case WINGO_ERR_NET_SEND:
        return "Send failed";
    case WINGO_ERR_NET_RECV:
        return "Receive failed";
    case WINGO_ERR_NET_CLOSE:
        return "Close failed";
    case WINGO_ERR_NET_RESOLVE:
        return "Name resolution failed";
    case WINGO_ERR_NET_UNREACHABLE:
        return "Network unreachable";
    case WINGO_ERR_NET_HOST_UNREACH:
        return "Host unreachable";
    case WINGO_ERR_NET_CONN_REFUSED:
        return "Connection refused";
    case WINGO_ERR_NET_CONN_RESET:
        return "Connection reset";
    case WINGO_ERR_NET_CONN_ABORTED:
        return "Connection aborted";
    case WINGO_ERR_NET_CONN_TIMEOUT:
        return "Connection timed out";
    case WINGO_ERR_NET_ALREADY_CONN:
        return "Already connected";
    case WINGO_ERR_NET_NOT_CONN:
        return "Not connected";
    case WINGO_ERR_NET_ADDR_IN_USE:
        return "Address already in use";
    case WINGO_ERR_NET_ADDR_NOT_AVAIL:
        return "Address not available";
    case WINGO_ERR_NET_MSG_TOO_LONG:
        return "Message too long";
    case WINGO_ERR_NET_NO_BUFS:
        return "No buffer space available";
    case WINGO_ERR_NET_WOULD_BLOCK:
        return "Operation would block";
    case WINGO_ERR_NET_IN_PROGRESS:
        return "Operation in progress";
    case WINGO_ERR_NET_ALREADY:
        return "Operation already in progress";
    case WINGO_ERR_NET_AF_NOT_SUPP:
        return "Address family not supported";
    case WINGO_ERR_NET_PROTO_NOT_SUPP:
        return "Protocol not supported";
    case WINGO_ERR_NET_SOCK_NOT_SUPP:
        return "Socket type not supported";

    /* --------------------------------------------------------------------- */
    /* Crypto errors (-300 to -399)                                           */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_CRYPTO:
        return "Generic crypto error";
    case WINGO_ERR_CRYPTO_INIT:
        return "Crypto initialization failed";
    case WINGO_ERR_CRYPTO_KEY:
        return "Invalid key";
    case WINGO_ERR_CRYPTO_KEY_GEN:
        return "Key generation failed";
    case WINGO_ERR_CRYPTO_KEY_EXCH:
        return "Key exchange failed";
    case WINGO_ERR_CRYPTO_ENCRYPT:
        return "Encryption failed";
    case WINGO_ERR_CRYPTO_DECRYPT:
        return "Decryption failed";
    case WINGO_ERR_CRYPTO_HASH:
        return "Hash failed";
    case WINGO_ERR_CRYPTO_SIGN:
        return "Signature failed";
    case WINGO_ERR_CRYPTO_VERIFY:
        return "Verification failed";
    case WINGO_ERR_CRYPTO_RANDOM:
        return "Random generation failed";
    case WINGO_ERR_CRYPTO_AUTH:
        return "Authentication failed";
    case WINGO_ERR_CRYPTO_TAG:
        return "Invalid authentication tag";
    case WINGO_ERR_CRYPTO_IV:
        return "Invalid initialization vector";
    case WINGO_ERR_CRYPTO_SALT:
        return "Invalid salt";
    case WINGO_ERR_CRYPTO_NONCE:
        return "Invalid nonce";
    case WINGO_ERR_CRYPTO_VERSION:
        return "Unsupported crypto version";
    case WINGO_ERR_CRYPTO_ALGO:
        return "Unsupported algorithm";

    /* --------------------------------------------------------------------- */
    /* DHT errors (-400 to -499)                                              */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_DHT:
        return "Generic DHT error";
    case WINGO_ERR_DHT_INIT:
        return "DHT initialization failed";
    case WINGO_ERR_DHT_BUCKET:
        return "Bucket operation failed";
    case WINGO_ERR_DHT_NODE:
        return "Node operation failed";
    case WINGO_ERR_DHT_SEARCH:
        return "Search operation failed";
    case WINGO_ERR_DHT_STORAGE:
        return "Storage operation failed";
    case WINGO_ERR_DHT_TOKEN:
        return "Invalid token";
    case WINGO_ERR_DHT_BLACKLIST:
        return "Node is blacklisted";
    case WINGO_ERR_DHT_MARTIAN:
        return "Martian address";
    case WINGO_ERR_DHT_PARSE:
        return "Message parse failed";
    case WINGO_ERR_DHT_SERIALIZE:
        return "Message serialize failed";
    case WINGO_ERR_DHT_TID:
        return "Invalid transaction ID";
    case WINGO_ERR_DHT_NO_PEERS:
        return "No peers found";
    case WINGO_ERR_DHT_FULL:
        return "DHT is full";
    case WINGO_ERR_DHT_EXPIRED:
        return "DHT entry expired";

    /* --------------------------------------------------------------------- */
    /* Tunnel errors (-500 to -599)                                           */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_TUNNEL:
        return "Generic tunnel error";
    case WINGO_ERR_TUNNEL_INIT:
        return "Tunnel initialization failed";
    case WINGO_ERR_TUNNEL_OPEN:
        return "TUN/TAP open failed";
    case WINGO_ERR_TUNNEL_CONFIG:
        return "TUN/TAP configuration failed";
    case WINGO_ERR_TUNNEL_READ:
        return "Tunnel read failed";
    case WINGO_ERR_TUNNEL_WRITE:
        return "Tunnel write failed";
    case WINGO_ERR_TUNNEL_PACKET:
        return "Invalid packet";
    case WINGO_ERR_TUNNEL_MTU:
        return "MTU exceeded";
    case WINGO_ERR_TUNNEL_FRAG:
        return "Fragmentation failed";
    case WINGO_ERR_TUNNEL_REASSEMBLE:
        return "Reassembly failed";
    case WINGO_ERR_TUNNEL_GATEWAY:
        return "Gateway error";
    case WINGO_ERR_TUNNEL_ROUTE:
        return "Routing error";
    case WINGO_ERR_TUNNEL_NO_ROUTE:
        return "No route to host";
    case WINGO_ERR_TUNNEL_CLOSED:
        return "Tunnel closed";

    /* --------------------------------------------------------------------- */
    /* Config errors (-600 to -699)                                           */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_CONFIG:
        return "Generic config error";
    case WINGO_ERR_CONFIG_PARSE:
        return "Config parse failed";
    case WINGO_ERR_CONFIG_SYNTAX:
        return "Config syntax error";
    case WINGO_ERR_CONFIG_MISSING:
        return "Required config missing";
    case WINGO_ERR_CONFIG_INVALID:
        return "Invalid config value";
    case WINGO_ERR_CONFIG_RANGE:
        return "Config value out of range";
    case WINGO_ERR_CONFIG_TYPE:
        return "Config type mismatch";
    case WINGO_ERR_CONFIG_DUPLICATE:
        return "Duplicate config entry";
    case WINGO_ERR_CONFIG_UNKNOWN:
        return "Unknown config key";

    /* --------------------------------------------------------------------- */
    /* File errors (-700 to -799)                                             */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_FILE:
        return "Generic file error";
    case WINGO_ERR_FILE_OPEN:
        return "File open failed";
    case WINGO_ERR_FILE_CLOSE:
        return "File close failed";
    case WINGO_ERR_FILE_READ:
        return "File read failed";
    case WINGO_ERR_FILE_WRITE:
        return "File write failed";
    case WINGO_ERR_FILE_SEEK:
        return "File seek failed";
    case WINGO_ERR_FILE_STAT:
        return "File stat failed";
    case WINGO_ERR_FILE_NOT_FOUND:
        return "File not found";
    case WINGO_ERR_FILE_EXISTS:
        return "File already exists";
    case WINGO_ERR_FILE_PERM:
        return "File permission denied";
    case WINGO_ERR_FILE_TOO_LARGE:
        return "File too large";
    case WINGO_ERR_FILE_EOF:
        return "End of file";

    /* --------------------------------------------------------------------- */
    /* Protocol errors (-800 to -899)                                         */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_PROTO:
        return "Generic protocol error";
    case WINGO_ERR_PROTO_VERSION:
        return "Protocol version mismatch";
    case WINGO_ERR_PROTO_HANDSHAKE:
        return "Handshake failed";
    case WINGO_ERR_PROTO_MESSAGE:
        return "Invalid message";
    case WINGO_ERR_PROTO_FRAMING:
        return "Framing error";
    case WINGO_ERR_PROTO_SERIALIZE:
        return "Serialization failed";
    case WINGO_ERR_PROTO_DESERIALIZE:
        return "Deserialization failed";
    case WINGO_ERR_PROTO_CHECKSUM:
        return "Checksum mismatch";
    case WINGO_ERR_PROTO_SEQUENCE:
        return "Sequence error";
    case WINGO_ERR_PROTO_ACK:
        return "Acknowledgment error";
    case WINGO_ERR_PROTO_STATE:
        return "Protocol state error";
    case WINGO_ERR_PROTO_PEER:
        return "Peer protocol error";

    /* --------------------------------------------------------------------- */
    /* Internal errors (-900 to -999)                                         */
    /* --------------------------------------------------------------------- */
    case WINGO_ERR_INTERNAL:
        return "Internal error";
    case WINGO_ERR_INTERNAL_ASSERT:
        return "Assertion failed";
    case WINGO_ERR_INTERNAL_STATE:
        return "Internal state error";
    case WINGO_ERR_INTERNAL_THREAD:
        return "Thread error";
    case WINGO_ERR_INTERNAL_MUTEX:
        return "Mutex error";
    case WINGO_ERR_INTERNAL_COND:
        return "Condition variable error";
    case WINGO_ERR_INTERNAL_ATOMIC:
        return "Atomic operation error";
    case WINGO_ERR_INTERNAL_PANIC:
        return "Panic";
    case WINGO_ERR_INTERNAL_BUG:
        return "Bug detected";

    default:
        return "Unknown error code";
    }
}

/* ============================================================================
 * ERROR NAMES
 * ============================================================================ */

/*
 * Get error name as string.
 *
 * This is useful for logging — it gives the enum name
 * so you can grep for it.
 */
const char *wingo_error_name(wingo_error_t code)
{
    switch (code) {
    case WINGO_SUCCESS:             return "WINGO_SUCCESS";

    /* Generic */
    case WINGO_ERR_GENERIC:         return "WINGO_ERR_GENERIC";
    case WINGO_ERR_INVALID_ARG:     return "WINGO_ERR_INVALID_ARG";
    case WINGO_ERR_INVALID_STATE:   return "WINGO_ERR_INVALID_STATE";
    case WINGO_ERR_NOT_IMPLEMENTED: return "WINGO_ERR_NOT_IMPLEMENTED";
    case WINGO_ERR_NOT_SUPPORTED:   return "WINGO_ERR_NOT_SUPPORTED";
    case WINGO_ERR_BUSY:            return "WINGO_ERR_BUSY";
    case WINGO_ERR_AGAIN:           return "WINGO_ERR_AGAIN";
    case WINGO_ERR_TIMEOUT:         return "WINGO_ERR_TIMEOUT";
    case WINGO_ERR_CANCELED:        return "WINGO_ERR_CANCELED";
    case WINGO_ERR_ABORTED:         return "WINGO_ERR_ABORTED";
    case WINGO_ERR_OVERFLOW:        return "WINGO_ERR_OVERFLOW";
    case WINGO_ERR_UNDERFLOW:       return "WINGO_ERR_UNDERFLOW";
    case WINGO_ERR_OUT_OF_RANGE:    return "WINGO_ERR_OUT_OF_RANGE";
    case WINGO_ERR_ALREADY_EXISTS:  return "WINGO_ERR_ALREADY_EXISTS";
    case WINGO_ERR_NOT_FOUND:       return "WINGO_ERR_NOT_FOUND";
    case WINGO_ERR_PERMISSION:      return "WINGO_ERR_PERMISSION";
    case WINGO_ERR_READ_ONLY:       return "WINGO_ERR_READ_ONLY";
    case WINGO_ERR_WRITE_ONLY:      return "WINGO_ERR_WRITE_ONLY";
    case WINGO_ERR_UNKNOWN:         return "WINGO_ERR_UNKNOWN";

    /* Memory */
    case WINGO_ERR_NOMEM:           return "WINGO_ERR_NOMEM";
    case WINGO_ERR_NOMEM_POOL:      return "WINGO_ERR_NOMEM_POOL";
    case WINGO_ERR_NOMEM_HEAP:      return "WINGO_ERR_NOMEM_HEAP";
    case WINGO_ERR_NOMEM_STACK:     return "WINGO_ERR_NOMEM_STACK";
    case WINGO_ERR_MEM_CORRUPT:     return "WINGO_ERR_MEM_CORRUPT";
    case WINGO_ERR_MEM_LEAK:        return "WINGO_ERR_MEM_LEAK";
    case WINGO_ERR_MEM_ALIGN:       return "WINGO_ERR_MEM_ALIGN";

    /* Network */
    case WINGO_ERR_NET:             return "WINGO_ERR_NET";
    case WINGO_ERR_NET_SOCKET:      return "WINGO_ERR_NET_SOCKET";
    case WINGO_ERR_NET_BIND:        return "WINGO_ERR_NET_BIND";
    case WINGO_ERR_NET_LISTEN:      return "WINGO_ERR_NET_LISTEN";
    case WINGO_ERR_NET_ACCEPT:      return "WINGO_ERR_NET_ACCEPT";
    case WINGO_ERR_NET_CONNECT:     return "WINGO_ERR_NET_CONNECT";
    case WINGO_ERR_NET_SEND:        return "WINGO_ERR_NET_SEND";
    case WINGO_ERR_NET_RECV:        return "WINGO_ERR_NET_RECV";
    case WINGO_ERR_NET_CLOSE:       return "WINGO_ERR_NET_CLOSE";
    case WINGO_ERR_NET_RESOLVE:     return "WINGO_ERR_NET_RESOLVE";
    case WINGO_ERR_NET_UNREACHABLE: return "WINGO_ERR_NET_UNREACHABLE";
    case WINGO_ERR_NET_HOST_UNREACH:return "WINGO_ERR_NET_HOST_UNREACH";
    case WINGO_ERR_NET_CONN_REFUSED:return "WINGO_ERR_NET_CONN_REFUSED";
    case WINGO_ERR_NET_CONN_RESET:  return "WINGO_ERR_NET_CONN_RESET";
    case WINGO_ERR_NET_CONN_ABORTED:return "WINGO_ERR_NET_CONN_ABORTED";
    case WINGO_ERR_NET_CONN_TIMEOUT:return "WINGO_ERR_NET_CONN_TIMEOUT";
    case WINGO_ERR_NET_ALREADY_CONN:return "WINGO_ERR_NET_ALREADY_CONN";
    case WINGO_ERR_NET_NOT_CONN:    return "WINGO_ERR_NET_NOT_CONN";
    case WINGO_ERR_NET_ADDR_IN_USE: return "WINGO_ERR_NET_ADDR_IN_USE";
    case WINGO_ERR_NET_ADDR_NOT_AVAIL: return "WINGO_ERR_NET_ADDR_NOT_AVAIL";
    case WINGO_ERR_NET_MSG_TOO_LONG:return "WINGO_ERR_NET_MSG_TOO_LONG";
    case WINGO_ERR_NET_NO_BUFS:     return "WINGO_ERR_NET_NO_BUFS";
    case WINGO_ERR_NET_WOULD_BLOCK: return "WINGO_ERR_NET_WOULD_BLOCK";
    case WINGO_ERR_NET_IN_PROGRESS: return "WINGO_ERR_NET_IN_PROGRESS";
    case WINGO_ERR_NET_ALREADY:     return "WINGO_ERR_NET_ALREADY";
    case WINGO_ERR_NET_AF_NOT_SUPP: return "WINGO_ERR_NET_AF_NOT_SUPP";
    case WINGO_ERR_NET_PROTO_NOT_SUPP: return "WINGO_ERR_NET_PROTO_NOT_SUPP";
    case WINGO_ERR_NET_SOCK_NOT_SUPP: return "WINGO_ERR_NET_SOCK_NOT_SUPP";

    /* Crypto */
    case WINGO_ERR_CRYPTO:          return "WINGO_ERR_CRYPTO";
    case WINGO_ERR_CRYPTO_INIT:     return "WINGO_ERR_CRYPTO_INIT";
    case WINGO_ERR_CRYPTO_KEY:      return "WINGO_ERR_CRYPTO_KEY";
    case WINGO_ERR_CRYPTO_KEY_GEN:  return "WINGO_ERR_CRYPTO_KEY_GEN";
    case WINGO_ERR_CRYPTO_KEY_EXCH: return "WINGO_ERR_CRYPTO_KEY_EXCH";
    case WINGO_ERR_CRYPTO_ENCRYPT:  return "WINGO_ERR_CRYPTO_ENCRYPT";
    case WINGO_ERR_CRYPTO_DECRYPT:  return "WINGO_ERR_CRYPTO_DECRYPT";
    case WINGO_ERR_CRYPTO_HASH:     return "WINGO_ERR_CRYPTO_HASH";
    case WINGO_ERR_CRYPTO_SIGN:     return "WINGO_ERR_CRYPTO_SIGN";
    case WINGO_ERR_CRYPTO_VERIFY:   return "WINGO_ERR_CRYPTO_VERIFY";
    case WINGO_ERR_CRYPTO_RANDOM:   return "WINGO_ERR_CRYPTO_RANDOM";
    case WINGO_ERR_CRYPTO_AUTH:     return "WINGO_ERR_CRYPTO_AUTH";
    case WINGO_ERR_CRYPTO_TAG:      return "WINGO_ERR_CRYPTO_TAG";
    case WINGO_ERR_CRYPTO_IV:       return "WINGO_ERR_CRYPTO_IV";
    case WINGO_ERR_CRYPTO_SALT:     return "WINGO_ERR_CRYPTO_SALT";
    case WINGO_ERR_CRYPTO_NONCE:    return "WINGO_ERR_CRYPTO_NONCE";
    case WINGO_ERR_CRYPTO_VERSION:  return "WINGO_ERR_CRYPTO_VERSION";
    case WINGO_ERR_CRYPTO_ALGO:     return "WINGO_ERR_CRYPTO_ALGO";

    /* DHT */
    case WINGO_ERR_DHT:             return "WINGO_ERR_DHT";
    case WINGO_ERR_DHT_INIT:        return "WINGO_ERR_DHT_INIT";
    case WINGO_ERR_DHT_BUCKET:      return "WINGO_ERR_DHT_BUCKET";
    case WINGO_ERR_DHT_NODE:        return "WINGO_ERR_DHT_NODE";
    case WINGO_ERR_DHT_SEARCH:      return "WINGO_ERR_DHT_SEARCH";
    case WINGO_ERR_DHT_STORAGE:     return "WINGO_ERR_DHT_STORAGE";
    case WINGO_ERR_DHT_TOKEN:       return "WINGO_ERR_DHT_TOKEN";
    case WINGO_ERR_DHT_BLACKLIST:   return "WINGO_ERR_DHT_BLACKLIST";
    case WINGO_ERR_DHT_MARTIAN:     return "WINGO_ERR_DHT_MARTIAN";
    case WINGO_ERR_DHT_PARSE:       return "WINGO_ERR_DHT_PARSE";
    case WINGO_ERR_DHT_SERIALIZE:   return "WINGO_ERR_DHT_SERIALIZE";
    case WINGO_ERR_DHT_TID:         return "WINGO_ERR_DHT_TID";
    case WINGO_ERR_DHT_NO_PEERS:    return "WINGO_ERR_DHT_NO_PEERS";
    case WINGO_ERR_DHT_FULL:        return "WINGO_ERR_DHT_FULL";
    case WINGO_ERR_DHT_EXPIRED:     return "WINGO_ERR_DHT_EXPIRED";

    /* Tunnel */
    case WINGO_ERR_TUNNEL:          return "WINGO_ERR_TUNNEL";
    case WINGO_ERR_TUNNEL_INIT:     return "WINGO_ERR_TUNNEL_INIT";
    case WINGO_ERR_TUNNEL_OPEN:     return "WINGO_ERR_TUNNEL_OPEN";
    case WINGO_ERR_TUNNEL_CONFIG:   return "WINGO_ERR_TUNNEL_CONFIG";
    case WINGO_ERR_TUNNEL_READ:     return "WINGO_ERR_TUNNEL_READ";
    case WINGO_ERR_TUNNEL_WRITE:    return "WINGO_ERR_TUNNEL_WRITE";
    case WINGO_ERR_TUNNEL_PACKET:   return "WINGO_ERR_TUNNEL_PACKET";
    case WINGO_ERR_TUNNEL_MTU:      return "WINGO_ERR_TUNNEL_MTU";
    case WINGO_ERR_TUNNEL_FRAG:     return "WINGO_ERR_TUNNEL_FRAG";
    case WINGO_ERR_TUNNEL_REASSEMBLE: return "WINGO_ERR_TUNNEL_REASSEMBLE";
    case WINGO_ERR_TUNNEL_GATEWAY:  return "WINGO_ERR_TUNNEL_GATEWAY";
    case WINGO_ERR_TUNNEL_ROUTE:    return "WINGO_ERR_TUNNEL_ROUTE";
    case WINGO_ERR_TUNNEL_NO_ROUTE: return "WINGO_ERR_TUNNEL_NO_ROUTE";
    case WINGO_ERR_TUNNEL_CLOSED:   return "WINGO_ERR_TUNNEL_CLOSED";

    /* Config */
    case WINGO_ERR_CONFIG:          return "WINGO_ERR_CONFIG";
    case WINGO_ERR_CONFIG_PARSE:    return "WINGO_ERR_CONFIG_PARSE";
    case WINGO_ERR_CONFIG_SYNTAX:   return "WINGO_ERR_CONFIG_SYNTAX";
    case WINGO_ERR_CONFIG_MISSING:  return "WINGO_ERR_CONFIG_MISSING";
    case WINGO_ERR_CONFIG_INVALID:  return "WINGO_ERR_CONFIG_INVALID";
    case WINGO_ERR_CONFIG_RANGE:    return "WINGO_ERR_CONFIG_RANGE";
    case WINGO_ERR_CONFIG_TYPE:     return "WINGO_ERR_CONFIG_TYPE";
    case WINGO_ERR_CONFIG_DUPLICATE:return "WINGO_ERR_CONFIG_DUPLICATE";
    case WINGO_ERR_CONFIG_UNKNOWN:  return "WINGO_ERR_CONFIG_UNKNOWN";

    /* File */
    case WINGO_ERR_FILE:            return "WINGO_ERR_FILE";
    case WINGO_ERR_FILE_OPEN:       return "WINGO_ERR_FILE_OPEN";
    case WINGO_ERR_FILE_CLOSE:      return "WINGO_ERR_FILE_CLOSE";
    case WINGO_ERR_FILE_READ:       return "WINGO_ERR_FILE_READ";
    case WINGO_ERR_FILE_WRITE:      return "WINGO_ERR_FILE_WRITE";
    case WINGO_ERR_FILE_SEEK:       return "WINGO_ERR_FILE_SEEK";
    case WINGO_ERR_FILE_STAT:       return "WINGO_ERR_FILE_STAT";
    case WINGO_ERR_FILE_NOT_FOUND:  return "WINGO_ERR_FILE_NOT_FOUND";
    case WINGO_ERR_FILE_EXISTS:     return "WINGO_ERR_FILE_EXISTS";
    case WINGO_ERR_FILE_PERM:       return "WINGO_ERR_FILE_PERM";
    case WINGO_ERR_FILE_TOO_LARGE:  return "WINGO_ERR_FILE_TOO_LARGE";
    case WINGO_ERR_FILE_EOF:        return "WINGO_ERR_FILE_EOF";

    /* Protocol */
    case WINGO_ERR_PROTO:           return "WINGO_ERR_PROTO";
    case WINGO_ERR_PROTO_VERSION:   return "WINGO_ERR_PROTO_VERSION";
    case WINGO_ERR_PROTO_HANDSHAKE: return "WINGO_ERR_PROTO_HANDSHAKE";
    case WINGO_ERR_PROTO_MESSAGE:   return "WINGO_ERR_PROTO_MESSAGE";
    case WINGO_ERR_PROTO_FRAMING:   return "WINGO_ERR_PROTO_FRAMING";
    case WINGO_ERR_PROTO_SERIALIZE: return "WINGO_ERR_PROTO_SERIALIZE";
    case WINGO_ERR_PROTO_DESERIALIZE: return "WINGO_ERR_PROTO_DESERIALIZE";
    case WINGO_ERR_PROTO_CHECKSUM:  return "WINGO_ERR_PROTO_CHECKSUM";
    case WINGO_ERR_PROTO_SEQUENCE:  return "WINGO_ERR_PROTO_SEQUENCE";
    case WINGO_ERR_PROTO_ACK:       return "WINGO_ERR_PROTO_ACK";
    case WINGO_ERR_PROTO_STATE:     return "WINGO_ERR_PROTO_STATE";
    case WINGO_ERR_PROTO_PEER:      return "WINGO_ERR_PROTO_PEER";

    /* Internal */
    case WINGO_ERR_INTERNAL:        return "WINGO_ERR_INTERNAL";
    case WINGO_ERR_INTERNAL_ASSERT: return "WINGO_ERR_INTERNAL_ASSERT";
    case WINGO_ERR_INTERNAL_STATE:  return "WINGO_ERR_INTERNAL_STATE";
    case WINGO_ERR_INTERNAL_THREAD: return "WINGO_ERR_INTERNAL_THREAD";
    case WINGO_ERR_INTERNAL_MUTEX:  return "WINGO_ERR_INTERNAL_MUTEX";
    case WINGO_ERR_INTERNAL_COND:   return "WINGO_ERR_INTERNAL_COND";
    case WINGO_ERR_INTERNAL_ATOMIC: return "WINGO_ERR_INTERNAL_ATOMIC";
    case WINGO_ERR_INTERNAL_PANIC:  return "WINGO_ERR_INTERNAL_PANIC";
    case WINGO_ERR_INTERNAL_BUG:    return "WINGO_ERR_INTERNAL_BUG";

    default:
        return "UNKNOWN_ERROR";
    }
}

/* ============================================================================
 * ERROR CONTEXT
 * ============================================================================ */

void wingo_error_set(wingo_error_context_t *ctx,
                     wingo_error_t code,
                     const char *file,
                     int line,
                     const char *func,
                     const char *fmt, ...)
{
    va_list args;

    if (ctx == NULL) {
        return;
    }

    /* Clear context */
    memset(ctx, 0, sizeof(*ctx));

    /* Set code */
    ctx->code = code;

    /* Set location */
    if (file != NULL) {
        /*
         * Use basename — strip directory prefix.
         * We want "buffer.c" not "/home/user/project/src/buffer.c"
         */
        const char *slash = strrchr(file, '/');
        const char *name = slash != NULL ? slash + 1 : file;
        strncpy(ctx->file, name, sizeof(ctx->file) - 1);
    }

    if (func != NULL) {
        strncpy(ctx->func, func, sizeof(ctx->func) - 1);
    }

    ctx->line = line;

    /* Set timestamp */
    ctx->timestamp = wingo_time_unix();

    /* Format message */
    if (fmt != NULL) {
        va_start(args, fmt);
        vsnprintf(ctx->message, sizeof(ctx->message), fmt, args);
        va_end(args);
    } else {
        strncpy(ctx->message, wingo_error_str(code), sizeof(ctx->message) - 1);
    }
}

void wingo_error_clear(wingo_error_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
}

void wingo_error_print(const wingo_error_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    fprintf(stderr,
            "[ERROR] %s: %s\n"
            "        at %s:%d in %s()\n",
            wingo_error_name(ctx->code),
            ctx->message,
            ctx->file,
            ctx->line,
            ctx->func);
}

int wingo_error_format(const wingo_error_context_t *ctx,
                       char *buf, wingo_size size)
{
    int n;

    if (ctx == NULL || buf == NULL || size == 0) {
        return -1;
    }

    n = snprintf(buf, size,
                 "%s: %s (at %s:%d in %s)",
                 wingo_error_name(ctx->code),
                 ctx->message,
                 ctx->file,
                 ctx->line,
                 ctx->func);

    return n;
}
