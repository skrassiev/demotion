#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <print>
#include <vector>

#include "background_subtractor.h" // Adjust path as needed

namespace {

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

// The printing lambda
auto print_grid = [width = 16, i = 0](uint8_t val) mutable {
  // Print the pixel
  std::print("{}", (val > 0 ? '#' : '.'));
  std::print("{}", val);

  // Check if we hit the end of a row
  if (++i % width == 0) {
    std::println("");
  }
};

} // namespace

TEST(BackgroundSubtractorTest, DetectsMotionAndLearns) {
  const int width = 4;
  const int height = 4;
  BackgroundSubtractor bs(width, height); // Faster learning for test

  // 1. Create a static background
  std::vector<uint8_t> frame(width * height, 100);
  std::vector<uint8_t> motion_map(width * height);

  std::println("Frame.size: {}, motion_map.size: {}", frame.size(),
               motion_map.size());
  std::println("\nInitial Frame:");
  std::for_each(frame.begin(), frame.end(), print_grid);
  std::println("\nInitial Motion Map:");
  std::for_each(motion_map.begin(), motion_map.end(), print_grid);
  std::println("\nInitial Background:");
  bs.PrintBackground(print_grid);

  // Initial pass: Algorithm should see "no motion" and learn the background
  bs.Process(frame, motion_map);
  std::println("\nMotion Map:");
  std::for_each(motion_map.begin(), motion_map.end(), print_grid);
  std::println("\nBackground:");
  bs.PrintBackground(print_grid);
  for (auto val : motion_map) {
    EXPECT_EQ(val, 0); // Background is initialized; everything is static
  }

  std::println("\nLearned background:");
  std::for_each(frame.begin(), frame.end(), print_grid);
  std::println();

  // Run for a few frames to learn the background
  for (int i = 0; i < 10; ++i) {
    bs.Process(frame, motion_map);
  }

  // Now introduce motion
  std::vector<uint8_t> with_motion = frame;
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
  BackgroundSubtractor bs(width, height);

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

TEST(BackgroundSubtractorTest, SimulatesSwayingLeaves) {
  const int width = 4;
  const int height = 4;
  BackgroundSubtractor bs(width, height);

  std::vector<uint8_t> frame(width * height, 100);
  std::vector<uint8_t> motion_map(width * height);

  // 1. Warm up: Learn the solid background 100
  for (int i = 0; i < 20; ++i) {
    bs.Process(frame, motion_map);
  }

  // 2. Simulate swaying: Pixel 0 oscillates between 90 and 110
  // threshold is 15, so 100 +/- 10 should be ignored as motion
  // if the background model stays near 100.
  std::println("\n--- Simulating Swaying Leaves (Pixel 0) ---");
  for (int i = 0; i < 60; ++i) {
    // Oscillate using a simple triangle wave or sine
    // We'll use a value that stays within +/- 10 of the background
    uint8_t swaying_val =
        static_cast<uint8_t>(100.0 + 10.0 * std::sin(i * 0.5));
    frame[0] = swaying_val;

    bs.Process(frame, motion_map);

    // Because the diff (max 10) is less than the threshold (15),
    // it should not trigger motion.
    EXPECT_EQ(motion_map[0], 0) << "Swaying leaf triggered motion at frame "
                                << i << " (val: " << (int)swaying_val << ")";
  }

  // 3. Verify the background model updated slightly but stayed stable
  // We can't easily check background_model_ directly if it's private,
  // but we can test it with a value that should now be "motion".
  frame[0] = 160; // Clearly motion (160 - ~100 > 15)
  bs.Process(frame, motion_map);
  EXPECT_EQ(motion_map[0], 255);
}

TEST(BackgroundSubtractorTest, SimulatesMovingCar) {
  const int width = 320;
  const int height = 240;
  BackgroundSubtractor bs(width, height);

  // Constants for 75-degree FOV at 30m distance, 25mph speed, 30fps
  // Calculations:
  // Pixels per meter @ 30m: 320 / (2 * 30 * tan(37.5 deg)) = 6.95 px/m
  // Speed in meters/frame: (25 mph / 2.237) / 30 fps = 0.3725 m/f
  // Speed in pixels/frame: 0.3725 * 6.95 = 2.59 px/f
  const float speed_px_f = 2.59f;
  const int car_w = 12; // ~1.8m * 6.95
  const int car_h = 10; // ~1.4m * 6.95

  std::vector<uint8_t> frame(width * height, 100);
  std::vector<uint8_t> motion_map(width * height);

  // 1. Initial learning
  bs.Process(frame, motion_map);

  // 2. Simulate car moving across the frame
  float car_x = -car_w;
  const int car_y = height / 2;

  std::println("\n--- Simulating Car at 30m (320x240, 25mph, 75deg FOV) ---");
  bool car_on_screen = false;

  for (int f = 0; f < 150; ++f) {
    std::fill(frame.begin(), frame.end(), 100);
    car_x += speed_px_f;
    int cur_x = static_cast<int>(car_x);

    // Draw car
    for (int y = car_y; y < car_y + car_h; ++y) {
      for (int x = cur_x; x < cur_x + car_w; ++x) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
          frame[y * width + x] = 200; // Car intensity
        }
      }
    }

    bs.Process(frame, motion_map);

    // Verify detection if car is fully on screen
    if (cur_x > 10 && cur_x + car_w < width - 10) {
      car_on_screen = true;
      // Sample a pixel in the middle of the car
      int mid_x = cur_x + car_w / 2;
      int mid_y = car_y + car_h / 2;
      EXPECT_EQ(motion_map[mid_y * width + mid_x], 255)
          << "Car NOT detected at frame " << f << " pos " << cur_x;
    }
  }
  EXPECT_TRUE(car_on_screen);
}

