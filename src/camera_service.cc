#include "camera_service.h"
#include "absl/cleanup/cleanup.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>

ABSL_FLAG(std::string, temp_dir, "./temp_recordings",
          "Directory for temporary recordings");
ABSL_FLAG(std::string, final_dir, "/street",
          "Directory for finalized MP4 videos");
ABSL_FLAG(std::string, motion_detect_file, "motion_detect.json",
          "Path to motion_detect.json post-process file");
ABSL_FLAG(bool, log_timestamps, true, "Include timestamps in log messages");
ABSL_FLAG(double, min_motion_duration, 1.0,
          "Minimum duration of motion to record (seconds). Shorter motion will "
          "not be saved.");
// Added a flag for the post-motion cooldown period
ABSL_FLAG(double, post_motion_delay, 1.0,
          "Seconds to continue recording after 'Motion stopped' is received");

using namespace std::chrono_literals;

void Log(std::string_view msg, bool is_error) {
  std::ostream &os = is_error ? std::cerr : std::cout;
  if (absl::GetFlag(FLAGS_log_timestamps)) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  now.time_since_epoch()) %
              1000000;
    std::tm local_tm;
    localtime_r(&t, &local_tm);
    char time_buf[64];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);
    os << "[" << time_buf << "." << std::setfill('0') << std::setw(6)
       << us.count() << "] ";
  }
  os << msg;
  if (msg.empty() || msg.back() != '\n') {
    os << std::endl;
  }
}

CameraService::CameraService(std::string temp_dir, std::string final_dir,
                             std::string motion_detect_file,
                             double min_motion_duration)
    : temp_dir_(std::move(temp_dir)), final_dir_(std::move(final_dir)),
      motion_detect_file_(std::move(motion_detect_file)),
      min_motion_duration_(min_motion_duration), stop_requested_(false) {
  fs::create_directories(temp_dir_);
  fs::create_directories(final_dir_);
  worker_ = std::thread([this]() { process_conversions(); });
}

