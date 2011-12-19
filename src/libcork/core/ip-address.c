/* -*- coding: utf-8 -*-
 * ----------------------------------------------------------------------
 * Copyright © 2011, RedJack, LLC.
 * All rights reserved.
 *
 * Please see the LICENSE.txt file in this distribution for license
 * details.
 * ----------------------------------------------------------------------
 */

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "libcork/core/error.h"
#include "libcork/core/net-addresses.h"
#include "libcork/core/types.h"
#include "libcork/ds/buffer.h"


/*-----------------------------------------------------------------------
 * Error handling
 */

#define DEFINE_ERRORS(ip_version) \
static int \
cork_ip##ip_version##_unknown_error \
    (struct cork_alloc *alloc, struct cork_error *err, \
     struct cork_buffer *dest) \
{ \
    return cork_buffer_set_string \
        (alloc, dest, \
         "Unknown error while parsing IP" #ip_version " address", \
         NULL); \
} \
\
int \
cork_ip##ip_version##_unknown_error_set \
    (struct cork_alloc *alloc, struct cork_error *err) \
{ \
    return cork_error_set(alloc, err, \
                          CORK_NET_ADDRESS_ERROR, \
                          CORK_NET_ADDRESS_UNKNOWN_ERROR, \
                          cork_ip##ip_version##_unknown_error); \
} \
\
static int \
cork_ip##ip_version##_parse_error(struct cork_alloc *alloc, struct cork_error *err, \
                      struct cork_buffer *dest) \
{ \
    const char  **str = cork_error_extra(err); \
    return cork_buffer_printf \
        (alloc, dest, NULL, \
         "Invalid IP" #ip_version " address: %s", *str); \
} \
\
int \
cork_ip##ip_version##_parse_error_set \
    (struct cork_alloc *alloc, struct cork_error *err, \
     const char *invalid_str) \
{ \
    return cork_error_set_extra(alloc, err, \
                                CORK_NET_ADDRESS_ERROR, \
                                CORK_NET_ADDRESS_UNKNOWN_ERROR, \
                                cork_ip##ip_version##_parse_error, \
                                invalid_str); \
}

DEFINE_ERRORS(v4)
DEFINE_ERRORS(v6)
DEFINE_ERRORS()

#undef DEFINE_ERRORS


/*-----------------------------------------------------------------------
 * IP addresses
 */

/*** IPv4 ***/

bool
cork_ipv4_copy(struct cork_ipv4 *addr, const void *src)
{
    memcpy(addr, src, sizeof(struct cork_ipv4));
    return true;
}

bool
cork_ipv4_init(struct cork_alloc *alloc, struct cork_ipv4 *addr,
               const char *str, struct cork_error *error)
{
    int  rc = inet_pton(AF_INET, str, addr);

    if (rc == 1) {
        /* successful parse */
        return true;
    } else if (rc == 0) {
        /* parse error */
        cork_ipv4_parse_error_set(alloc, error, str);
        return false;
    } else {
        cork_ipv4_unknown_error_set(alloc, error);
        return false;
    }
}

bool
cork_ipv4_equal(const struct cork_ipv4 *addr1, const struct cork_ipv4 *addr2)
{
    return (memcmp(addr1, addr2, sizeof(struct cork_ipv4)) == 0);
}

void
cork_ipv4_to_raw_string(const struct cork_ipv4 *addr, char *dest)
{
    snprintf(dest, CORK_IPV4_STRING_LENGTH, "%u.%u.%u.%u",
             addr->u8[0], addr->u8[1], addr->u8[2], addr->u8[3]);
}

/*** IPv6 ***/

bool
cork_ipv6_copy(struct cork_ipv6 *addr, const void *src)
{
    memcpy(addr, src, sizeof(struct cork_ipv6));
    return true;
}

bool
cork_ipv6_init(struct cork_alloc *alloc, struct cork_ipv6 *addr,
               const char *str, struct cork_error *error)
{
    int  rc = inet_pton(AF_INET6, str, addr);

    if (rc == 1) {
        /* successful parse */
        return true;
    } else if (rc == 0) {
        /* parse error */
        cork_ipv6_parse_error_set(alloc, error, str);
        return false;
    } else {
        cork_ipv6_unknown_error_set(alloc, error);
        return false;
    }
}

bool
cork_ipv6_equal(const struct cork_ipv6 *addr1, const struct cork_ipv6 *addr2)
{
    return (memcmp(addr1, addr2, sizeof(struct cork_ipv6)) == 0);
}

#define NS_IN6ADDRSZ 16
#define NS_INT16SZ 2

void
cork_ipv6_to_raw_string(const struct cork_ipv6 *addr, char *dest)
{
    const uint8_t  *src = addr->u8;

    /*
     * Note that int32_t and int16_t need only be "at least" large enough
     * to contain a value of the specified size.  On some systems, like
     * Crays, there is no such thing as an integer variable with 16 bits.
     * Keep this in mind if you think this function should have been coded
     * to use pointer overlays.  All the world's not a VAX.
     */
    char *tp;
    struct { int base, len; } best, cur;
    unsigned int words[NS_IN6ADDRSZ / NS_INT16SZ];
    int i;

    /*
     * Preprocess:
     *      Copy the input (bytewise) array into a wordwise array.
     *      Find the longest run of 0x00's in src[] for :: shorthanding.
     */
    memset(words, '\0', sizeof words);
    for (i = 0; i < NS_IN6ADDRSZ; i++)
        words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
    best.base = -1;
    best.len = 0;
    cur.base = -1;
    cur.len = 0;
    for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
        if (words[i] == 0) {
            if (cur.base == -1)
                cur.base = i, cur.len = 1;
            else
                cur.len++;
        } else {
            if (cur.base != -1) {
                if (best.base == -1 || cur.len > best.len)
                    best = cur;
                cur.base = -1;
            }
        }
    }
    if (cur.base != -1) {
        if (best.base == -1 || cur.len > best.len)
            best = cur;
    }
    if (best.base != -1 && best.len < 2)
        best.base = -1;

