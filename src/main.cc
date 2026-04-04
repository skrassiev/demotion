#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <array>
#include <queue>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <format>
#include <expected>
#include <execinfo.h>
#include <csignal>
#include <cstdlib>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/cleanup/cleanup.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

ABSL_FLAG(std::string, temp_dir, "./temp_recordings", "Directory for temporary recordings");
ABSL_FLAG(std::string, final_dir, "./videos", "Directory for finalized MP4 videos");

[[gnu::noinline]] void print_stack_dump() {
    void* array[50];
    int size = backtrace(array, 50);
    char** symbols = backtrace_symbols(array, size);

    if (symbols == nullptr) {
        std::cerr << "Failed to generate stack symbols." << std::endl;
        return;
    }

    std::cerr << "\nStack crash dump:" << std::endl;
    for (int i = 0; i < size; ++i) {
        std::string symbol = symbols[i];

        size_t start = symbol.find('[');
        size_t end = symbol.find(']');
        if (start != std::string::npos && end != std::string::npos && end > start) {
            std::string addr = symbol.substr(start + 1, end - start - 1);
            std::string cmd = std::format("addr2line -e /proc/self/exe -p -C {} 2>/dev/null", addr);
            FILE* fp = popen(cmd.c_str(), "r");
            if (fp) {
                absl::Cleanup closer = [fp] { pclose(fp); };
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), fp)) {
                    std::string line(buffer);
                    if (line.find("main.cc") != std::string::npos) {
                        std::cerr << std::format("#{} {}", i, line);
                    } else {
                        size_t pos = line.find(" at ");
                        if (pos != std::string::npos) {
                            std::cerr << std::format("#{} {}\n", i, line.substr(0, pos));
                        } else {
                            std::cerr << std::format("#{} {}", i, line);
                        }
                    }
                } else {
                    std::cerr << std::format("#{} {}\n", i, symbol);
                }
            } else {
                std::cerr << std::format("#{} {}\n", i, symbol);
            }
        } else {
            std::cerr << std::format("#{} {}\n", i, symbol);
        }
    }
    free(symbols);
}

void signal_handler(int sig) {
    std::cerr << std::format("\nCaught fatal signal: {}\n", sig);
    print_stack_dump();
    std::exit(sig);
}

void setup_crash_handler() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    std::set_terminate([]() {
        std::cerr << "Uncaught runtime exception!" << std::endl;
        print_stack_dump();
        std::exit(1);
    });
}

struct ConversionTask {
    std::string src_;
    std::string dest_;
};

class CameraService {
public:
    CameraService(std::string temp_dir, std::string final_dir) 
        : temp_dir_(std::move(temp_dir)), 
          final_dir_(std::move(final_dir)),
          stop_requested_(false) {
        fs::create_directories(temp_dir_);
        fs::create_directories(final_dir_);
        worker_ = std::thread([this]() {
            process_conversions();
        });
    }

    ~CameraService() {
        {
            std::lock_guard lock(mtx_);
            stop_requested_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::expected<void, std::string> run() {
        std::string cmd = "rpicam-vid -t 0 --inline --nopreview --width 1280 --height 720 "
                          "--framerate 30 --lores-width 160 --lores-height 120 "
                          "--post-process-file motion_detect.json -o - 2>&1";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::unexpected("Failed to open rpicam-vid pipe");
        absl::Cleanup closer = [pipe] { pclose(pipe); };

        std::cout << "Service started. Monitoring hardware ISP motion signals..." << std::endl;

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
                std::cout << "--- Motion detected signal received ---" << std::endl;
                last_motion = now;
                if (!recording) {
                    start_recording(recording, out_file, current_temp_path);
                }
            }

            if (recording) {
                out_file.write(buffer.data(), bytes_read);

                if (now - last_motion > 5s || now - start_time_ > 5min) {
                    stop_recording(recording, out_file, current_temp_path);
                }
            }
        }

        return {};
    }

private:
    const std::string temp_dir_;
    const std::string final_dir_;
    std::thread worker_;
    bool stop_requested_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<ConversionTask> tasks_;
    std::chrono::steady_clock::time_point start_time_;

    void start_recording(bool& recording, std::ofstream& file, std::string& path) {
        recording = true;
        start_time_ = std::chrono::steady_clock::now();
        auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        path = std::format("{}/{}.h264", temp_dir_, ts);
        file.open(path, std::ios::binary);
        std::cout << std::format("Recording started: {}\n", path);
    }

    void stop_recording(bool& recording, std::ofstream& file, std::string& path) {
        file.close();
        recording = false;
        {
            std::lock_guard lock(mtx_);
            std::string final_path = std::format("{}/{}.mp4", final_dir_, fs::path(path).stem().string());
            tasks_.push({path, final_path});
        }
        cv_.notify_one();
    }

    void process_conversions() {
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
            if (std::system(ffmpeg_cmd.c_str()) == 0) {
                fs::remove(task.src_);
                std::cout << std::format("Finalized conversion: {}\n", task.dest_);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    setup_crash_handler();
    absl::SetProgramUsageMessage("Camera monitoring and recording service. Monitors hardware ISP motion signals and saves recorded video to MP4.");
    absl::ParseCommandLine(argc, argv);

    CameraService service(absl::GetFlag(FLAGS_temp_dir), absl::GetFlag(FLAGS_final_dir));
    auto result = service.run();
    if (!result) {
        std::cerr << std::format("\nRuntime Error: {}\n", result.error());
        print_stack_dump();
        std::exit(1);
    }
    return 0;
}
