#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include "src/background_subtractor.h" // Adjust path as needed

TEST(BackgroundSubtractorTest, DetectsMotionAndLearns) {
    const int width = 4;
    const int height = 4;
    BackgroundSubtractor bs(width, height, 0.1f); // Faster learning for test

    // 1. Create a static background
    std::vector<uint8_t> background(width * height, 100);
    std::vector<uint8_t> motion_map(width * height);

    // Initial pass: Algorithm should see "no motion" and learn the background
    bs.Process(background, motion_map);
    for (auto val : motion_map) {
        EXPECT_EQ(val, 0); // No motion
    }

    // 2. Introduce a moving object (high intensity change)
    std::vector<uint8_t> with_motion = background;
    with_motion[5] = 200; // Change one pixel significantly
    with_motion[6] = 200;

    bs.Process(with_motion, motion_map);

    // Verify detection
    EXPECT_EQ(motion_map[5], 255);
    EXPECT_EQ(motion_map[6], 255);
    EXPECT_EQ(motion_map[0], 0); // Still 0
}

TEST(BackgroundSubtractorTest, IgnoresSlowChanges) {
    const int width = 2;
    const int height = 2;
    // Learning rate 0.1 means it adapts quickly
    BackgroundSubtractor bs(width, height, 0.1f);

    std::vector<uint8_t> frame(width * height, 100);
    std::vector<uint8_t> motion_map(width * height);

    // Slowly "drift" the background
    for(int i = 0; i < 5; ++i) {
        for(auto& p : frame) p += 1; // Slow drift
        bs.Process(frame, motion_map);
    }

    // After 5 frames, the model should have adapted
    // No single pixel change was > 15, so motion_map should be all 0s
    for (auto val : motion_map) {
        EXPECT_EQ(val, 0);
    }
}
