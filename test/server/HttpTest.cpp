#include "HttpTest.h"
#include "TestLoop.h"
#include "testutil.h"

#include <string.h> // strcasecmp
#include <string>
#include <stdio.h>

extern std::string www_path;

HttpTest::HttpTest(ObjectManager* obj_mgr, long conn_id, const std::string &ver)
: obj_mgr_(obj_mgr)
, http_(obj_mgr->eventLoop(), ver.c_str())
, conn_id_(conn_id)
{
    
}

void HttpTest::setupCallbacks()
{
    http_.setWriteCallback([this] (KMError err) { onSend(err); });
    http_.setErrorCallback([this] (KMError err) { onClose(err); });
    
    http_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_.setHeaderCompleteCallback([this] () { onHeaderComplete(); });
    http_.setRequestCompleteCallback([this] () { onRequestComplete(); });
    http_.setResponseCompleteCallback([this] () { onResponseComplete(); });
}

KMError HttpTest::attachFd(SOCKET_FD fd, uint32_t ssl_flags, const KMBuffer *init_buf)
{
    setupCallbacks();
    http_.setSslFlags(ssl_flags);
    return http_.attachFd(fd, init_buf);
}

KMError HttpTest::attachSocket(TcpSocket&& tcp, HttpParser&& parser, const KMBuffer *init_buf)
{
    setupCallbacks();
    return http_.attachSocket(std::move(tcp), std::move(parser), init_buf);
}

KMError HttpTest::attachStream(H2Connection* conn, uint32_t streamId)
{
    setupCallbacks();
    return conn->attachStream(streamId, &http_);
}

int HttpTest::close()
{
    http_.close();
    return 0;
}

void HttpTest::onSend(KMError err)
{
    if (state_ == State::SENDING_FILE) {
        sendTestFile();
    } else if (state_ == State::SENDING_TEST_DATA) {
        sendTestData();
    }
}

void HttpTest::onClose(KMError err)
{
    printf("HttpTest_%ld::onClose, err=%d\n", conn_id_, err);
    http_.close();
    obj_mgr_->removeObject(conn_id_);
}

void HttpTest::onHttpData(KMBuffer &buf)
{
    total_bytes_read_ += buf.chainLength();
    //printf("HttpClient_%ld::onHttpData, len=%zu, total=%zu\n", conn_id_, buf.chainLength(), total_bytes_read_);
}

void HttpTest::onHeaderComplete()
{
    printf("HttpTest_%ld::onHeaderComplete\n", conn_id_);
}

void HttpTest::onRequestComplete()
{
    printf("HttpTest_%ld::onRequestComplete, total=%zu\n", conn_id_, total_bytes_read_);
    if (strcasecmp(http_.getMethod(), "OPTIONS") == 0) {
        http_.addHeader("Content-Length", (uint32_t)0);
        is_options_ = true;
    }
    //bool isHttp2 = strcasecmp(http_.getVersion(), "HTTP/2.0") == 0;
    //http_.addHeader("Access-Control-Allow-Origin", "*");
    const char* hdr = http_.getHeaderValue("Access-Control-Request-Headers");
    if (hdr) {
        http_.addHeader("Access-Control-Allow-Headers", hdr);
    }
    hdr = http_.getHeaderValue("Access-Control-Request-Method");
    if (hdr) {
        http_.addHeader("Access-Control-Allow-Methods", hdr);
    }
    printf("HttpTest_%ld, path: %s\n", conn_id_, http_.getPath());
    int status = 200;
    std::string desc("OK");
    if (!is_options_) {
        std::string file = www_path;
        if (strcasecmp(http_.getPath(), "/") == 0) {
            file += PATH_SEPARATOR;
            file += "index.html";
            state_ = State::SENDING_FILE;
        } else if (strcasecmp(http_.getPath(), "/testdata") == 0) {
            state_ = State::SENDING_TEST_DATA;
            size_t contentLength = 256*1024*1024;
            if (http_.getHeaderValue("User-Agent")) {
                std::string userAgent = http_.getHeaderValue("User-Agent");
                if (userAgent.find("kuma") == std::string::npos) {
                    // maybe browser, reduce the content length to decrease memory consuming
                    contentLength = 128*1024*1024;
                }
            }
            http_.addHeader("Content-Length", (uint32_t)contentLength);
        } else {
            file += http_.getPath();
            state_ = State::SENDING_FILE;
        }
        if (State::SENDING_FILE == state_) {
            if (fileExist(file)) {
                file_name_ = std::move(file);
                std::string path, name, ext;
                splitPath(file_name_, path, name, ext);
                http_.addHeader("Content-Type", getMime(ext).c_str());
            } else {
                file_name_.clear();
                status = 404;
                desc = "Not Found";
                http_.addHeader("Content-Type", "text/html");
            }
            http_.addHeader("Transfer-Encoding", "chunked");
        }
    }
    http_.sendResponse(status, desc.c_str());
}

void HttpTest::onResponseComplete()
{
    printf("HttpTest_%ld::onResponseComplete\n", conn_id_);
    http_.reset();
}

void HttpTest::sendTestFile()
{
    FILE *fp = nullptr;
#ifdef KUMA_OS_WIN
    auto err = fopen_s(&fp, file_name_.c_str(), "rb");
#else
    fp = fopen(file_name_.c_str(), "rb");
#endif
    if (!fp) {
        printf("failed to open file %s\n", file_name_.c_str());
        file_name_.clear();
    }
    if (file_name_.empty()) {
        static const std::string not_found("<html><body>404 Not Found!</body></html>");
        //http_.sendData((const uint8_t*)(not_found.c_str()), not_found.size());
        //http_.sendData(nullptr, 0);
        KMBuffer buf(not_found.c_str(), not_found.size(), not_found.size());
        http_.sendData(buf);
        buf.reset();
        http_.sendData(buf);
        return;
    }
    //uint8_t buf[4096];
    //auto nread = fread(buf, 1, sizeof(buf), fp);
    KMBuffer buf(4096);
    auto nread = fread(buf.writePtr(), 1, buf.space(), fp);
    buf.bytesWritten(nread);
    fclose(fp);
    if (nread > 0) {
        //int ret = http_.sendData(buf, nread);
        int ret = http_.sendData(buf);
        if (ret < 0) {
            return;
        } else if (ret < nread) {
            // should buffer remain data
            return;
        } else {
            // end response
            http_.sendData(nullptr, 0);
        }
    }
}

void HttpTest::sendTestData()
{
    if (is_options_) {
        return;
    }
    const size_t kBufferSize = 16*1024;
    uint8_t buf[kBufferSize];
    memset(buf, 'a', sizeof(buf));
    
    KMBuffer buf1(buf, kBufferSize/2, kBufferSize/2);
    KMBuffer buf2(buf + kBufferSize/2, kBufferSize/2, kBufferSize/2);
    buf1.append(&buf2);
    
    while (true) {
        int ret = http_.sendData(buf1);
        if (ret < 0) {
            break;
        } else if (ret < buf1.chainLength()) {
            // should buffer remain data
            break;
        }
    }
}
