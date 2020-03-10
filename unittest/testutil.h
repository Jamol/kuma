
#pragma once

#include <stdlib.h>
#include <gtest/gtest.h>

#define UT_SETUPTRACE() KM_INFOTRACE("SetUp "<<getTestCaseName())
#define UT_TEARDOWNTRACE() KM_INFOTRACE("TearDown "<<getTestCaseName())

inline std::string getTestCaseName()
{
    auto test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string(test_info->test_case_name()) + "." + test_info->name();
}

class ScopedCaseNameLogger
{
public:
    ScopedCaseNameLogger(string name = ""): case_name_(std::move(name))
    {
        if (case_name_.empty()) {
            case_name_ = getTestCaseName();
        }
        std::cout << "Entering "<< case_name_ << std::endl;
    }
    
    ~ScopedCaseNameLogger()
    {
        std::cout << "Leaving  " << case_name_ << std::endl;
    }
    
private:
    string case_name_;
};

