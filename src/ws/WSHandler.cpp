
#include "WSHandler.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "util/base64.h"

#include <sstream>
#include <openssl/sha.h>

KUMA_NS_BEGIN

//////////////////////////////////////////////////////////////////////////
WSHandler::WSHandler()
: state_(STATE_HANDSHAKE)
, destroy_flag_ptr_(nullptr)
{
    http_parser_.setDataCallback([this] (const char* data, uint32_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpParser::HttpEvent ev) { onHttpEvent(ev); });
}

WSHandler::~WSHandler()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

void WSHandler::cleanup()
{
    ctx_.reset();
}

#define SHA1_DIGEST_SIZE	20
std::string generate_sec_accept_value(const std::string& sec_ws_key)
{
    static const std::string sec_accept_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    if(sec_ws_key.empty()) {
        return "";
    }
    std::string accept = sec_ws_key;
    accept += sec_accept_guid;
    
    uint8_t uShaRst2[SHA1_DIGEST_SIZE] = {0};
    SHA1((const uint8_t *)accept.c_str(), accept.size(), uShaRst2);
    
    uint8_t x64_encode_buf[32] = {0};
    uint32_t x64_encode_len = x64_encode(uShaRst2, SHA1_DIGEST_SIZE, x64_encode_buf, sizeof(x64_encode_buf), false);
    
    return std::string((char*)x64_encode_buf, x64_encode_len);
}

