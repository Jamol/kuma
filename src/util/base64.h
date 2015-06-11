/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __base64_h__
#define __base64_h__

#include "kmdefs.h"
#include <stdint.h>

KUMA_NS_BEGIN

typedef struct __X64_CTX{
    uint8_t l;
    uint8_t b[3];
}X64_CTX;

uint32_t x64_calc_encode_buf_size(uint32_t len);
uint32_t x64_calc_decode_buf_size(uint32_t len);
uint32_t x64_encode(const uint8_t* data, uint32_t data_len,
                    uint8_t* buf, uint32_t buf_len, bool url_safe);
uint32_t x64_decode(const uint8_t* buf, uint32_t buf_len,
                    uint8_t* data, uint32_t data_len);

void x64_init_ctx(X64_CTX* ctx);
uint32_t x64_encode_ctx(X64_CTX* ctx, const uint8_t* data, uint32_t data_len,
                        uint8_t* buf, uint32_t buf_len, bool url_safe, bool final);
uint32_t x64_decode_ctx(X64_CTX* ctx, const uint8_t* buf, uint32_t buf_len,
                        uint8_t* data, uint32_t data_len, bool final);

KUMA_NS_END

#endif
