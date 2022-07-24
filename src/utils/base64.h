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
#include <string>

KUMA_NS_BEGIN

typedef struct __X64_CTX    x64_ctx_t;

size_t x64_calc_encode_buf_size(size_t len);
size_t x64_calc_decode_buf_size(size_t len);
size_t x64_encode(const void* src, size_t src_len, void* dst, size_t dst_len, bool url_safe);
size_t x64_decode(const void* src, size_t src_len, void* dst, size_t dst_len);
std::string x64_encode(const void* src, size_t src_len, bool url_safe);
std::string x64_decode(const void* src, size_t src_len);
std::string x64_encode(const std::string &src, bool url_safe);
std::string x64_decode(const std::string &src);

x64_ctx_t* x64_ctx_create();
void x64_ctx_destroy(x64_ctx_t *ctx);
void x64_ctx_reset(x64_ctx_t *ctx);
size_t x64_ctx_encode(x64_ctx_t* ctx, const void* src, size_t src_len, void* dst, size_t dst_len, bool url_safe, bool finish);
size_t x64_ctx_decode(x64_ctx_t* ctx, const void* src, size_t src_len, void* dst, size_t dst_len, bool finish);

KUMA_NS_END

#endif
