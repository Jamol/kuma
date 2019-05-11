
#include <stdlib.h>
#include <gtest/gtest.h>

int main(int argc, const char *argv[])
{
    //::testing::GTEST_FLAG(output) = "xml:./kuma_test.xml";
    ::testing::InitGoogleTest(&argc, (char **)(argv));

    //::testing::FLAGS_gtest_filter = "test*";
    auto ret = RUN_ALL_TESTS();
    return ret;
}
