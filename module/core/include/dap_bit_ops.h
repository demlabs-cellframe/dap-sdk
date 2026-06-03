/**
 * @file dap_bit_ops.h
 * @brief Cross-platform byte order (endian) conversion macros
 * 
 * Provides htobe16/32/64, htole16/32/64, be16/32/64toh, le16/32/64toh
 * macros for portable endian conversion across Linux, macOS, BSD, Windows.
 *
 * Original: Public Domain by Mathias Panzenböck
 * Adapted for DAP SDK
 */

#pragma once

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)

# define __WINDOWS__

#endif

#if defined(__linux__) || defined(__CYGWIN__) || defined(__unix__) || defined(__EMSCRIPTEN__)

# include <endian.h>

#elif defined(__APPLE__)

# include <libkern/OSByteOrder.h>

# define htobe16(x) OSSwapHostToBigInt16(x)
# define htole16(x) OSSwapHostToLittleInt16(x)
# define be16toh(x) OSSwapBigToHostInt16(x)
# define le16toh(x) OSSwapLittleToHostInt16(x)
 
# define htobe32(x) OSSwapHostToBigInt32(x)
# define htole32(x) OSSwapHostToLittleInt32(x)
# define be32toh(x) OSSwapBigToHostInt32(x)
# define le32toh(x) OSSwapLittleToHostInt32(x)
 
# define htobe64(x) OSSwapHostToBigInt64(x)
# define htole64(x) OSSwapHostToLittleInt64(x)
# define be64toh(x) OSSwapBigToHostInt64(x)
# define le64toh(x) OSSwapLittleToHostInt64(x)

# define __BYTE_ORDER    BYTE_ORDER
# define __BIG_ENDIAN    BIG_ENDIAN
# define __LITTLE_ENDIAN LITTLE_ENDIAN
# define __PDP_ENDIAN    PDP_ENDIAN

#elif defined(__OpenBSD__)

# include <sys/endian.h>

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)

# include <sys/endian.h>

#if !defined(be16toh)
# define be16toh(x) betoh16(x)
#endif

#if !defined(le16toh)
# define le16toh(x) letoh16(x)
#endif

#if !defined(be32toh)
# define be32toh(x) betoh32(x)
#endif

#if !defined(le32toh)
# define le32toh(x) letoh32(x)
#endif

#if !defined(be64toh)
# define be64toh(x) betoh64(x)
#endif

#if !defined(le64toh)
# define le64toh(x) letoh64(x)
#endif

#elif defined(__WINDOWS__)

# include <windows.h>

# if BYTE_ORDER == LITTLE_ENDIAN

#               if defined(_MSC_VER)
#                       include <stdlib.h>
#     define htobe16(x) _byteswap_ushort(x)
#     define htole16(x) (x)
#     define be16toh(x) _byteswap_ushort(x)
#     define le16toh(x) (x)
                  
#     define htobe32(x) _byteswap_ulong(x)
#     define htole32(x) (x)
#     define be32toh(x) _byteswap_ulong(x)
#     define le32toh(x) (x)
                  
#     define htobe64(x) _byteswap_uint64(x)
#     define htole64(x) (x)
#     define be64toh(x) _byteswap_uint64(x)
#     define le64toh(x) (x)

#               elif defined(__GNUC__) || defined(__clang__)

#     define htobe16(x) __builtin_bswap16(x)
#     define htole16(x) (x)
#     define be16toh(x) __builtin_bswap16(x)
#     define le16toh(x) (x)
                  
#     define htobe32(x) __builtin_bswap32(x)
#     define htole32(x) (x)
#     define be32toh(x) __builtin_bswap32(x)
#     define le32toh(x) (x)
                  
#     define htobe64(x) __builtin_bswap64(x)
#     define htole64(x) (x)
#     define be64toh(x) __builtin_bswap64(x)
#     define le64toh(x) (x)
#               else
#                       error platform not supported
#               endif

# else

#   error byte order not supported

# endif

# define __BYTE_ORDER    BYTE_ORDER
# define __BIG_ENDIAN    BIG_ENDIAN
# define __LITTLE_ENDIAN LITTLE_ENDIAN
# define __PDP_ENDIAN    PDP_ENDIAN

#else

# error platform not supported

#endif
