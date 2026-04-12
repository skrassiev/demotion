#include <algorithm>
#include <cstdint>
#include <vector>

#include <span>

class BackgroundSubtractor {
public:
  // width/height of your lo-res stream
  BackgroundSubtractor(int w, int h, float learning_rate = 0.01f)
      : width_(w), height_(h), alpha_(learning_rate) {
    background_model_.resize(width_ * height_, 0.0f);
  }

  // Process a new frame and return a binary motion map
  // Using std::span for C++23 bounds-safe memory access
  void Process(std::span<const uint8_t> frame,
               std::vector<uint8_t> &motion_map);

  template <typename F>
  void PrintBackground(F print_grid) {
    std::for_each(background_model_.begin(), background_model_.end(), print_grid);
  }

private:
  int width_, height_;
  float alpha_;
  bool initialized_ = false;
  std::vector<float> background_model_;
};
