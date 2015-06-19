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

uint32_t x64_calc_encode_buf_size(uint32_t length)
{
    return ((length / 3) + 1) <<2;
}

uint32_t x64_calc_decode_buf_size(uint32_t length)
{
    return (length >> 2) * 3;
}

uint32_t x64_encode(const uint8_t* data, uint32_t data_len,
                        uint8_t* buf, uint32_t buf_len, bool url_safe)
{
    const uint8_t* src = data;
    uint8_t* dst = buf;
    const uint8_t* encode_dict = (uint8_t*)s_base64_encode_map;
    if(url_safe) encode_dict = (uint8_t*)s_url_safe_base64_encode_map;
    for(uint32_t i = 0; i < data_len / 3; i++)
    {
        dst[0] = encode_dict[(src[0] & 0xFC) >> 2];
        dst[1] = encode_dict[((src[0] & 0x03) << 4) | ((src[1] & 0xF0) >> 4)];
        dst[2] = encode_dict[((src[1] & 0x0F) << 2) | ((src[2] & 0xC0) >> 6)];
        dst[3] = encode_dict[src[2] & 0x3F];
        
        src += 3;
        dst += 4;
    }
    
    switch(data_len % 3)
    {
        case 0 :
            break;
            
        case 1 :
            dst[0] = encode_dict[(src[0] & 0xFC) >> 2];
            dst[1] = encode_dict[(src[0] & 0x03) << 4];
            dst[2] = '=';
            dst[3] = '=';
            
            dst += 4;
            break;
            
        case 2 :
            dst[0] = encode_dict[(src[0] & 0xFC) >> 2];
            dst[1] = encode_dict[((src[0] & 0x03) << 4) | ((src[1] & 0xF0) >> 4)];
            dst[2] = encode_dict[(src[1] & 0x0F) << 2];
            dst[3] = '=';
            
            dst += 4;
            break;
    }
    
    return uint32_t(dst - buf);
}

uint32_t x64_decode(const uint8_t* buf, uint32_t buf_len,
                        uint8_t* data, uint32_t data_len)
{
    if(buf_len < 4) return 0;
    x64_init_decode_table();
    
    const uint8_t* src = buf;
    uint8_t* dst = data;
    
    for(uint32_t i = 0; i < buf_len / 4 - 1; i++)
    {
        uint8_t bA, bB, bC, bD;
        bA = s_base64_decode_map[src[0]];
        bB = s_base64_decode_map[src[1]];
        bC = s_base64_decode_map[src[2]];
        bD = s_base64_decode_map[src[3]];
        
        dst[0] = (bA << 2) | (bB >> 4);
        dst[1] = ((bB & 0x0F) << 4) | (bC >> 2);
        dst[2] = ((bC & 0x03) << 6) | bD;
        
        src += 4;
        dst += 3;
    }
    
    {// last 4 bytes
        uint8_t bA, bB, bC, bD;
        bA = s_base64_decode_map[src[0]];
        bB = s_base64_decode_map[src[1]];
        bC = s_base64_decode_map[src[2]];
        bD = s_base64_decode_map[src[3]];
        dst[0] = (bA << 2) | (bB >> 4);
        dst[1] = ((bB & 0x0F) << 4) | (bC >> 2);
        dst[2] = ((bC & 0x03) << 6) | bD;
        
        int pos = 3;
        if('=' == src[3])
        {
            dst[2] = 0;
            pos = 2;
        }
        if('=' == src[2])
        {
            dst[1] = 0;
            pos = 1;
        }
        dst += pos;
    }
    
    return uint32_t(dst - data);
}

void x64_init_ctx(X64_CTX* ctx)
{
    memset(ctx, 0, sizeof(X64_CTX));
}

uint32_t x64_encode_ctx(X64_CTX* ctx, const uint8_t* data, uint32_t data_len,
                            uint8_t* buf, uint32_t buf_len, bool url_safe, bool final)
{
    if(0 == data_len && !final) return 0;
    const uint8_t* src = data;
    uint8_t* dst = buf;
    const uint8_t* encode_dict = (uint8_t*)s_base64_encode_map;
    if(url_safe) encode_dict = (uint8_t*)s_url_safe_base64_encode_map;
    
    if(ctx->l > 0)
    {
        if(ctx->l + data_len >= 3)
        {
            if(1 == ctx->l)
            {
                dst[0] = encode_dict[(ctx->b[0] & 0xFC) >> 2];
                dst[1] = encode_dict[((ctx->b[0] & 0x03) << 4) | ((src[0] & 0xF0) >> 4)];
                dst[2] = encode_dict[((src[0] & 0x0F) << 2) | ((src[1] & 0xC0) >> 6)];
                dst[3] = encode_dict[src[1] & 0x3F];
                src += 2;
                data_len -= 2;
                dst += 4;
                ctx->l = 0;
            }
            else if(2 == ctx->l)
            {
                dst[0] = encode_dict[(ctx->b[0] & 0xFC) >> 2];
                dst[1] = encode_dict[((ctx->b[0] & 0x03) << 4) | ((ctx->b[1] & 0xF0) >> 4)];
                dst[2] = encode_dict[((ctx->b[1] & 0x0F) << 2) | ((src[0] & 0xC0) >> 6)];
                dst[3] = encode_dict[src[0] & 0x3F];
                src += 1;
                data_len -= 1;
                dst += 4;
                ctx->l = 0;
            }
        }
        else
        {
            if(1 == ctx->l && 1 == data_len)
            {
                ctx->b[1] = src[0];
                ctx->l += 1;
            }
            if(final)
            {
                src = ctx->b;
                data_len = ctx->l;
            }
            else
                return 0;
        }
    }
    
    for(uint32_t i = 0; i < data_len / 3; i++)
    {
        dst[0] = encode_dict[(src[0] & 0xFC) >> 2];
        dst[1] = encode_dict[((src[0] & 0x03) << 4) | ((src[1] & 0xF0) >> 4)];
        dst[2] = encode_dict[((src[1] & 0x0F) << 2) | ((src[2] & 0xC0) >> 6)];
        dst[3] = encode_dict[src[2] & 0x3F];
        
        src += 3;
        dst += 4;
    }
    
    switch(data_len % 3)
    {
        case 0 :
            break;
            
        case 1 :
            if(final)
            {
                dst[0] = encode_dict[(src[0] & 0xFC) >> 2];
                dst[1] = encode_dict[(src[0] & 0x03) << 4];
                dst[2] = '=';
                dst[3] = '=';
                
                dst += 4;
            }
            else
            {
                ctx->b[0] = src[0];
                ctx->l = 1;
            }
            break;
            
        case 2 :
            if(final)
            {
                dst[0] = encode_dict[(src[0] & 0xFC) >> 2];
                dst[1] = encode_dict[((src[0] & 0x03) << 4) | ((src[1] & 0xF0) >> 4)];
                dst[2] = encode_dict[(src[1] & 0x0F) << 2];
                dst[3] = '=';
                
                dst += 4;
            }
            else
            {
                ctx->b[0] = src[0];
                ctx->b[1] = src[1];
                ctx->l = 2;
            }
            break;
    }
    
    return uint32_t(dst - buf);
}

