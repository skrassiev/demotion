#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <expected>
#include <format>
#include <iostream>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"

namespace fs = std::filesystem;

ABSL_DECLARE_FLAG(std::string, temp_dir);
ABSL_DECLARE_FLAG(std::string, final_dir);
ABSL_DECLARE_FLAG(std::string, motion_detect_file);
ABSL_DECLARE_FLAG(bool, log_timestamps);

struct ConversionTask {
    std::string src_;
    std::string dest_;
};

class CameraService {
public:
    CameraService(std::string temp_dir, std::string final_dir, std::string motion_detect_file);
    ~CameraService();

    std::expected<void, std::string> run();

    // Internal methods exposed for testing if needed
    void start_recording(bool& recording, std::ofstream& file, std::string& path);
    void stop_recording(bool& recording, std::ofstream& file, std::string& path);
    void process_conversions();

private:
    const std::string temp_dir_;
    const std::string final_dir_;
    const std::string motion_detect_file_;
    std::thread worker_;
    bool stop_requested_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<ConversionTask> tasks_;
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::system_clock::time_point wall_start_time_;
};

void Log(std::string_view msg, bool is_error = false);
