#include "src/camera_service.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include "absl/cleanup/cleanup.h"

ABSL_FLAG(std::string, temp_dir, "./temp_recordings", "Directory for temporary recordings");
ABSL_FLAG(std::string, final_dir, "/street", "Directory for finalized MP4 videos");
ABSL_FLAG(std::string, motion_detect_file, "motion_detect.json", "Path to motion_detect.json post-process file");
ABSL_FLAG(bool, log_timestamps, true, "Include timestamps in log messages");

using namespace std::chrono_literals;

void Log(std::string_view msg, bool is_error) {
    std::ostream& os = is_error ? std::cerr : std::cout;
    if (absl::GetFlag(FLAGS_log_timestamps)) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
        std::tm local_tm;
        localtime_r(&t, &local_tm);
        char time_buf[64];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);
        os << "[" << time_buf << "." << std::setfill('0') << std::setw(6) << us.count() << "] ";
    }
    os << msg;
    if (msg.empty() || msg.back() != '\n') {
        os << std::endl;
    }
}

CameraService::CameraService(std::string temp_dir, std::string final_dir, std::string motion_detect_file) 
    : temp_dir_(std::move(temp_dir)), 
      final_dir_(std::move(final_dir)),
      motion_detect_file_(std::move(motion_detect_file)),
      stop_requested_(false) {
    fs::create_directories(temp_dir_);
    fs::create_directories(final_dir_);
    worker_ = std::thread([this]() {
        process_conversions();
    });
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
        "--bitrate=1200000 "
        "--lens-position=0.04 --autofocus-mode=manual "
        "--post-process-file={} -o - 2>&1", 
        motion_detect_file_);

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return std::unexpected("Failed to open rpicam-vid pipe");
    absl::Cleanup closer = [pipe] { pclose(pipe); };

    Log("Service started. Monitoring hardware ISP motion signals...");

    std::array<char, 64 * 1024> buffer;
    auto last_motion = std::chrono::steady_clock::now() - 1h;
    bool recording = false;
    std::ofstream out_file;
    std::string current_temp_path;

    while (true) {
        size_t bytes_read = fread(buffer.data(), 1, buffer.size(), pipe);
        if (bytes_read == 0) break;

        std::string_view chunk(buffer.data(), bytes_read);
        auto now = std::chrono::steady_clock::now();

        if (chunk.find("Motion detected") != std::string::npos) {
            Log("--- Motion detected signal received ---");
            last_motion = now;
            if (!recording) {
                start_recording(recording, out_file, current_temp_path);
            }
        }

        if (recording) {
            if (out_file.is_open()) {
                out_file.write(buffer.data(), bytes_read);
            }

            if (now - last_motion > 2s || now - start_time_ > 5min) {
                stop_recording(recording, out_file, current_temp_path);
            }
        }
    }

    return {};
}

void CameraService::start_recording(bool& recording, std::ofstream& file, std::string& path) {
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
}

void CameraService::stop_recording(bool& recording, std::ofstream& file, std::string& path) {
    if (file.is_open()) {
        file.close();
    }
    recording = false;
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

            if (stop_requested_ && tasks_.empty()) return;

            task = tasks_.front();
            tasks_.pop();
        }

        std::string ffmpeg_cmd = std::format("ffmpeg -y -i {} -c copy {} > /dev/null 2>&1", task.src_, task.dest_);
        int status = std::system(ffmpeg_cmd.c_str());
        
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) {
                Log(std::format("Finalized conversion: {}\n", task.dest_));
            } else {
                Log(std::format("FFmpeg exited with error code {} for task: {}\n", exit_code, task.src_), true);
            }
        } else if (WIFSIGNALED(status)) {
            Log(std::format("FFmpeg terminated by signal {} for task: {}\n", WTERMSIG(status), task.src_), true);
        } else {
            Log(std::format("FFmpeg failed with status {} for task: {}\n", status, task.src_), true);
        }
        
        std::error_code ec;
        if (fs::remove(task.src_, ec)) {
            // File removed
        } else if (ec) {
            Log(std::format("Failed to remove temporary file {}: {}\n", task.src_, ec.message()), true);
        }
    }
}