uint32_t x64_decode_ctx(X64_CTX* ctx, const uint8_t* buf, uint32_t buf_len,
                            uint8_t* data, uint32_t data_len, bool final)
{
    x64_init_decode_table();
    
    uint32_t cur_len = buf_len;
    const uint8_t* src = buf;
    uint8_t* dst = data;
    
    if(ctx->l > 0)
    {
        if(ctx->l + buf_len >= 4)
        {
            uint8_t bA, bB, bC, bD;
            if(1 == ctx->l)
            {
                bA = s_base64_decode_map[ctx->b[0]];
                bB = s_base64_decode_map[src[0]];
                bC = s_base64_decode_map[src[1]];
                bD = s_base64_decode_map[src[2]];
            }
            else if(2 == ctx->l)
            {
                bA = s_base64_decode_map[ctx->b[0]];
                bB = s_base64_decode_map[ctx->b[1]];
                bC = s_base64_decode_map[src[0]];
                bD = s_base64_decode_map[src[1]];
            }
            else //if(3 == ctx->l)
            {
                bA = s_base64_decode_map[ctx->b[0]];
                bB = s_base64_decode_map[ctx->b[1]];
                bC = s_base64_decode_map[ctx->b[2]];
                bD = s_base64_decode_map[src[0]];
            }
            src += 4 - ctx->l;
            cur_len -= 4 - ctx->l;
            dst[0] = (bA << 2) | (bB >> 4);
            dst[1] = ((bB & 0x0F) << 4) | (bC >> 2);
            dst[2] = ((bC & 0x03) << 6) | bD;
            dst += 3;
            ctx->l = 0;
        }
        else
        {
            if(1 == ctx->l)
            {
                if(1 == cur_len)
                {
                    ctx->b[1] = src[0];
                    ctx->l += 1;
                }
                else if(2 == cur_len)
                {
                    ctx->b[1] = src[0];
                    ctx->b[2] = src[1];
                    ctx->l += 2;
                }
            }
            else if(2 == ctx->l && 1 == cur_len)
            {
                ctx->b[1] = src[0];
                ctx->l += 1;
            }
            if(final)
            {// ??? data not enough ???
                src = ctx->b;
                data_len = ctx->l;
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
            bA = s_base64_decode_map[src[0]];
            bB = s_base64_decode_map[src[1]];
            bC = s_base64_decode_map[src[2]];
            bD = s_base64_decode_map[src[3]];
            
            dst[0] = (bA << 2) | (bB >> 4);
            dst[1] = ((bB & 0x0F) << 4) | (bC >> 2);
            dst[2] = ((bC & 0x03) << 6) | bD;
            
            src += 4;
            dst += 3;
        }
        
        {// last 4 bytes
            uint8_t bA, bB, bC, bD;
            bA = s_base64_decode_map[src[0]];
            bB = s_base64_decode_map[src[1]];
            bC = s_base64_decode_map[src[2]];
            bD = s_base64_decode_map[src[3]];
            dst[0] = (bA << 2) | (bB >> 4);
            dst[1] = ((bB & 0x0F) << 4) | (bC >> 2);
            dst[2] = ((bC & 0x03) << 6) | bD;
            
            int pos = 3;
            if('=' == src[3])
            {
                dst[2] = 0;
                pos = 2;
            }
            if('=' == src[2])
            {
                dst[1] = 0;
                pos = 1;
            }
            src += 4;
            dst += pos;
        }
    }
    if(!final)
    {
        uint32_t remain_len = uint32_t(buf_len - (src - buf));
        remain_len %= 4;
        for (uint32_t i=0; i<remain_len; ++i)
        {
            ctx->b[i] = src[i];
        }
        ctx->l = remain_len;
    }
    
    return uint32_t(dst - data);
}

KUMA_NS_END
