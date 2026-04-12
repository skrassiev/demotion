#include <algorithm>
#include <cmath>
#include <print>

#include "background_subtractor.h"

// Process a new frame and return a binary motion map
// Using std::span for C++23 bounds-safe memory access
void BackgroundSubtractor::Process(std::span<const uint8_t> frame,
                                   std::vector<uint8_t> &motion_map) {
  motion_map.resize(width_ * height_);

  if (!initialized_) {
    for (size_t i = 0; i < frame.size(); ++i) {
      background_model_[i] = static_cast<float>(frame[i]);
    }
    std::fill(motion_map.begin(), motion_map.end(), 0);
    initialized_ = true;
    return;
  }

  for (size_t i = 0; i < frame.size(); ++i) {
    float pixel = static_cast<float>(frame[i]);

    // 1. Calculate absolute difference
    float diff = std::abs(pixel - background_model_[i]);

    // 2. Thresholding: Is this motion?
    motion_map[i] = (diff > 15.0f) ? 255 : 0;

    // 3. Update background (Learning)
    // Slowly update the model to ignore swaying branches and eventually stationary objects
    background_model_[i] =
        (1.0f - alpha_) * background_model_[i] + alpha_ * pixel;
  }
}