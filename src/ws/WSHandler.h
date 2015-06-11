#ifndef __WSHandler_H__
#define __WSHandler_H__

#include "kmdefs.h"
#include "HttpParser.h"
#include <vector>

KUMA_NS_BEGIN

class WSHandler
{
public:
    typedef enum{
        WS_FRAME_TYPE_TEXT  = 1,
        WS_FRAME_TYPE_BINARY
    }FrameType;
    typedef enum{
        WS_ERROR_NOERR,
        WS_ERROR_NEED_MORE_DATA,
        WS_ERROR_HANDSHAKE,
        WS_ERROR_INVALID_PARAM,
        WS_ERROR_INVALID_STATE,
        WS_ERROR_INVALID_FRAME,
        WS_ERROR_INVALID_LENGTH,
        WS_ERROR_CLOSED,
        WS_ERROR_DESTROY
    }WSError;
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> HandshakeCallback;
    
    WSHandler();
    ~WSHandler();
    
    std::string buildRequest(const std::string& path, const std::string& host);
    std::string buildResponse();
    
    WSError handleData(uint8_t* data, uint32_t len);
    int encodeFrameHeader(FrameType frame_type, uint32_t frame_len, uint8_t frame_hdr[10]);
    
    void setDataCallback(DataCallback& cb) { cb_data_ = cb; }
    void setHandshakeCallback(HandshakeCallback& cb) { cb_handshake_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setHandshakeCallback(HandshakeCallback&& cb) { cb_handshake_ = std::move(cb); }
    
private:
    typedef enum {
        FRAME_DECODE_STATE_HDR1,
        FRAME_DECODE_STATE_HDR2,
        FRAME_DECODE_STATE_HDREX,
        FRAME_DECODE_STATE_MASKEY,
        FRAME_DECODE_STATE_DATA,
        FRAME_DECODE_STATE_CLOSED,
        FRAME_DECODE_STATE_ERROR,
    }FrameDecodeState;
    
    typedef struct FrameHeader {
        FrameHeader()
        {
            reset();
        }
        void reset() {
            u1.UByte = 0;
            u2.UByte = 0;
            xpl.xpl64 = 0;
            length = 0;
            
            state = FRAME_DECODE_STATE_HDR1;
            buffer.clear();
            de_pos = 0;
        }
        union{
            struct{
                uint8_t Opcode:4;
                uint8_t Rsv3:1;
                uint8_t Rsv2:1;
                uint8_t Rsv1:1;
                uint8_t Fin:1;
            }HByte;
            uint8_t UByte;
        }u1;
        union{
            struct{
                uint8_t PayloadLen:7;
                uint8_t Mask:1;
            }HByte;
            uint8_t UByte;
        }u2;
        union{
            uint16_t xpl16;
            uint64_t xpl64;
        }xpl;
        uint8_t maskey[4];
        uint32_t length;
        
        FrameDecodeState state;
        std::vector<uint8_t> buffer;
        uint8_t de_pos;
    }FrameHeader;
    void cleanup();
    
    static WSError handleDataMask(FrameHeader& hdr, uint8_t* data, uint32_t len);
    WSError decodeFrame(uint8_t* data, uint32_t len);
    
    void onHttpData(const char* data, uint32_t len);
    void onHttpEvent(HttpParser::HttpEvent ev);
    
    void handleRequest();
    void handleResponse();
    
private:
    typedef enum {
        STATE_HANDSHAKE,
        STATE_OPEN,
        STATE_ERROR,
        STATE_DESTROY
    }State;
    State                   state_;
    FrameHeader             frame_hdr_;
    
    HttpParser              http_parser_;
    
    DataCallback            cb_data_;
    HandshakeCallback       cb_handshake_;
    
    bool*                   destroy_flag_ptr_;
};

KUMA_NS_END

#endif
