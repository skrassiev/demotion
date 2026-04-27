#include "background_subtractor.h"
struct Options {
  virtual ~Options() {}
};
#include "core/rpicam_app.hpp"
#include "post_processing_stages/post_processing_stage.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <libcamera/stream.h>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

using Stream = libcamera::Stream;

void print_ptree(const boost::property_tree::ptree &pt) {
  // This writes the tree to std::cout in a pretty-printed JSON format
  boost::property_tree::write_json(std::cout, pt);
}

class MotionPlugin : public PostProcessingStage {
public:
  static constexpr std::string_view stage_name_ = "mog2_detect";

  MotionPlugin(RPiCamApp *app) : PostProcessingStage(app) {
    // Initialize with fixed dimensions; can be updated via Read()
    subtractor_ = std::make_unique<BackgroundSubtractor>(320, 240, 0.05f);
    LOG(1, "MOG2 Motion::New");
  }

  char const *Name() const override { return stage_name_.data(); }

  virtual void Read(boost::property_tree::ptree const &params) override {
    // Optional: read parameters from JSON
    LOG(1, "MOG2 Motion::Read params.vebose=" << params.get<bool>("verbose",
                                                                  false));
    print_ptree(params);
    config_.frame_period = params.get<int>("frame_period", 5);
    config_.verbose = params.get<bool>("verbose", false);
    LOG(1, "MOG2 Motion::Read verbose=" << config_.verbose);
  }

  virtual bool Process(CompletedRequestPtr &completed_request) override {
    // 1. Get image from completed_request
    // 2. Wrap in std::span
    // 3. subtractor_->Process(...)	if (!stream_)
    // LOG(1, "MOG2 Motion::Process");
    // LOG(1, "Framerate: " << completed_request->framerate);
    // LOG(1, "Buffers: " << completed_request->buffers.size());
    // LOG(1, "Libcamera Metadata: " << completed_request->metadata.size()
    //                               << " entries");
    if (config_.frame_period &&
        completed_request->sequence % config_.frame_period)
      return false;

    BufferReadSync r(app_, completed_request->buffers[stream_]);
    libcamera::Span<uint8_t> buffer = r.Get()[0];

    // We need to protect access to first_time_, previous_frame_ and
    // motion_detected_.
    std::lock_guard<std::mutex> lock(mutex_);

    auto regions =
        subtractor_->Process(buffer.subspan(0, frame_size_), motion_map_);
    bool motion_detected = regions > 0;

    completed_request->post_process_metadata.Set("motion_detector.result",
                                                 regions);
    if (!debug_file_.is_open()) {
      debug_file_.open("motion_debug.bin", std::ios::binary);
    }
    if (debug_file_.is_open()) {
      debug_file_.write(reinterpret_cast<const char *>(buffer.data()),
                        buffer.size());
      debug_file_.flush();
    }

    if (config_.verbose && motion_detected != motion_detected_) {
      LOG(1, "Motion " << (motion_detected ? "detected" : "stopped"));
    }
    motion_detected_ = motion_detected;

    return regions > 0;
  }

  virtual void Configure() override {
    LOG(1, "MOG2 Motion::Configure");
    StreamInfo info;
    stream_ = app_->LoresStream(&info);
    if (!stream_)
      return;
    LOG(1, "MOG2 Motion::Configure: " << info.width << " " << info.height);
    subtractor_ =
        std::make_unique<BackgroundSubtractor>(info.width, info.height, 0.05f);
    frame_size_ = info.width * info.height;
    motion_map_.resize(frame_size_);
  }

private:
  std::unique_ptr<BackgroundSubtractor> subtractor_;

  struct Config {
    float roi_x, roi_y;
    float roi_width, roi_height;
    int hskip, vskip;
    float difference_m;
    int difference_c;
    float region_threshold;
    int frame_period;
    bool verbose;
    std::string region_name;
  } config_;
  Stream *stream_ = nullptr;
  std::mutex mutex_;
  bool motion_detected_ = false;
  std::vector<uint8_t> motion_map_;
  uint16_t frame_size_;
  std::ofstream debug_file_;
};

static PostProcessingStage *create_stage(RPiCamApp *app) {
  return new MotionPlugin(app);
}

static RegisterStage r(MotionPlugin::stage_name_.data(), create_stage);
