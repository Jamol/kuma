/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "kmconf.h"

#include <string>


#define WS_NS_BEGIN namespace kuma { namespace ws {;
#define WS_NS_END }}


WS_NS_BEGIN


#define WS_MASK_KEY_SIZE    4
#define WS_MAX_HEADER_SIZE  14

const std::string kSecWebSocketKey { "Sec-WebSocket-Key" };
const std::string kSecWebSocketAccept { "Sec-WebSocket-Accept" };
const std::string kSecWebSocketVersion { "Sec-WebSocket-Version" };
const std::string kSecWebSocketProtocol { "Sec-WebSocket-Protocol" };
const std::string kSecWebSocketExtensions { "Sec-WebSocket-Extensions" };

const std::string kWebSocketVersion { "13" };

enum class WSOpcode : uint8_t {
    CONTINUE  = 0,
    TEXT      = 1,
    BINARY    = 2,
    CLOSE     = 8,
    PING      = 9,
    PONG      = 10
};

enum class WSError : int {
    NOERR,
    NEED_MORE_DATA,
    HANDSHAKE,
    INVALID_PARAM,
    INVALID_STATE,
    INVALID_FRAME,
    INVALID_LENGTH,
    PROTOCOL_ERROR,
    CLOSED,
    DESTROYED
};

enum class WSMode {
    CLIENT,
    SERVER
};

typedef struct FrameHeader {
    uint8_t fin:1;
    uint8_t rsv1:1;
    uint8_t rsv2:1;
    uint8_t rsv3:1;
    uint8_t opcode:4;
    uint8_t mask:1;
    uint8_t plen:7;
    union{
        uint16_t xpl16;
        uint64_t xpl64;
    } xpl;
    uint8_t maskey[WS_MASK_KEY_SIZE];
    uint32_t length = 0;
} FrameHeader;

WS_NS_END