CameraService::~CameraService() {
  {
    std::lock_guard lock(mtx_);
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::expected<void, std::string> CameraService::run() {
  std::string cmd = std::format(
      "rpicam-vid -t 0 --inline --nopreview --width=1280 --height=720 "
      "--framerate=30 --lores-width=160 --lores-height=120 "
      "--bitrate=400000 "
      "--lens-position=0.04 --autofocus-mode=manual "
      "--post-process-file={} -o - 2>&1",
      motion_detect_file_);

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe)
    return std::unexpected("Failed to open rpicam-vid pipe");
  absl::Cleanup closer = [pipe] { pclose(pipe); };

  Log("Service started. Monitoring hardware ISP motion signals...");

  std::array<char, 64 * 1024> buffer;
  bool motion_active = false;
  bool recording = false;
  std::ofstream out_file;
  std::string current_temp_path;

  auto stop_time_deadline = std::chrono::steady_clock::now();
  const auto post_delay = std::chrono::milliseconds(
      static_cast<int64_t>(absl::GetFlag(FLAGS_post_motion_delay) * 1000));

  while (true) {
    size_t bytes_read = fread(buffer.data(), 1, buffer.size(), pipe);
    if (bytes_read == 0)
      break;

    std::string_view chunk(buffer.data(), bytes_read);
    auto now = std::chrono::steady_clock::now();

    // 1. Process Signal Events
    if (chunk.find("Motion detected") != std::string::npos) {
      Log("--- Motion detected ---");
      motion_active = true;
      if (!recording) {
        start_recording(recording, out_file, current_temp_path);
      }
    }

    if (chunk.find("Motion stopped") != std::string::npos) {
      Log("--- Motion stopped ---");
      motion_active = false;
      // Set the deadline for when to stop recording
      stop_time_deadline = now + post_delay;
    }

    // 1.5 Scan for SPS/PPS if not yet found
    if (!has_sps_pps_) {
      for (size_t i = 0; i + 5 < bytes_read; ++i) {
        if (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 0 &&
            buffer[i + 3] == 1) {
          uint8_t nalu_type = buffer[i + 4] & 0x1F;
          if (nalu_type == 7) { // SPS found
            // Look for PPS (type 8) nearby, or just capture a chunk
            // For simplicity, we'll capture until the next IDR or just a fixed
            // size
            // Realistically, SPS/PPS are small. We'll look for the next NAL
            // after PPS.
            Log("Found SPS/PPS header. Analyzing...");
            uint8_t profile_idc = buffer[i + 5];
            uint8_t level_idc = buffer[i + 7];
            std::string profile_name = "Unknown";
            if (profile_idc == 66)
              profile_name = "Baseline";
            else if (profile_idc == 77)
              profile_name = "Main";
            else if (profile_idc == 100)
              profile_name = "High";

            Log(std::format("H.264 Stream Info: Profile {} ({}), Level {:.1f}",
                           profile_idc, profile_name, level_idc / 10.0));

            // Capture SPS + PPS. Usually they are consecutive.
            // We'll search for the next NAL after SPS that isn't PPS, or just
            // grab both.
            size_t start = i;
            size_t end = i + 5;
            bool found_pps = false;
            while (end + 4 < bytes_read) {
              if (buffer[end] == 0 && buffer[end + 1] == 0 &&
                  buffer[end + 2] == 0 && buffer[end + 3] == 1) {
                uint8_t next_type = buffer[end + 4] & 0x1F;
                if (next_type == 8) {
                  found_pps = true;
                } else if (found_pps && next_type != 8) {
                  break; // Found something else after PPS
                }
              }
              end++;
            }
            cached_sps_pps_.assign(buffer.begin() + start,
                                   buffer.begin() + end);
            has_sps_pps_ = true;
            Log(std::format("Cached {} bytes of SPS/PPS header",
                           cached_sps_pps_.size()));
            break;
          }
        }
      }
    }

    if (recording) {
      if (out_file.is_open()) {
        out_file.write(buffer.data(), bytes_read);
      }

      // 3. Termination Logic
      // Stop if motion is inactive AND we have passed the delay deadline
      // OR if we hit the 5-minute safety limit
      bool delay_expired = (!motion_active && now >= stop_time_deadline);
      bool safety_limit = (now - start_time_ > 5min);

      if (delay_expired || safety_limit) {
        stop_recording(recording, out_file, current_temp_path);
      }
    }
  }

  return {};
}

void CameraService::start_recording(bool &recording, std::ofstream &file,
                                    std::string &path) {
  recording = true;
  wall_start_time_ = std::chrono::system_clock::now();
  start_time_ = std::chrono::steady_clock::now();
  auto ts = std::chrono::system_clock::to_time_t(wall_start_time_);
  path = std::format("{}/{}.h264", temp_dir_, ts);
  file.open(path, std::ios::binary);
  if (!file.is_open()) {
    Log(std::format("ERROR: Failed to open output file: {}\n", path), true);
    recording = false;
    return;
  }
  Log(std::format("Recording started: {}\n", path));

  if (has_sps_pps_) {
    file.write(reinterpret_cast<const char *>(cached_sps_pps_.data()),
               cached_sps_pps_.size());
    Log("Prepended cached SPS/PPS header to recording");
  }
}

void CameraService::stop_recording(bool &recording, std::ofstream &file,
                                   std::string &path) {
  if (file.is_open()) {
    file.close();
  }
  recording = false;

  auto now = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_)
          .count() /
      1000.0;
  if (duration < min_motion_duration_) {
    Log(std::format("Motion too short ({:.2f}s < {:.2f}s), discarding: {}\n",
                    duration, min_motion_duration_, path));
    std::error_code ec;
    fs::remove(path, ec);
    return;
  }

  {
    std::lock_guard lock(mtx_);
    auto t_start = std::chrono::system_clock::to_time_t(wall_start_time_);
    std::tm local_tm;
    localtime_r(&t_start, &local_tm);

    char date_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local_tm);

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H-%M-%S", &local_tm);

    fs::path daily_dir = fs::path(final_dir_) / date_buf;
    fs::create_directories(daily_dir);

    std::string final_filename = std::string(time_buf) + ".mp4";
    std::string final_path = (daily_dir / final_filename).string();
    tasks_.push({path, final_path});
  }
  cv_.notify_one();
}

void CameraService::process_conversions() {
  while (true) {
    ConversionTask task;
    {
      std::unique_lock lock(mtx_);
      cv_.wait(lock, [this] { return !tasks_.empty() || stop_requested_; });

      if (stop_requested_ && tasks_.empty())
        return;

      task = tasks_.front();
      tasks_.pop();
    }

    std::string ffmpeg_cmd = std::format(
        "ffmpeg -y -i {} -c copy {} > /dev/null 2>&1", task.src_, task.dest_);
    int status = std::system(ffmpeg_cmd.c_str());
    bool ffmpeg_success = false;

    if (WIFEXITED(status)) {
      int exit_code = WEXITSTATUS(status);
      if (exit_code == 0) {
        Log(std::format("Finalized conversion: {}\n", task.dest_));
        ffmpeg_success = true;
      } else {
        Log(std::format("FFmpeg exited with error code {} for task: {}\n",
                        exit_code, task.src_),
            true);
      }
    } else if (WIFSIGNALED(status)) {
      Log(std::format("FFmpeg terminated by signal {} for task: {}\n",
                      WTERMSIG(status), task.src_),
          true);
    } else {
      Log(std::format("FFmpeg failed with status {} for task: {}\n", status,
                      task.src_),
          true);
    }

    std::error_code ec;
    if (ffmpeg_success) {
      if (fs::remove(task.src_, ec)) {
        // File removed
      } else if (ec) {
        Log(std::format("Failed to remove temporary file {}: {}\n", task.src_,
                        ec.message()),
            true);
      }
    }
  }
}
