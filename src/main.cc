#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>
#include <format>
#include <fstream>
#include <cstdio>
#include <array>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <expected>
#include <stop_token>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

struct ConversionTask {
    std::string src;
    std::string dest;
};

class CameraService {
public:
    CameraService() {
        fs::create_directories(temp_dir);
        fs::create_directories(final_dir);
        // jthread automatically joins on destruction
        worker = std::jthread([this](std::stop_token st) {
            process_conversions(st);
        });
    }

    std::expected<void, std::string> run() {
        std::string cmd = "rpicam-vid -t 0 --inline --nopreview --width 1280 --height 720 "
                          "--framerate 30 --lores-width 160 --lores-height 120 "
                          "--post-process-file motion_detect.json -o -";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::unexpected("Failed to open rpicam-vid pipe");

        std::cout << "Service started. Monitoring hardware ISP motion signals..." << std::endl;

        std::array<char, 64 * 1024> buffer;
        auto last_motion = std::chrono::steady_clock::now() - 1h;
        bool recording = false;
        std::ofstream out_file;
        std::string current_temp_path;

        // Monitor stderr for the "Motion detected" string from the RPi post-processor
        while (fgets(buffer.data(), buffer.size(), stderr)) {
            std::string line(buffer.data());
        std::cout << "Here" << std::endl;
            auto now = std::chrono::steady_clock::now();

            if (line.find("Motion detected") != std::string::npos) {
                last_motion = now;
                if (!recording) {
                    start_recording(recording, out_file, current_temp_path);
                }
            }

            if (recording) {
                // Logic: Keep recording until 5s of silence OR 5m of total time
                if (now - last_motion > 5s || now - start_time > 5min) {
                    stop_recording(recording, out_file, current_temp_path);
                }
            }
        }

        pclose(pipe);
        return {};
    }

private:
    const std::string temp_dir = "./temp_recordings";
    const std::string final_dir = "./videos";
    std::jthread worker;
    std::mutex mtx;
    std::condition_variable_any cv; // Use _any for stop_token compatibility
    std::queue<ConversionTask> tasks;
    std::chrono::steady_clock::time_point start_time;

    void start_recording(bool& recording, std::ofstream& file, std::string& path) {
        recording = true;
        start_time = std::chrono::steady_clock::now();
        auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        path = std::format("{}/{}.h264", temp_dir, ts);
        file.open(path, std::ios::binary);
        std::cout << std::format("Recording started: {}", path) << std::endl;
    }

    void stop_recording(bool& recording, std::ofstream& file, std::string& path) {
        file.close();
        recording = false;
        {
            std::lock_guard lock(mtx);
            std::string final_path = std::format("{}/{}.mp4", final_dir, fs::path(path).stem().string());
            tasks.push({path, final_path});
        }
        cv.notify_one();
    }

    void process_conversions(std::stop_token st) {
        while (!st.stop_requested()) {
            ConversionTask task;
            {
                std::unique_lock lock(mtx);
                // Wait until a task exists OR the service is shutting down
                cv.wait(lock, st, [this] { return !tasks.empty(); });

                if (st.stop_requested() && tasks.empty()) return;

                task = tasks.front();
                tasks.pop();
            }

            std::string ffmpeg_cmd = std::format("ffmpeg -y -i {} -c copy {} > /dev/null 2>&1", task.src, task.dest);
            if (std::system(ffmpeg_cmd.c_str()) == 0) {
                fs::remove(task.src);
                std::cout << std::format("Finalized conversion: {}", task.dest) << std::endl;
            }
        }
    }
};

int main() {
    CameraService service;
    auto result = service.run();
    if (!result) {
        std::cerr << std::format("Error: {}", result.error()) << std::endl;
        return 1;
    }
    return 0;
}