    /*
     * Format the result.
     */
    tp = dest;
    for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
        /* Are we inside the best run of 0x00's? */
        if (best.base != -1 && i >= best.base &&
            i < (best.base + best.len)) {
            if (i == best.base)
                *tp++ = ':';
            continue;
        }
        /* Are we following an initial run of 0x00s or any real hex? */
        if (i != 0)
            *tp++ = ':';
        /* Is this address an encapsulated IPv4? */
        if (i == 6 && best.base == 0 &&
            (best.len == 6 || (best.len == 5 && words[5] == 0xffff))) {
            tp += sprintf(tp, "%u.%u.%u.%u",
                          src[12], src[13], src[14], src[15]);
            break;
        }
        tp += sprintf(tp, "%x", words[i]);
    }
    /* Was it a trailing run of 0x00's? */
    if (best.base != -1 && (best.base + best.len) ==
        (NS_IN6ADDRSZ / NS_INT16SZ))
        *tp++ = ':';
    *tp++ = '\0';
}


/*** IP ***/

bool
cork_ip_from_ipv4(struct cork_ip *addr, const void *src)
{
    addr->version = 4;
    return cork_ipv4_copy(&addr->ip.v4, src);
}

bool
cork_ip_from_ipv6(struct cork_ip *addr, const void *src)
{
    addr->version = 6;
    return cork_ipv6_copy(&addr->ip.v6, src);
}

bool
cork_ip_init(struct cork_alloc *alloc, struct cork_ip *addr,
             const char *str, struct cork_error *error)
{
    int  rc;

    /* Try IPv4 first */

    rc = inet_pton(AF_INET, str, &addr->ip.v4);

    if (rc == 1) {
        /* successful parse */
        addr->version = 4;
        return true;
    } else if (rc != 0) {
        /* non-parse error */
        cork_ip_unknown_error_set(alloc, error);
        return false;
    }

    /* Then try IPv6 */

    rc = inet_pton(AF_INET6, str, &addr->ip.v6);

    if (rc == 1) {
        /* successful parse */
        addr->version = 6;
        return true;
    } else if (rc != 0) {
        /* non-parse error */
        cork_ip_unknown_error_set(alloc, error);
        return false;
    }

    /* Parse error for both address types */
    cork_ip_parse_error_set(alloc, error, str);
    return false;
}

bool
cork_ip_equal(const struct cork_ip *addr1,
              const struct cork_ip *addr2)
{
    if (addr1 == addr2) {
        return true;
    }

    if (!addr1 || !addr2) {
        return false;
    }

    if (addr1->version != addr2->version) {
        return false;
    }

    switch (addr1->version) {
        case 4:
            return cork_ipv4_equal(&addr1->ip.v4, &addr2->ip.v4);

        case 6:
            return cork_ipv6_equal(&addr1->ip.v6, &addr2->ip.v6);

        default:
            return false;
    }
}

void
cork_ip_to_raw_string(const struct cork_ip *addr, char *dest)
{
    switch (addr->version) {
        case 4:
            cork_ipv4_to_raw_string(&addr->ip.v4, dest);
            return;

        case 6:
            cork_ipv6_to_raw_string(&addr->ip.v6, dest);
            return;

        default:
            strncpy(dest, "<INVALID>", CORK_IP_STRING_LENGTH);
            return;
    }
}
