#pragma once

#include <string>
#include <chrono>

using namespace std::chrono;

class RateReporter
{
public:
    RateReporter(std::string rate_name)
    : rate_name_(std::move(rate_name))
    {
        
    };
    
    void report(size_t value, steady_clock::time_point now_time)
    {
        sum_received_ += value;
        if (!value_received_) {
            value_received_ = true;
            last_report_time_ = now_time;
        }
        auto report_diff = (size_t)duration_cast<milliseconds>(now_time - last_report_time_).count();
        if (report_diff > 1000) {
            auto rate = (sum_received_ - sum_reported_) * 1000 / report_diff;
            printf("RateReporter, %s=%zu\n", rate_name_.c_str(), rate);
            last_report_time_ = now_time;
            sum_reported_ = sum_received_;
        }
    }
    
    void reset()
    {
        value_received_ = false;
        sum_received_ = 0;
        sum_received_ = 0;
    }
    
private:
    std::string rate_name_;
    bool        value_received_ = false;
    size_t      sum_received_ = 0;
    size_t      sum_reported_ = 0;
    
    steady_clock::time_point   last_report_time_;
};

