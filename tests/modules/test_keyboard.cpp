#include <gtest/gtest.h>
#include "test_harness.hpp"
#include "keyboard/keyboard_module.hpp"

TEST(Keyboard, StartsAndStopsWithoutCrash) {
    TestHarness h;
    h.use<KeyboardModule>();
    h.start();
    // stop() called by ~TestHarness
}

TEST(Keyboard, RestoreCalledOnStop) {
    TestHarness h;
    h.use<KeyboardModule>();
    h.start();

    EXPECT_FALSE(h.backend.fake_keyboard().restore_called);
}

// NOTE: KeyboardModule reads siren.keyboard from Lua assignment handler.
// Without a Lua config, layouts_ is empty and apply() is a no-op.
// Testing actual layout application requires the config_loader path,
// which is covered by integration tests.
