#include "DataSender.h"


DataSender::DataSender(TestLoop* loop)
: loop_(loop)
, timer_(loop->eventLoop())
, send_reporter_("send_bitrate")
{
    token_slice_ = bandwidth_ * time_slice_ / 1000 / 8;
}

DataSender::~DataSender()
{
    timer_.cancel();
}

void DataSender::updateBandwidth(size_t bw_bps)
{
    bandwidth_ = bw_bps;
    token_slice_ = bandwidth_ * time_slice_ / 1000 / 8;
}

void DataSender::startSendData()
{
    timer_.schedule((uint32_t)time_slice_, TimerMode::REPEATING, [this] { onTimer(); });
    last_send_time_ = steady_clock::now();
    sendData(token_slice_);
}

void DataSender::sendData(size_t bytes_to_send)
{
    size_t bytes_sent = 0;
    uint8_t buf[1024];
    while(bytes_to_send > 0) {
        auto send_len = sizeof(buf);
        if (send_len > bytes_to_send) {
            send_len = bytes_to_send;
        }
        int ret = sender_(buf, send_len);
        if (ret > 0) {
            bytes_to_send -= ret;
            bytes_sent += ret;
        }
        if (ret < (int)send_len) {
            //printf("DataSender::sendData, last_len=%u\n", ret);
            break;
        }
    }
    
    if (bytes_sent > 0) {
        send_reporter_.report(bytes_sent * 8, steady_clock::now());
    }
}

void DataSender::doSendData()
{
    auto now_time = steady_clock::now();
    auto time_diff = duration_cast<milliseconds>(now_time - last_send_time_).count();
    auto avai_token = bandwidth_ * uint32_t(time_diff) / 1000 / 8;
    last_send_time_ = now_time;
    sendData(avai_token);
}

void DataSender::onTimer()
{
    doSendData();
}
