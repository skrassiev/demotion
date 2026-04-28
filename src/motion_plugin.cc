#include "background_subtractor.h"
struct Options {
  virtual ~Options() {}
};
#include "core/rpicam_app.hpp"
#include "post_processing_stages/post_processing_stage.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <fstream>
#include <libcamera/stream.h>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

using Stream = libcamera::Stream;
namespace fs = std::filesystem;
void print_ptree(const boost::property_tree::ptree &pt) {
  // This writes the tree to std::cout in a pretty-printed JSON format
  boost::property_tree::write_json(std::cout, pt);
}

class MotionPlugin : public PostProcessingStage {
public:
  static constexpr std::string_view stage_name_ = "mog2_detect";

  MotionPlugin(RPiCamApp *app) : PostProcessingStage(app) {
    // Initialize with fixed dimensions; can be updated via Read()
    subtractor_ = std::make_unique<BackgroundSubtractor>(320, 240);
    LOG(1, "MOG2 Motion::New");
  }

  char const *Name() const override { return stage_name_.data(); }

  virtual void Read(boost::property_tree::ptree const &params) override {
    // Optional: read parameters from JSON
    LOG(1, "MOG2 Motion::Read");
    print_ptree(params);

    config_.frame_period = params.get<int>("frame_period", 5);
    config_.log_level = params.get<int>("log_level", 1);
    config_.motion_threshold = params.get<int>("motion_threshold", 0);
    config_.skip_initial_frames = params.get<int>("skip_initial_frames", 0);
    config_.capture_frames_to_file =
        params.get<std::string>("capture_frames_to_file", std::string{});
    config_.freeze_frames_dir =
        params.get<std::string>("freeze_frames_dir", std::string{});
    if (!config_.freeze_frames_dir.empty()) {
      fs::create_directories(config_.freeze_frames_dir);
    }
  }

  virtual bool Process(CompletedRequestPtr &completed_request) override {
    // 1. Get image from completed_request
    // 2. Wrap in std::span
    // 3. subtractor_->Process(...)	if (!stream_)
    if (config_.frame_period &&
        completed_request->sequence % config_.frame_period)
      return false;

    if (completed_request->sequence < config_.skip_initial_frames)
      return false;

    BufferReadSync r(app_, completed_request->buffers[stream_]);
    libcamera::Span<uint8_t> buffer = r.Get()[0];

    // We need to protect access to first_time_, previous_frame_ and
    // motion_detected_.
    std::lock_guard<std::mutex> lock(mutex_);

    auto regions =
        subtractor_->Process(buffer.subspan(0, frame_size_), motion_map_);
    bool motion_detected = regions > config_.motion_threshold;
    LOG(1, "Modified regions: " << regions);

    completed_request->post_process_metadata.Set("motion_detector.result",
                                                 regions);
    if (!config_.capture_frames_to_file.empty()) {
      if (!debug_file_.is_open()) {
        debug_file_.open("motion_debug.bin", std::ios::binary);
      }
      if (debug_file_.is_open()) {
        debug_file_.write(reinterpret_cast<const char *>(buffer.data()),
                          buffer.size());
        debug_file_.flush();
      }
    }

    if (motion_detected != motion_detected_) {
      if (config_.log_level >= 1) {
        LOG(1,
            "Motion " << (motion_detected ? "detected ++++" : "stopped ----"));
      }
      if (!config_.freeze_frames_dir.empty()) {
        std::ofstream frame_trigger_file;
        frame_trigger_file.open(
            config_.freeze_frames_dir +
                std::format("/{}_{}.yuv", completed_request->sequence,
                            motion_detected ? "start" : "stop"),
            std::ios::binary);
        if (!frame_trigger_file.is_open()) {
          LOG(1, "Failed to open frame trigger file");
        } else {
          frame_trigger_file.write(
              reinterpret_cast<const char *>(buffer.data()), buffer.size());
        }
      }
    }
    motion_detected_ = motion_detected;

    return motion_detected_;
  }

  virtual void Configure() override {
    LOG(1, "MOG2 Motion::Configure");
    StreamInfo info;
    stream_ = app_->LoresStream(&info);
    if (!stream_)
      return;
    LOG(1, "MOG2 Motion::Configure: " << info.width << " " << info.height);
    subtractor_ =
        std::make_unique<BackgroundSubtractor>(info.width, info.height);
    frame_size_ = info.width * info.height;
    motion_map_.resize(frame_size_);
  }

private:
  std::unique_ptr<BackgroundSubtractor> subtractor_;

  struct Config {
    uint16_t motion_threshold;
    int frame_period;
    uint8_t log_level;
    uint8_t skip_initial_frames;
    std::string capture_frames_to_file;
    std::string freeze_frames_dir;
  } config_;
  Stream *stream_ = nullptr;
  std::mutex mutex_;
  bool motion_detected_ = false;
  std::vector<uint8_t> motion_map_;
  uint16_t frame_size_ = uint16_t(320 * 240);
  std::ofstream debug_file_;
};

static PostProcessingStage *create_stage(RPiCamApp *app) {
  return new MotionPlugin(app);
}

static RegisterStage r(MotionPlugin::stage_name_.data(), create_stage);
