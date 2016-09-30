
#ifndef __h2defs_H__
#define __h2defs_H__

#include "http/httpdefs.h"
#include <string>
#include <vector>

KUMA_NS_BEGIN

const uint32_t H2_MAX_STREAM_ID = 0x7FFFFFFF;
const size_t H2_FRAME_HEADER_SIZE = 9;
const size_t H2_PRIORITY_PAYLOAD_SIZE = 5;
const size_t H2_RST_STREAM_PAYLOAD_SIZE = 4;
const size_t H2_SETTING_ITEM_SIZE = 6;
const size_t H2_PING_PAYLOAD_SIZE = 8;
const size_t H2_WINDOW_UPDATE_PAYLOAD_SIZE = 4;
const size_t H2_WINDOW_UPDATE_FRAME_SIZE = + H2_FRAME_HEADER_SIZE + H2_WINDOW_UPDATE_PAYLOAD_SIZE;

const uint32_t H2_DEFAULT_FRAME_SIZE = 16384;
const uint32_t H2_DEFAULT_WINDOW_SIZE = 65535;
const uint32_t H2_MAX_WINDOW_SIZE = 2147483647;

const uint8_t H2_FRAME_FLAG_END_STREAM = 0x1;
const uint8_t H2_FRAME_FLAG_ACK = 0x1;
const uint8_t H2_FRAME_FLAG_END_HEADERS = 0x4;
const uint8_t H2_FRAME_FLAG_PADDED = 0x8;
const uint8_t H2_FRAME_FLAG_PRIORITY = 0x20;

enum H2FrameType : uint8_t {
    DATA            = 0,
    HEADERS         = 1,
    PRIORITY        = 2,
    RST_STREAM      = 3,
    SETTINGS        = 4,
    PUSH_PROMISE    = 5,
    PING            = 6,
    GOAWAY          = 7,
    WINDOW_UPDATE   = 8,
    CONTINUATION    = 9
};

enum H2SettingsId : uint16_t {
    HEADER_TABLE_SIZE       = 1,
    ENABLE_PUSH             = 2,
    MAX_CONCURRENT_STREAMS  = 3,
    INITIAL_WINDOW_SIZE     = 4,
    MAX_FRAME_SIZE          = 5,
    MAX_HEADER_LIST_SIZE    = 6
};

enum class H2Error : int32_t {
    NOERR                   = 0x0,
    PROTOCOL_ERROR          = 0x1,
    INTERNAL_ERROR          = 0x2,
    FLOW_CONTROL_ERROR      = 0x3,
    SETTINGS_TIMEOUT        = 0x4,
    STREAM_CLOSED           = 0x5,
    FRAME_SIZE_ERROR        = 0x6,
    REFUSED_STREAM          = 0x7,
    CANCEL                  = 0x8,
    COMPRESSION_ERROR       = 0x9,
    CONNECT_ERROR           = 0xa,
    ENHANCE_YOUR_CALM       = 0xb,
    INADEQUATE_SECURITY     = 0xc,
    HTTP_1_1_REQUIRED       = 0xd
};

using ParamVector = std::vector<std::pair<uint16_t, uint32_t>>;

const std::string H2HeaderMethod(":method");
const std::string H2HeaderScheme(":scheme");
const std::string H2HeaderAuthority(":authority");
const std::string H2HeaderPath(":path");
const std::string H2HeaderStatus(":status");

inline bool isPromisedStream(uint32_t streamId) {
    return !(streamId & 1);
}

KUMA_NS_END

#endif
