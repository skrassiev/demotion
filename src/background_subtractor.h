#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class BackgroundSubtractor {
public:
  BackgroundSubtractor(int w, int h, float alpha = 0.05f)
      : width_(w), height_(h) {
    background_model_.resize(width_ * height_, 0.0f);
    // Initialize variance high so the model learns the initial scene quickly
    variance_model_.resize(width_ * height_, 100.0f);
  }

  uint32_t Process(const uint8_t *frame, size_t size,
                   std::vector<uint8_t> &motion_map) {
    return Process(std::span<const uint8_t>(frame, size), motion_map);
  }

  uint32_t Process(std::span<const uint8_t> frame,
                   std::vector<uint8_t> &motion_map);

  template <typename F> void PrintBackground(F print_grid) {
    std::for_each(background_model_.begin(), background_model_.end(),
                  print_grid);
  }

private:
  int width_, height_;
  bool initialized_ = false;
  std::vector<float> background_model_;
  std::vector<float> variance_model_;

  // Hyperparameters for the adaptive logic
  const float min_alpha = 0.002f; // Slowest learning (for stable background)
  const float max_alpha = 0.1f;   // Fastest learning (for swaying leaves)
};