TEST(BackgroundSubtractorTest, SimulatesCarStopAndGo) {
  const int width = 320;
  const int height = 240;
  BackgroundSubtractor bs(width, height);

  // Constants for 75-degree FOV at 30m distance, 15mph speed, 30fps
  const float speed_px_f = 1.55f;
  const int car_w = 12;
  const int car_h = 10;
  const int car_y = height / 2;

  std::vector<uint8_t> frame(width * height, 100);
  std::vector<uint8_t> motion_map(width * height);

  // 1. Initial learning
  bs.Process(frame, motion_map);

  std::println("\n--- Simulating Car Stop-and-Go (15mph, 5s stop) ---");

  float car_x = -car_w;

  // Phase 1: Move for 60 frames (~2 seconds)
  // The car should be detected as it enters a "fresh" background.
  for (int f = 0; f < 60; ++f) {
    std::fill(frame.begin(), frame.end(), 100);
    car_x += speed_px_f;
    int cur_x = static_cast<int>(car_x);

    for (int y = car_y; y < car_y + car_h; ++y) {
      for (int x = cur_x; x < cur_x + car_w; ++x) {
        if (x >= 0 && x < width && y >= 0 && y < height)
          frame[y * width + x] = 200;
      }
    }

    bs.Process(frame, motion_map);

    if (cur_x > 10 && cur_x + car_w < width - 10) {
      // Check leading edge of the car (newly entered pixels)
      int edge_x = cur_x + car_w - 1;
      int mid_y = car_y + car_h / 2;
      EXPECT_EQ(motion_map[mid_y * width + edge_x], 255)
          << "Car leading edge NOT detected at frame " << f;
    }
  }

  // Phase 2: Stop for 150 frames (5 seconds)
  int stop_x = static_cast<int>(car_x);
  std::println("Car stopped at x = {}", stop_x);
  for (int f = 0; f < 150; ++f) {
    std::fill(frame.begin(), frame.end(), 100);
    for (int y = car_y; y < car_y + car_h; ++y) {
      for (int x = stop_x; x < stop_x + car_w; ++x) {
        frame[y * width + x] = 200;
      }
    }

    bs.Process(frame, motion_map);

    // After 5 seconds (150 frames) at alpha=0.02, it should be absorbed
    // (0.98^150 approx 0.05, so diff approx 5 which is < 15)
    if (f == 149) {
      int mid_x = stop_x + car_w / 2;
      int mid_y = car_y + car_h / 2;
      EXPECT_EQ(motion_map[mid_y * width + mid_x], 0)
          << "Car should be absorbed into background after 5s stop";
    }
  }

  // Phase 3: Resume moving
  std::println("Car resuming motion from x = {}", stop_x);
  for (int f = 0; f < 60; ++f) {
    std::fill(frame.begin(), frame.end(), 100);
    car_x += speed_px_f;
    int cur_x = static_cast<int>(car_x);

    for (int y = car_y; y < car_y + car_h; ++y) {
      for (int x = cur_x; x < cur_x + car_w; ++x) {
        if (x >= 0 && x < width)
          frame[y * width + x] = 200;
      }
    }

    bs.Process(frame, motion_map);

    // After several frames, the leading edge of the car should be in a fresh
    // background zone
    if (f > 20) {
      int edge_x = std::min(width - 1, cur_x + car_w - 1);
      int mid_y = car_y + car_h / 2;
      EXPECT_EQ(motion_map[mid_y * width + edge_x], 255)
          << "Car NOT detected after resuming at frame " << (210 + f);

      // The ghost (background mismatch) should be visible at the old position
      int ghost_x = stop_x + 1; // Left edge of where it was
      int ghost_y = car_y + car_h / 2;
      EXPECT_EQ(motion_map[ghost_y * width + ghost_x], 255)
          << "Ghost (background mismatch) NOT detected at old stop position at "
             "frame "
          << (210 + f);
    }
  }
}

