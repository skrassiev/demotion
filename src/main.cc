#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <array>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <format>
#include <expected>
#include <execinfo.h>
#include <csignal>
#include <cstdlib>

#include "absl/cleanup/cleanup.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

ABSL_FLAG(std::string, temp_dir, "./temp_recordings", "Directory for temporary recordings");
ABSL_FLAG(std::string, final_dir, "./videos", "Directory for finalized MP4 videos");
ABSL_FLAG(std::string, motion_detect_file, "motion_detect.json", "Path to motion_detect.json post-process file");

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
    CameraService(std::string temp_dir, std::string final_dir, std::string motion_detect_file) 
        : temp_dir_(std::move(temp_dir)), 
          final_dir_(std::move(final_dir)),
          motion_detect_file_(std::move(motion_detect_file)),
          stop_requested_(false) {
        std::cout << std::format("[CameraService] Initializing. temp_dir: {}, final_dir: {}, motion_detect_file: {}\n", 
                                 temp_dir_, final_dir_, motion_detect_file_);
        fs::create_directories(temp_dir_);
        fs::create_directories(final_dir_);
        worker_ = std::thread([this]() {
            std::cout << "[Worker] Thread started.\n";
            process_conversions();
            std::cout << "[Worker] Thread exiting.\n";
        });
    }

    ~CameraService() {
        std::cout << "[CameraService] Destructor called. Requesting stop...\n";
        {
            std::lock_guard lock(mtx_);
            stop_requested_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        std::cout << "[CameraService] Destructor finished.\n";
    }

    std::expected<void, std::string> run() {
        std::string cmd = std::format(
            "rpicam-vid -t 0 --inline --nopreview --width 1280 --height 720 "
            "--framerate 30 --lores-width 160 --lores-height 120 "
            "--post-process-file {} -o - 2>&1", 
            motion_detect_file_);

        std::cout << std::format("[run] Executing command: {}\n", cmd);
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::unexpected("Failed to open rpicam-vid pipe");
        absl::Cleanup closer = [pipe] { 
            int status = pclose(pipe);
            std::cout << std::format("[run] Pipe closed with status: {}\n", status);
        };

        std::cout << "[run] Service started. Monitoring hardware ISP motion signals...\n";

        std::array<char, 64 * 1024> buffer;
        auto last_motion = std::chrono::steady_clock::now() - 1h;
        bool recording = false;
        std::ofstream out_file;
        std::string current_temp_path;

        while (true) {
            size_t bytes_read = fread(buffer.data(), 1, buffer.size(), pipe);
            if (bytes_read == 0) {
                std::cout << "[run] fread returned 0 bytes. Pipe likely closed.\n";
                break;
            }

            std::string_view chunk(buffer.data(), bytes_read);
            auto now = std::chrono::steady_clock::now();

            if (chunk.find("Motion detected") != std::string::npos) {
                std::cout << "[run] --- Motion detected signal received ---\n";
                last_motion = now;
                if (!recording) {
                    start_recording(recording, out_file, current_temp_path);
                }
            } else if (chunk.find("Error") != std::string::npos || chunk.find("exception") != std::string::npos) {
                std::cout << "[run] Potential error detected in pipe output: " << chunk << "\n";
            }

            if (recording) {
                out_file.write(buffer.data(), bytes_read);

                if (now - last_motion > 5s || now - start_time_ > 5min) {
                    std::cout << "[run] Stopping recording due to timeout or duration limit.\n";
                    stop_recording(recording, out_file, current_temp_path);
                }
            }
        }

        std::cout << "[run] Loop finished.\n";
        return {};
    }

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

    void start_recording(bool& recording, std::ofstream& file, std::string& path) {
        recording = true;
        start_time_ = std::chrono::steady_clock::now();
        auto ts = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        path = std::format("{}/{}.h264", temp_dir_, ts);
        file.open(path, std::ios::binary);
        std::cout << std::format("[start_recording] Recording started: {}\n", path);
    }

    void stop_recording(bool& recording, std::ofstream& file, std::string& path) {
        file.close();
        recording = false;
        {
            std::lock_guard lock(mtx_);
            auto now = std::chrono::system_clock::now();
            std::string date_str = std::format("{:%Y-%m-%d}", now);
            fs::path daily_dir = fs::path(final_dir_) / date_str;
            
            if (!fs::exists(daily_dir)) {
                std::cout << "[stop_recording] Creating daily directory: " << daily_dir << "\n";
                fs::create_directories(daily_dir);
            }

            std::string final_path = (daily_dir / fs::path(path).stem().replace_extension(".mp4")).string();
            std::cout << std::format("[stop_recording] Queuing conversion: {} -> {}\n", path, final_path);
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

                if (stop_requested_ && tasks_.empty()) {
                    std::cout << "[process_conversions] Stop requested and no tasks left.\n";
                    return;
                }

                task = tasks_.front();
                tasks_.pop();
                std::cout << std::format("[process_conversions] Processing task: {}\n", task.dest_);
            }

            std::string ffmpeg_cmd = std::format("ffmpeg -y -i {} -c copy {} > /dev/null 2>&1", task.src_, task.dest_);
            std::cout << std::format("[process_conversions] Executing: {}\n", ffmpeg_cmd);
            int ret = std::system(ffmpeg_cmd.c_str());
            if (ret == 0) {
                fs::remove(task.src_);
                std::cout << std::format("[process_conversions] Finished conversion: {}\n", task.dest_);
            } else {
                std::cerr << std::format("[process_conversions] FFmpeg failed with code: {}\n", ret);
            }
        }
    }
};

int main(int argc, char* argv[]) {
    absl::InitializeSymbolizer(argv[0]);
    absl::FailureSignalHandlerOptions options;
    absl::InstallFailureSignalHandler(options);
    
    absl::SetProgramUsageMessage("Camera monitoring and recording service. Monitors hardware ISP motion signals and saves recorded video to MP4.");
    absl::ParseCommandLine(argc, argv);

    std::string t_dir = absl::GetFlag(FLAGS_temp_dir);
    std::string f_dir = absl::GetFlag(FLAGS_final_dir);
    std::string md_file = absl::GetFlag(FLAGS_motion_detect_file);
    
    std::cout << std::format("[main] Starting with temp_dir: {}, final_dir: {}, motion_detect_file: {}\n", t_dir, f_dir, md_file);

    CameraService service(t_dir, f_dir, md_file);
    try {
        std::cout << "[main] Calling service.run()...\n";
        auto result = service.run();
        if (!result) {
            std::cerr << std::format("[main] Runtime Error: {}\n", result.error());
            print_stack_dump();
            std::exit(1);
        } else {
            std::cout << "[main] service.run() returned successfully.\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[main] Uncaught exception: " << e.what() << std::endl;
        print_stack_dump();
        throw;
    }

    std::cout << "[main] Exiting cleanly.\n";
    return 0;
}
