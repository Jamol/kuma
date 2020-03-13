#pragma once

#include "kmapi.h"
#include "TestLoop.h"
#include "RateReporter.h"

#include <chrono>
#include <functional>

using namespace kuma;
using namespace std::chrono;

class DataSender
{
public:
    using SendCallback = std::function<int(void*, size_t)>;
    
    DataSender(TestLoop* loop);
    ~DataSender();
    
    void updateBandwidth(size_t bw_bps);
    void startSendData();
    void doSendData();
    
    void setSendCallback(SendCallback cb) { sender_ = std::move(cb); }
    
protected:
    void sendData(size_t bytes_to_send);
    void onTimer();
    
private:
    TestLoop*   loop_;
    size_t      bandwidth_ = 1*1000*1000; // bps
    size_t      time_slice_ = 20; // ms
    size_t      token_slice_ = 0;
    
    SendCallback sender_;
    
    Timer       timer_;
    steady_clock::time_point   last_send_time_;
    
    RateReporter    send_reporter_;
};