TEST(BackgroundSubtractorTest, SimulatesHumanHeadOn) {
  const int width = 320;
  const int height = 240;
  BackgroundSubtractor bs(width, height);

  // Constants for 75-degree FOV, 320px width
  const float f_px = 160.0f / std::tan(37.5f * M_PI / 180.0f); // ~208.5
  const float human_w_m = 0.5f;
  const float human_h_m = 1.8f;
  const float speed_m_s = 1.4f;
  const float speed_m_f = speed_m_s / 30.0f;

  std::vector<uint8_t> frame(width * height, 100);
  std::vector<uint8_t> motion_map(width * height);

  // 1. Initial learning
  bs.Process(frame, motion_map);

  std::println("\n--- Simulating Human Walking Path (20m to 2m) ---");

  float z_m = 20.0f;
  bool detected_at_start = false;
  bool detected_at_end = false;

  for (int f = 0; f < 400; ++f) {
    std::fill(frame.begin(), frame.end(), 100);

    // Calculate projected size
    int w_px = static_cast<int>(f_px * human_w_m / z_m);
    int h_px = static_cast<int>(f_px * human_h_m / z_m);

    // Center the human
    int x0 = (width - w_px) / 2;
    int y0 = (height - h_px) / 2;

    for (int y = y0; y < y0 + h_px; ++y) {
      for (int x = x0; x < x0 + w_px; ++x) {
        if (x >= 0 && x < width && y >= 0 && y < height)
          frame[y * width + x] = 180; // Human color
      }
    }

    bs.Process(frame, motion_map);

    // Verify detection: look for motion anywhere within the human's projected
    // area. We check a few sample points, especially near the expanding edges
    // which shouldn't be absorbed as quickly as the dead center.
    bool motion_in_human = false;
    // Check center
    if (motion_map[(y0 + h_px / 2) * width + (x0 + w_px / 2)] == 255)
      motion_in_human = true;
    // Check top edge
    if (motion_map[y0 * width + (x0 + w_px / 2)] == 255)
      motion_in_human = true;
    // Check left edge
    if (motion_map[(y0 + h_px / 2) * width + x0] == 255)
      motion_in_human = true;

    if (motion_in_human) {
      if (z_m > 18.0f)
        detected_at_start = true;
      if (z_m < 5.0f)
        detected_at_end = true;
    }

    // Move closer
    z_m -= speed_m_f;
    if (z_m < 2.0f)
      break;
  }

  EXPECT_TRUE(detected_at_start) << "Human should be detected at 20m";
  EXPECT_TRUE(detected_at_end) << "Human should be detected at 5m";
}
