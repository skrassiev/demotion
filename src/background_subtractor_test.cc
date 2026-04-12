#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <print>
#include <vector>

#include "background_subtractor.h" // Adjust path as needed

auto print_if_motion = [](const std::vector<uint8_t> &map, int width,
                          int height) {
  bool has_motion =
      std::any_of(map.begin(), map.end(), [](uint8_t val) { return val > 0; });

  if (has_motion) {
    std::println("--- Motion Detected! Mapping Grid ---");
    for (int y = 0; y < height; y += 4) { // Sample every 4th row to save logs
      for (int x = 0; x < width; x += 4) {
        std::print("{}", (map[y * width + x] > 0 ? '#' : '.'));
      }
      std::println("");
    }
  }
};

// Define your grid dimensions
const int width = 320;
const int height = 240;

// The printing lambda
auto print_grid = [i = 0](uint8_t val) mutable {
  // Print the pixel
  std::print("{}", (val > 0 ? '#' : '.'));
  std::print("{}", val);

  // Check if we hit the end of a row
  if (++i % width == 0) {
    std::println("");
  }
};

TEST(BackgroundSubtractorTest, DetectsMotionAndLearns) {
  const int width = 4;
  const int height = 4;
  BackgroundSubtractor bs(width, height, 0.1f); // Faster learning for test

  // 1. Create a static background
  std::vector<uint8_t> background(width * height, 100);
  std::vector<uint8_t> motion_map(width * height);

  std::println("Background.size: {}, motion_map.size: {}", background.size(),
               motion_map.size());
  std::for_each(background.begin(), background.end(), print_grid);

  // Initial pass: Algorithm should see "no motion" and learn the background
  bs.Process(background, motion_map);
  std::println("\nMotion Map:");
  std::for_each(motion_map.begin(), motion_map.end(), print_grid);
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
  for (int i = 0; i < 5; ++i) {
    for (auto &p : frame)
      p += 1; // Slow drift
    bs.Process(frame, motion_map);
  }

  // After 5 frames, the model should have adapted
  // No single pixel change was > 15, so motion_map should be all 0s
  for (auto val : motion_map) {
    EXPECT_EQ(val, 0);
  }
}
