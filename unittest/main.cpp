
#include <stdlib.h>
#include <gtest/gtest.h>

int main(int argc, const char *argv[])
{
    //::testing::GTEST_FLAG(output) = "xml:./kuma_test.xml";
    ::testing::InitGoogleTest(&argc, (char **)(argv));

    auto ret = RUN_ALL_TESTS();
    return ret;
}
