#pragma once

#include "kmapi.h"
#include "RunLoop.h"

#include <string>

namespace kmsvr {

class HttpHandler : public LoopObject
{
public:
    HttpHandler(const RunLoop::Ptr &loop, const std::string &ver);
    ~HttpHandler();

    kuma::KMError attachFd(kuma::SOCKET_FD fd, uint32_t ssl_flags, const kuma::KMBuffer *init_buf);
    kuma::KMError attachSocket(kuma::TcpSocket&& tcp, kuma::HttpParser&& parser, const kuma::KMBuffer *init_buf);
    kuma::KMError attachStream(uint32_t streamId, kuma::H2Connection* conn);
    void close();
    
private:
    void onSend(kuma::KMError err);
    void onClose(kuma::KMError err);
    
    void onHttpData(kuma::KMBuffer &buf);
    void onHeaderComplete();
    void onRequestComplete();
    void onResponseComplete();
    
private:
    enum class State {
        NONE,
        SENDING_FILE,
        SENDING_TEST_DATA,
        COMPLETED
    };
    void setupCallbacks();
    void sendTestFile();
    void sendTestData();
    
private:
    RunLoop*            loop_;
    kuma::HttpResponse  http_;
    State               state_ = State::NONE;
    bool                is_options_ = false;
    size_t              total_bytes_read_ = 0;
    std::string         file_name_;
};

}
