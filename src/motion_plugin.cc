#include "background_subtractor.h"
struct Options { virtual ~Options() {} };
#include "core/rpicam_app.hpp"
#include "post_processing_stages/post_processing_stage.hpp"
#include <memory>

class MotionPlugin : public PostProcessingStage {
public:
  MotionPlugin(RPiCamApp *app) : PostProcessingStage(app) {
    // Initialize with fixed dimensions; can be updated via Read()
    subtractor_ = std::make_unique<BackgroundSubtractor>(320, 240, 0.05f);
  }

  char const *Name() const override { return "motion_detect"; }

  void Read(boost::property_tree::ptree const &params) override {
    // Optional: read parameters from JSON
  }

  bool Process(CompletedRequestPtr &completed_request) override {
    // 1. Get image from completed_request
    // 2. Wrap in std::span
    // 3. subtractor_->Process(...)
    return false;
  }

private:
  std::unique_ptr<BackgroundSubtractor> subtractor_;
};

static PostProcessingStage *create_stage(RPiCamApp *app) {
  return new MotionPlugin(app);
}

static RegisterStage r("motion_detect", create_stage);
