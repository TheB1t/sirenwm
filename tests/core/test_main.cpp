#include <gtest/gtest.h>
#include <support/log.hpp>

int main(int argc, char** argv) {
    log_init_null();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