std::string WSHandler::buildRequest(const std::string& path, const std::string& host,
                                    const std::string& proto, const std::string& origin)
{
    std::stringstream ss;
    ss << "GET ";
    ss << path;
    ss << " HTTP/1.1\r\n";
    ss << "Host: " << host << "\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Origin: " << origin << "\r\n";
    ss << "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    ss << "Sec-WebSocket-Protocol: " << proto << "\r\n";
    ss << "Sec-WebSocket-Version: 13\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string WSHandler::buildResponse()
{
    std::string sec_ws_key = http_parser_.getHeaderValue("Sec-WebSocket-Key");
    std::string protos = http_parser_.getHeaderValue("Sec-WebSocket-Protocol");
    
    std::stringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Sec-WebSocket-Accept: " << generate_sec_accept_value(sec_ws_key) << "\r\n";
    if(!protos.empty()) {
        ss << "Sec-WebSocket-Protocol: " << protos << "\r\n";
    }
    ss << "\r\n";
    return ss.str();
}

const std::string WSHandler::getProtocol()
{
    return http_parser_.getHeaderValue("Sec-WebSocket-Protocol");
}

const std::string WSHandler::getOrigin()
{
    return http_parser_.getHeaderValue("Origin");
}

void WSHandler::onHttpData(const char* data, uint32_t len)
{
    KUMA_ERRTRACE("WSHandler::onHttpData, len="<<len);
}

void WSHandler::onHttpEvent(HttpParser::HttpEvent ev)
{
    KUMA_INFOTRACE("WSHandler::onHttpEvent, ev="<<ev);
    switch (ev) {
        case HttpParser::HTTP_HEADER_COMPLETE:
            break;
            
        case HttpParser::HTTP_COMPLETE:
            if(http_parser_.isRequest()) {
                handleRequest();
            } else {
                handleResponse();
            }
            break;
            
        case HttpParser::HTTP_ERROR:
            state_ = STATE_ERROR;
            break;
            
        default:
            break;
    }
}

void WSHandler::handleRequest()
{
    if(!is_equal(http_parser_.getHeaderValue("Upgrade"), "WebSocket") ||
       !is_equal(http_parser_.getHeaderValue("Connection"), "Upgrade")) {
        state_ = STATE_ERROR;
        KUMA_INFOTRACE("WSHandler::handleRequest, not WebSocket request");
        if(cb_handshake_) cb_handshake_(KUMA_ERROR_INVALID_PROTO);
        return;
    }
    std::string sec_ws_key = http_parser_.getHeaderValue("Sec-WebSocket-Key");
    if(sec_ws_key.empty()) {
        state_ = STATE_ERROR;
        KUMA_INFOTRACE("WSHandler::handleRequest, no Sec-WebSocket-Key");
        if(cb_handshake_) cb_handshake_(KUMA_ERROR_INVALID_PROTO);
        return;
    }
    state_ = STATE_OPEN;
    if(cb_handshake_) cb_handshake_(0);
}

void WSHandler::handleResponse()
{
    if(101 == http_parser_.getStatusCode() &&
       is_equal(http_parser_.getHeaderValue("Upgrade"), "WebSocket") &&
       is_equal(http_parser_.getHeaderValue("Connection"), "Upgrade")) {
        state_ = STATE_OPEN;
        if(cb_handshake_) cb_handshake_(0);
    } else {
        state_ = STATE_ERROR;
        KUMA_INFOTRACE("WSHandler::handleResponse, invalid status code: "<<http_parser_.getStatusCode());
        if(cb_handshake_) cb_handshake_(-1);
    }
}

WSHandler::WSError WSHandler::handleData(uint8_t* data, uint32_t len)
{
    if(state_ == STATE_OPEN) {
        return decodeFrame(data, len);
    }
    if(state_ == STATE_HANDSHAKE) {
        bool destroyed = false;
        KUMA_ASSERT(nullptr == destroy_flag_ptr_);
        destroy_flag_ptr_ = &destroyed;
        int bytes_used = http_parser_.parse((char*)data, len);
        if(destroyed) {
            return WS_ERROR_DESTROY;
        }
        destroy_flag_ptr_ = nullptr;
        if(state_ == STATE_ERROR) {
            return WS_ERROR_HANDSHAKE;
        }
        if(bytes_used < len && state_ == STATE_OPEN) {
            return decodeFrame(data + bytes_used, len - bytes_used);
        }
    } else {
        return WS_ERROR_INVALID_STATE;
    }
    return WS_ERROR_NOERR;
}

int WSHandler::encodeFrameHeader(FrameType frame_type, uint32_t frame_len, uint8_t frame_hdr[10])
{
    uint8_t first_byte = 0x81;
    uint8_t second_byte = 0x00;
    uint8_t hdr_len = 2;
    if(frame_type == WS_FRAME_TYPE_BINARY)
    {
        first_byte = 0x82;
    }
    
    if(frame_len <= 125)
    {//0 byte
        second_byte = (uint8_t)frame_len;
    }
    else if(frame_len <= 0xffff)
    {//2 bytes
        hdr_len += 2;
        second_byte = 126;
    }
    else
    {//8 bytes
        hdr_len += 8;
        second_byte = 127;
    }
    
    frame_hdr[0] = first_byte;
    frame_hdr[1] = second_byte;
    if(126 == second_byte)
    {
        frame_hdr[2] = (unsigned char)(frame_len >> 8);
        frame_hdr[3] = (unsigned char)frame_len;
    }
    else if(127 == second_byte)
    {
        frame_hdr[2] = 0;
        frame_hdr[3] = 0;
        frame_hdr[4] = 0;
        frame_hdr[5] = 0;
        frame_hdr[6] = (unsigned char)(frame_len >> 24);
        frame_hdr[7] = (unsigned char)(frame_len >> 16);
        frame_hdr[8] = (unsigned char)(frame_len >> 8);
        frame_hdr[9] = (unsigned char)frame_len;
    }
    return hdr_len;
}

WSHandler::WSError WSHandler::decodeFrame(uint8_t* data, uint32_t len)
{
#define UB1	ctx_.hdr.u1.HByte
#define UB2	ctx_.hdr.u2.HByte
#define WS_MAX_FRAME_DATA_LENGTH	10*1024*1024
    
    uint32_t pos = 0;
    while(pos < len)
    {
        switch(ctx_.state)
        {
            case FRAME_DECODE_STATE_HDR1:
                ctx_.hdr.u1.UByte = data[pos++];
                if(UB1.Rsv1 != 0 || UB1.Rsv2 != 0 || UB1.Rsv3 != 0) {
                    ctx_.state = (FRAME_DECODE_STATE_ERROR);
                    return WS_ERROR_INVALID_FRAME;
                }
                ctx_.state = FRAME_DECODE_STATE_HDR2;
                break;
            case FRAME_DECODE_STATE_HDR2:
                ctx_.hdr.u2.UByte = data[pos++];
                ctx_.hdr.xpl.xpl64 = 0;
                ctx_.pos = 0;
                ctx_.buf.clear();
                ctx_.state = FRAME_DECODE_STATE_HDREX;
                break;
            case FRAME_DECODE_STATE_HDREX:
                if(126 == UB2.PayloadLen) {
                    uint32_t expect_len = 2;
                    if(len-pos+ctx_.pos >= expect_len) {
                        for (; ctx_.pos<expect_len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl16 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        ctx_.pos = 0;
                        if(ctx_.hdr.xpl.xpl16 < 126)
                        {// invalid ex payload length
                            ctx_.state = (FRAME_DECODE_STATE_ERROR);
                            return WS_ERROR_INVALID_LENGTH;
                        }
                        ctx_.hdr.length = ctx_.hdr.xpl.xpl16;
                        ctx_.state = FRAME_DECODE_STATE_MASKEY;
                    } else {
                        ctx_.hdr.xpl.xpl16 |= data[pos++]<<8;
                        ++ctx_.pos;
                        return WS_ERROR_NEED_MORE_DATA;
                    }
                } else if(127 == UB2.PayloadLen) {
                    uint32_t expect_len = 8;
                    if(len-pos+ctx_.pos >= expect_len) {
                        for (; ctx_.pos<expect_len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl64 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        ctx_.pos = 0;
                        if((ctx_.hdr.xpl.xpl64>>63) != 0)
                        {// invalid ex payload length
                            ctx_.state = (FRAME_DECODE_STATE_ERROR);
                            return WS_ERROR_INVALID_LENGTH;
                        }
                        ctx_.hdr.length = (uint32_t)ctx_.hdr.xpl.xpl64;
                        if(ctx_.hdr.length > WS_MAX_FRAME_DATA_LENGTH)
                        {// invalid ex payload length
                            ctx_.state = FRAME_DECODE_STATE_ERROR;
                            return WS_ERROR_INVALID_LENGTH;
                        }
                        ctx_.state = FRAME_DECODE_STATE_MASKEY;
                    } else {
                        for (; pos<len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl64 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        return WS_ERROR_NEED_MORE_DATA;
                    }
                } else {
                    ctx_.hdr.length = UB2.PayloadLen;
                    ctx_.state = FRAME_DECODE_STATE_MASKEY;
                }
                break;
            case FRAME_DECODE_STATE_MASKEY:
                if(UB2.Mask) {
                    uint32_t expect_len = 4;
                    if(len-pos+ctx_.pos >= expect_len)
                    {
                        memcpy(ctx_.hdr.maskey+ctx_.pos, data+pos, expect_len-ctx_.pos);
                        pos += expect_len-ctx_.pos;
                        ctx_.pos = 0;
                    } else {
                        memcpy(ctx_.hdr.maskey+ctx_.pos, data+pos, len-pos);
                        ctx_.pos += len-pos;
                        pos = len;
                        return WS_ERROR_NEED_MORE_DATA;
                    }
                }
                ctx_.buf.clear();
                ctx_.state = FRAME_DECODE_STATE_DATA;
                break;
            case FRAME_DECODE_STATE_DATA:
                if(0x8 == UB1.Opcode) {
                    // connection closed
                    ctx_.state = (FRAME_DECODE_STATE_CLOSED);
                    return WS_ERROR_CLOSED;
                }
                if(ctx_.buf.empty() && len-pos >= ctx_.hdr.length) {
                    uint8_t* notify_data = data + pos;
                    uint32_t notify_len = ctx_.hdr.length;
                    pos += notify_len;
                    handleDataMask(ctx_.hdr, notify_data, notify_len);
                    KUMA_ASSERT(!destroy_flag_ptr_);
                    bool destroyed = false;
                    destroy_flag_ptr_ = &destroyed;
                    if(cb_data_) cb_data_(notify_data, notify_len);
                    if(destroyed) {
                        return WS_ERROR_DESTROY;
                    }
                    destroy_flag_ptr_ = nullptr;
                    ctx_.reset();
                } else if(len-pos+ctx_.buf.size() >= ctx_.hdr.length) {
                    auto buf_size = ctx_.buf.size();
                    auto read_len = ctx_.hdr.length - buf_size;
                    ctx_.buf.reserve(ctx_.hdr.length);
                    ctx_.buf.insert(ctx_.buf.end(), data + pos, data + pos + read_len);
                    uint8_t* notify_data = &ctx_.buf[0];
                    uint32_t notify_len = ctx_.hdr.length;
                    pos += read_len;
                    handleDataMask(ctx_.hdr, notify_data, notify_len);
                    KUMA_ASSERT(!destroy_flag_ptr_);
                    bool destroyed = false;
                    destroy_flag_ptr_ = &destroyed;
                    if(cb_data_) cb_data_(notify_data, notify_len);
                    if(destroyed) {
                        return WS_ERROR_DESTROY;
                    }
                    destroy_flag_ptr_ = nullptr;
                    ctx_.reset();
                } else {
                    auto buf_size = ctx_.buf.size();
                    auto read_len = len - pos;
                    ctx_.buf.reserve(buf_size + read_len);
                    ctx_.buf.insert(ctx_.buf.end(), data + pos, data + len);
                    return WS_ERROR_NEED_MORE_DATA;
                }
                break;
            default:
                return WS_ERROR_INVALID_FRAME;
        }
    }
    return ctx_.state == FRAME_DECODE_STATE_HDR1 ? WS_ERROR_NOERR : WS_ERROR_NEED_MORE_DATA;
}

WSHandler::WSError WSHandler::handleDataMask(FrameHeader& hdr, uint8_t* data, uint32_t len)
{
    if(0 == hdr.u2.HByte.Mask) return WS_ERROR_NOERR;
    if(nullptr == data || 0 == len) return WS_ERROR_INVALID_FRAME;
    
    for(uint32_t i=0; i < len; ++i) {
        data[i] = data[i] ^ hdr.maskey[i%4];
    }
    
    return WS_ERROR_NOERR;
}

KUMA_NS_END
