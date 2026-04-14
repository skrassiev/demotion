#include "background_subtractor.h"
#include <algorithm>
#include <cmath>

uint32_t BackgroundSubtractor::Process(std::span<const uint8_t> frame,
                                       std::vector<uint8_t> &motion_map) {
  motion_map.resize(width_ * height_);

  if (!initialized_) {
    for (size_t i = 0; i < frame.size(); ++i) {
      background_model_[i] = static_cast<float>(frame[i]);
    }
    initialized_ = true;
    return 0;
  }

  uint32_t regions = 0;
  for (size_t i = 0; i < frame.size(); ++i) {
    float pixel = static_cast<float>(frame[i]);
    float diff_sq = std::pow(pixel - background_model_[i], 2);

    // 1. Adaptive Thresholding
    // Use the pixel's variance to determine if a change is "motion"
    // vs "noise" (like swaying leaves).
    float std_dev = std::sqrt(variance_model_[i]);
    bool is_motion = diff_sq > std::pow(3.0f * std_dev, 2) &&
                     diff_sq > 225.0f; // > 3 sigma AND > 15 diff
    motion_map[i] = is_motion ? 255 : 0;

    // 2. Adaptive Learning Rate
    // Inversely proportional to variance: high variance = faster learning.
    float current_alpha =
        std::clamp(1.0f / (variance_model_[i] + 1.0f), min_alpha, max_alpha);

    // 3. Background and Variance Update
    // We update on every frame to ensure stationary objects are eventually
    // absorbed. The adaptive alpha naturally slows learning in noisy regions.
    background_model_[i] =
        (1.0f - current_alpha) * background_model_[i] + current_alpha * pixel;

    // Only update the variance model if NO motion is detected.
    // This keeps the threshold stable for valid moving objects and prevents
    // them from being learned as noise.
    if (!is_motion) {
      variance_model_[i] =
          (1.0f - current_alpha) * variance_model_[i] + current_alpha * diff_sq;
    } else {
      regions++;
    }
  }
  return regions;
}
