#include "TestLoop.h"
#include "LoopPool.h"
#include "TcpClient.h"
#include "HttpClient.h"
#include "WsClient.h"
#include "UdpClient.h"

#include <string.h>
#include <string>

TestLoop::TestLoop(LoopPool* server, PollType poll_type)
: loop_(new EventLoop(poll_type))
, server_(server)
, thread_()
{
    
}

void TestLoop::cleanup()
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    for (auto &kv : obj_map_) {
        kv.second->close();
        delete kv.second;
    }
    obj_map_.clear();
}

bool TestLoop::init()
{
    try {
        thread_ = std::thread([this] {
            run();
        });
    }
    catch(...)
    {
        return false;
    }
    return true;
}

void TestLoop::stop()
{
    //cleanup();
    if(loop_) {
        loop_->async([this] { cleanup(); });
        loop_->stop();
    }
    if(thread_.joinable()) {
        try {
            thread_.join();
        } catch (...) {
            printf("failed to join loop thread\n");
        }
    }
}

void TestLoop::run()
{
    if(!loop_->init()) {
        printf("TestLoop::run, failed to init EventLoop\n");
        return;
    }
    loop_->loop();
}

void TestLoop::startTest(const std::string& addr_url, const std::string& bind_addr)
{
    loop_->async([=] {
        startTest_i(addr_url, bind_addr);
    });
}

void TestLoop::startTest_i(const std::string& addr_url, const std::string& bind_addr)
{
    char proto[16] = {0};
    char host[64] = {0};
    uint16_t port = 0;
    if(km_parse_address(addr_url.c_str(), proto, sizeof(proto), host, sizeof(host), &port) != 0) {
        return ;
    }
    
    if(strcmp(proto, "tcp") == 0 || strcmp(proto, "tcps") == 0) {
        long conn_id = server_->getConnId();
        TcpClient* client = new TcpClient(this, conn_id);
        addObject(conn_id, client);
        if(!bind_addr.empty()) {
            char bind_host[64] = {0};
            uint16_t bind_port = 0;
            if(km_parse_address(bind_addr.c_str(), NULL, 0, bind_host, sizeof(bind_host), &bind_port) == 0) {
                client->bind(bind_host, bind_port);
            }
        }
        if (strcmp(proto, "tcps") == 0) {
            if (port == 0) port = 443;
            client->setSslFlags(SSL_ENABLE);
        }
        client->connect(host, port);
    } else if(strcmp(proto, "udp") == 0) {
        long conn_id = server_->getConnId();
        UdpClient* udp_client = new UdpClient(this, conn_id);
        addObject(conn_id, udp_client);
        if(!bind_addr.empty()) {
            char bind_host[64] = {0};
            uint16_t bind_port = 0;
            if(km_parse_address(bind_addr.c_str(), NULL, 0, bind_host, sizeof(bind_host), &bind_port) == 0) {
                udp_client->bind(bind_host, bind_port);
            }
        }
        udp_client->startSend(host, port);
    } else if(strcmp(proto, "http") == 0 || strcmp(proto, "https") == 0) {
        long conn_id = server_->getConnId();
        HttpClient* http_client = new HttpClient(this, conn_id);
        addObject(conn_id, http_client);
        http_client->startRequest(addr_url);
    } else if(strcmp(proto, "ws") == 0 || strcmp(proto, "wss") == 0) {
        long conn_id = server_->getConnId();
        WsClient* ws_client = new WsClient(this, conn_id);
        addObject(conn_id, ws_client);
        ws_client->startRequest(addr_url);
    }
}

void TestLoop::addObject(long conn_id, TestObject* obj)
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    obj_map_.insert(std::make_pair(conn_id, obj));
}

void TestLoop::removeObject(long conn_id)
{
    printf("TestLoop::removeObject, conn_id=%ld\n", conn_id);
    std::lock_guard<std::mutex> lg(obj_mutex_);
    auto it = obj_map_.find(conn_id);
    if(it != obj_map_.end()) {
        delete it->second;
        obj_map_.erase(it);
    }
}

int km_parse_address(const char* addr,
                     char* proto, int proto_len,
                     char* host, int  host_len, unsigned short* port)
{
    if(!addr || !host)
        return -1;
    
    const char* tmp1 = nullptr;
    int tmp_len = 0;
    const char* tmp = strstr(addr, "://");
    if(tmp) {
        tmp_len = int(proto_len > tmp-addr?
            tmp-addr:proto_len-1);
        
        if(proto) {
            memcpy(proto, addr, tmp_len);
            proto[tmp_len] = '\0';
        }
        tmp += 3;
    } else {
        if(proto) proto[0] = '\0';
        tmp = addr;
    }
    const char* end = strchr(tmp, '/');
    if(!end)
        end = addr + strlen(addr);
    
    tmp1 = strchr(tmp, '[');
    if(tmp1) {// ipv6 address
        tmp = tmp1 + 1;
        tmp1 = strchr(tmp, ']');
        if(!tmp1)
            return -1;
        tmp_len = int(host_len>tmp1-tmp?
            tmp1-tmp:host_len-1);
        memcpy(host, tmp, tmp_len);
        host[tmp_len] = '\0';
        tmp = tmp1 + 1;
        tmp1 = strchr(tmp, ':');
        if(tmp1 && tmp1 <= end)
            tmp = tmp1 + 1;
        else
            tmp = nullptr;
    } else {// ipv4 address
        tmp1 = strchr(tmp, ':');
        if(tmp1 && tmp1 <= end) {
            tmp_len = int(host_len>tmp1-tmp?
                tmp1-tmp:host_len-1);
            memcpy(host, tmp, tmp_len);
            host[tmp_len] = '\0';
            tmp = tmp1 + 1;
        } else {
            tmp_len = int(host_len>end-tmp?
                end-tmp:host_len-1);
            memcpy(host, tmp, tmp_len);
            host[tmp_len] = '\0';
            tmp = nullptr;
        }
    }
    
    if(port) {
        *port = tmp ? atoi(tmp) : 0;
    }
    
    return 0;
}
