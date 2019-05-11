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

#include "base64.h"
#include <string.h>

KUMA_NS_BEGIN
/////////////////////////////////////////////////////////////////////////////
// Base64 encode/decode
//
static char s_base64_encode_map[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint8_t s_base64_decode_map[256] = { 0 };
static bool s_base64_decode_map_initilized = false;

static char s_url_safe_base64_encode_map[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void x64_init_decode_table()
{
    if(s_base64_decode_map_initilized)
        return ;
    s_base64_decode_map_initilized = true;
    memset(s_base64_decode_map, 0x40, sizeof(s_base64_decode_map));
    
    uint8_t code = 0;
    for (int i=0; i<(int)strlen(s_base64_encode_map); ++i)
    {
        s_base64_decode_map[(uint8_t)s_base64_encode_map[i]] = code++;
    }
    
    s_base64_decode_map[(int)'-'] = s_base64_decode_map[(int)'+'];
    s_base64_decode_map[(int)'_'] = s_base64_decode_map[(int)'/'];
}

size_t x64_calc_encode_buf_size(size_t length)
{
    return ((length / 3) + 1) <<2;
}

size_t x64_calc_decode_buf_size(size_t length)
{
    return (length >> 2) * 3;
}

size_t x64_encode(const void* src, size_t src_len, void* dst, size_t dst_len, bool url_safe)
{
    auto const *s = static_cast<const uint8_t*>(src);
    auto *d = static_cast<uint8_t*>(dst);
    auto const *encode_dict = s_base64_encode_map;
    if(url_safe) encode_dict = s_url_safe_base64_encode_map;
    for(size_t i = 0; i < src_len / 3; i++)
    {
        d[0] = encode_dict[(s[0] & 0xFC) >> 2];
        d[1] = encode_dict[((s[0] & 0x03) << 4) | ((s[1] & 0xF0) >> 4)];
        d[2] = encode_dict[((s[1] & 0x0F) << 2) | ((s[2] & 0xC0) >> 6)];
        d[3] = encode_dict[s[2] & 0x3F];
        
        s += 3;
        d += 4;
    }
    
    switch(src_len % 3)
    {
        case 0 :
            break;
            
        case 1 :
            d[0] = encode_dict[(s[0] & 0xFC) >> 2];
            d[1] = encode_dict[(s[0] & 0x03) << 4];
            d[2] = '=';
            d[3] = '=';
            
            d += 4;
            break;
            
        case 2 :
            d[0] = encode_dict[(s[0] & 0xFC) >> 2];
            d[1] = encode_dict[((s[0] & 0x03) << 4) | ((s[1] & 0xF0) >> 4)];
            d[2] = encode_dict[(s[1] & 0x0F) << 2];
            d[3] = '=';
            
            d += 4;
            break;
    }
    
    return size_t(d - static_cast<uint8_t*>(dst));
}

size_t x64_decode(const void* src, size_t src_len, void* dst, size_t dst_len)
{
    if(src_len < 4) return 0;
    x64_init_decode_table();
    
    auto const *s = static_cast<const uint8_t*>(src);
    auto *d = static_cast<uint8_t*>(dst);
    
    for(size_t i = 0; i < src_len / 4 - 1; i++)
    {
        uint8_t bA, bB, bC, bD;
        bA = s_base64_decode_map[s[0]];
        bB = s_base64_decode_map[s[1]];
        bC = s_base64_decode_map[s[2]];
        bD = s_base64_decode_map[s[3]];
        
        d[0] = (bA << 2) | (bB >> 4);
        d[1] = ((bB & 0x0F) << 4) | (bC >> 2);
        d[2] = ((bC & 0x03) << 6) | bD;
        
        s += 4;
        d += 3;
    }
    
    {// last 4 bytes
        uint8_t bA, bB, bC, bD;
        bA = s_base64_decode_map[s[0]];
        bB = s_base64_decode_map[s[1]];
        bC = s_base64_decode_map[s[2]];
        bD = s_base64_decode_map[s[3]];
        d[0] = (bA << 2) | (bB >> 4);
        d[1] = ((bB & 0x0F) << 4) | (bC >> 2);
        d[2] = ((bC & 0x03) << 6) | bD;
        
        int pos = 3;
        if('=' == s[3])
        {
            d[2] = 0;
            pos = 2;
        }
        if('=' == s[2])
        {
            d[1] = 0;
            pos = 1;
        }
        d += pos;
    }
    
    return size_t(d - static_cast<uint8_t*>(dst));
}

std::string x64_encode(const void* src, size_t src_len, bool url_safe)
{
    auto const *s = static_cast<const uint8_t*>(src);
    std::string dst;
    auto const *encode_dict = s_base64_encode_map;
    if(url_safe) encode_dict = s_url_safe_base64_encode_map;
    for(size_t i = 0; i < src_len / 3; i++)
    {
        dst.push_back(encode_dict[(s[0] & 0xFC) >> 2]);
        dst.push_back(encode_dict[((s[0] & 0x03) << 4) | ((s[1] & 0xF0) >> 4)]);
        dst.push_back(encode_dict[((s[1] & 0x0F) << 2) | ((s[2] & 0xC0) >> 6)]);
        dst.push_back(encode_dict[s[2] & 0x3F]);
        
        s += 3;
    }
    
    switch(src_len % 3)
    {
        case 0 :
            break;
            
        case 1 :
            dst.push_back(encode_dict[(s[0] & 0xFC) >> 2]);
            dst.push_back(encode_dict[(s[0] & 0x03) << 4]);
            dst.push_back('=');
            dst.push_back('=');
            break;
            
        case 2 :
            dst.push_back(encode_dict[(s[0] & 0xFC) >> 2]);
            dst.push_back(encode_dict[((s[0] & 0x03) << 4) | ((s[1] & 0xF0) >> 4)]);
            dst.push_back(encode_dict[(s[1] & 0x0F) << 2]);
            dst.push_back('=');
            break;
    }
    
    return dst;
}

std::string x64_decode(const void* src, size_t src_len)
{
    x64_init_decode_table();
    
    auto const *s = static_cast<const uint8_t*>(src);
    std::string dst;
    
    for(size_t i = 0; i < src_len / 4 - 1; i++)
    {
        uint8_t bA, bB, bC, bD;
        bA = s_base64_decode_map[s[0]];
        bB = s_base64_decode_map[s[1]];
        bC = s_base64_decode_map[s[2]];
        bD = s_base64_decode_map[s[3]];
        
        dst.push_back((bA << 2) | (bB >> 4));
        dst.push_back(((bB & 0x0F) << 4) | (bC >> 2));
        dst.push_back(((bC & 0x03) << 6) | bD);
        
        s += 4;
    }
    
    {// last 4 bytes
        uint8_t bA, bB, bC, bD;
        bA = s_base64_decode_map[s[0]];
        bB = s_base64_decode_map[s[1]];
        bC = s_base64_decode_map[s[2]];
        bD = s_base64_decode_map[s[3]];
        dst.push_back((bA << 2) | (bB >> 4));
        if (s[2] != '=')
        {
            dst.push_back(((bB & 0x0F) << 4) | (bC >> 2));
            if (s[3] != '=')
            {
                dst.push_back(((bC & 0x03) << 6) | bD);
            }
        }
    }
    
    return dst;
}

std::string x64_encode(const std::string &src, bool url_safe)
{
    return x64_encode(src.data(), src.size(), url_safe);
}

std::string x64_decode(const std::string &src)
{
    return x64_decode(src.data(), src.size());
}

struct __X64_CTX {
    uint8_t l;
    uint8_t b[3];
};

x64_ctx_t* x64_ctx_create()
{
    auto *ctx = new x64_ctx_t();
    memset(ctx, 0, sizeof(x64_ctx_t));
    
    return ctx;
}

void x64_ctx_destroy(x64_ctx_t *ctx)
{
    if (ctx) delete ctx;
}

void x64_ctx_reset(x64_ctx_t *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(x64_ctx_t));
}

size_t x64_ctx_encode(x64_ctx_t* ctx, const void* src, size_t src_len, void* dst, size_t dst_len, bool url_safe, bool finish)
{
    if(0 == src_len && !finish) return 0;
    auto const * s = static_cast<const uint8_t*>(src);
    auto *d = static_cast<uint8_t*>(dst);
    auto const *encode_dict = s_base64_encode_map;
    if(url_safe) encode_dict = s_url_safe_base64_encode_map;
    
    if(ctx->l > 0)
    {
        if(ctx->l + src_len >= 3)
        {
            if(1 == ctx->l)
            {
                d[0] = encode_dict[(ctx->b[0] & 0xFC) >> 2];
                d[1] = encode_dict[((ctx->b[0] & 0x03) << 4) | ((s[0] & 0xF0) >> 4)];
                d[2] = encode_dict[((s[0] & 0x0F) << 2) | ((s[1] & 0xC0) >> 6)];
                d[3] = encode_dict[s[1] & 0x3F];
                s += 2;
                src_len -= 2;
                d += 4;
                ctx->l = 0;
            }
            else if(2 == ctx->l)
            {
                d[0] = encode_dict[(ctx->b[0] & 0xFC) >> 2];
                d[1] = encode_dict[((ctx->b[0] & 0x03) << 4) | ((ctx->b[1] & 0xF0) >> 4)];
                d[2] = encode_dict[((ctx->b[1] & 0x0F) << 2) | ((s[0] & 0xC0) >> 6)];
                d[3] = encode_dict[s[0] & 0x3F];
                s += 1;
                src_len -= 1;
                d += 4;
                ctx->l = 0;
            }
        }
        else
        {
            if(1 == ctx->l && 1 == src_len)
            {
                ctx->b[1] = s[0];
                ctx->l += 1;
            }
            if(finish)
            {
                s = ctx->b;
                src_len = ctx->l;
            }
            else
                return 0;
        }
    }
    
    for(size_t i = 0; i < src_len / 3; i++)
    {
        d[0] = encode_dict[(s[0] & 0xFC) >> 2];
        d[1] = encode_dict[((s[0] & 0x03) << 4) | ((s[1] & 0xF0) >> 4)];
        d[2] = encode_dict[((s[1] & 0x0F) << 2) | ((s[2] & 0xC0) >> 6)];
        d[3] = encode_dict[s[2] & 0x3F];
        
        s += 3;
        d += 4;
    }
    
    switch(src_len % 3)
    {
        case 0 :
            break;
            
        case 1 :
            if(finish)
            {
                d[0] = encode_dict[(s[0] & 0xFC) >> 2];
                d[1] = encode_dict[(s[0] & 0x03) << 4];
                d[2] = '=';
                d[3] = '=';
                
                d += 4;
            }
            else
            {
                ctx->b[0] = s[0];
                ctx->l = 1;
            }
            break;
            
        case 2 :
            if(finish)
            {
                d[0] = encode_dict[(s[0] & 0xFC) >> 2];
                d[1] = encode_dict[((s[0] & 0x03) << 4) | ((s[1] & 0xF0) >> 4)];
                d[2] = encode_dict[(s[1] & 0x0F) << 2];
                d[3] = '=';
                
                d += 4;
            }
            else
            {
                ctx->b[0] = s[0];
                ctx->b[1] = s[1];
                ctx->l = 2;
            }
            break;
    }
    
    return size_t(d - static_cast<uint8_t*>(dst));
}

size_t x64_ctx_decode(x64_ctx_t* ctx, const void* src, size_t src_len, void* dst, size_t dst_len, bool finish)
{
    x64_init_decode_table();
    
    size_t cur_len = src_len;
    auto const *s = static_cast<const uint8_t*>(src);
    auto *d = static_cast<uint8_t*>(dst);
    
    if(ctx->l > 0)
    {
        if(ctx->l + src_len >= 4)
        {
            uint8_t bA, bB, bC, bD, bL2, bL3;
            if(1 == ctx->l)
            {
                bA = s_base64_decode_map[ctx->b[0]];
                bB = s_base64_decode_map[s[0]];
                bC = s_base64_decode_map[s[1]];
                bD = s_base64_decode_map[s[2]];
                bL2 = s[1];
                bL3 = s[2];
            }
            else if(2 == ctx->l)
            {
                bA = s_base64_decode_map[ctx->b[0]];
                bB = s_base64_decode_map[ctx->b[1]];
                bC = s_base64_decode_map[s[0]];
                bD = s_base64_decode_map[s[1]];
                bL2 = s[0];
                bL3 = s[1];
            }
            else //if(3 == ctx->l)
            {
                bA = s_base64_decode_map[ctx->b[0]];
                bB = s_base64_decode_map[ctx->b[1]];
                bC = s_base64_decode_map[ctx->b[2]];
                bD = s_base64_decode_map[s[0]];
                bL2 = ctx->b[2];
                bL3 = s[0];
            }
            s += 4 - ctx->l;
            cur_len -= 4 - ctx->l;
            
            d[0] = (bA << 2) | (bB >> 4);
            d[1] = ((bB & 0x0F) << 4) | (bC >> 2);
            d[2] = ((bC & 0x03) << 6) | bD;
            
            int pos = 3;
            if('=' == bL3)
            {
                d[2] = 0;
                pos = 2;
            }
            if('=' == bL2)
            {
                d[1] = 0;
                pos = 1;
            }
            d += pos;
            
            ctx->l = 0;
        }
        else
        {
            if(1 == ctx->l)
            {
                if(1 == cur_len)
                {
                    ctx->b[1] = s[0];
                    ctx->l += 1;
                }
                else if(2 == cur_len)
                {
                    ctx->b[1] = s[0];
                    ctx->b[2] = s[1];
                    ctx->l += 2;
                }
            }
            else if(2 == ctx->l && 1 == cur_len)
            {
                ctx->b[1] = s[0];
                ctx->l += 1;
            }
            if(finish)
            {// ??? data not enough ???
                s = ctx->b;
                dst_len = ctx->l;
                return 0;
            }
            else
                return 0;
        }
    }
    if(cur_len >= 4)
    {
        for(int i = 0; i < (int)cur_len / 4 - 1; i++)
        {
            uint8_t bA, bB, bC, bD;
            bA = s_base64_decode_map[s[0]];
            bB = s_base64_decode_map[s[1]];
            bC = s_base64_decode_map[s[2]];
            bD = s_base64_decode_map[s[3]];
            
            d[0] = (bA << 2) | (bB >> 4);
            d[1] = ((bB & 0x0F) << 4) | (bC >> 2);
            d[2] = ((bC & 0x03) << 6) | bD;
            
            s += 4;
            d += 3;
        }
        
        {// last 4 bytes
            uint8_t bA, bB, bC, bD;
            bA = s_base64_decode_map[s[0]];
            bB = s_base64_decode_map[s[1]];
            bC = s_base64_decode_map[s[2]];
            bD = s_base64_decode_map[s[3]];
            d[0] = (bA << 2) | (bB >> 4);
            d[1] = ((bB & 0x0F) << 4) | (bC >> 2);
            d[2] = ((bC & 0x03) << 6) | bD;
            
            int pos = 3;
            if('=' == s[3])
            {
                d[2] = 0;
                pos = 2;
            }
            if('=' == s[2])
            {
                d[1] = 0;
                pos = 1;
            }
            s += 4;
            d += pos;
        }
    }
    if(!finish)
    {
        size_t remain_len = size_t(src_len - (s - static_cast<const uint8_t*>(src)));
        remain_len %= 4;
        for (size_t i=0; i<remain_len; ++i)
        {
            ctx->b[i] = s[i];
        }
        ctx->l = static_cast<uint8_t>(remain_len);
    }
    
    return size_t(d - static_cast<uint8_t*>(dst));
}

KUMA_NS_END
