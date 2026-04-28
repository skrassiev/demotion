#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"

namespace fs = std::filesystem;

ABSL_DECLARE_FLAG(std::string, temp_dir);
ABSL_DECLARE_FLAG(std::string, final_dir);
ABSL_DECLARE_FLAG(std::string, motion_detect_file);
ABSL_DECLARE_FLAG(bool, log_timestamps);
ABSL_DECLARE_FLAG(double, min_motion_duration);
ABSL_DECLARE_FLAG(std::string, post_process_libs);

struct ConversionTask {
  std::string src_;
  std::string dest_;
};

class CameraService {
public:
  CameraService(std::string temp_dir, std::string final_dir,
                std::string motion_detect_file,
                double min_motion_duration = 0.0,
                std::string post_process_libs = "");
  ~CameraService();

  std::expected<void, std::string> run();

  // Internal methods exposed for testing if needed
  void start_recording(bool &recording, std::ofstream &file, std::string &path);
  void stop_recording(bool &recording, std::ofstream &file, std::string &path);
  void process_conversions();

private:
  const std::string temp_dir_;
  const std::string final_dir_;
  const std::string motion_detect_file_;
  const double min_motion_duration_;
  const std::string post_process_libs_;
  std::thread worker_;
  bool stop_requested_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::queue<ConversionTask> tasks_;
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::system_clock::time_point wall_start_time_;
  std::vector<uint8_t> cached_sps_pps_;
  bool has_sps_pps_ = false;
};

void Log(std::string_view msg, bool is_error = false);
