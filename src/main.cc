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
#include <atomic>
#include <execinfo.h>
#include <csignal>
#include <cstdlib>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

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
            // Use addr2line to get function and line info. -p for pretty, -C for demangle, -e for executable.
            std::string cmd = std::format("addr2line -e /proc/self/exe -p -C {} 2>/dev/null", addr);
            FILE* fp = popen(cmd.c_str(), "r");
            if (fp) {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), fp)) {
                    std::string line(buffer);
                    if (line.find("main.cc") != std::string::npos) {
                        // For main.cc, print the full line (including line numbers).
                        std::cerr << std::format("#{} {}", i, line);
                    } else {
                        // For other files, strip the ' at ...' part to hide line numbers.
                        size_t pos = line.find(" at ");
                        if (pos != std::string::npos) {
                            std::cerr << std::format("#{} {}", i, line.substr(0, pos)) << std::endl;
                        } else {
                            std::cerr << std::format("#{} {}", i, line);
                        }
                    }
                } else {
                    std::cerr << std::format("#{} {}", i, symbol) << std::endl;
                }
                pclose(fp);
            } else {
                std::cerr << std::format("#{} {}", i, symbol) << std::endl;
            }
        } else {
            std::cerr << std::format("#{} {}", i, symbol) << std::endl;
        }
    }
    free(symbols);
}

void signal_handler(int sig) {
    std::cerr << std::format("\nCaught fatal signal: {}", sig) << std::endl;
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
    std::string src;
    std::string dest;
};

class CameraService {
public:
    CameraService() : stop_requested(false) {
        fs::create_directories(temp_dir);
        fs::create_directories(final_dir);
        worker = std::thread([this]() {
            process_conversions();
        });
    }

    ~CameraService() {
        stop_requested = true;
        cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
    }

    std::expected<void, std::string> run() {
        std::string cmd = "rpicam-vid -t 0 --inline --nopreview --width 1280 --height 720 "
                          "--framerate 30 --lores-width 160 --lores-height 120 "
                          "--post-process-file motion_detect.json -o - 2>&1";

        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return std::unexpected("Failed to open rpicam-vid pipe");

        std::cout << "Service started. Monitoring hardware ISP motion signals..." << std::endl;

        std::array<char, 64 * 1024> buffer;
        auto last_motion = std::chrono::steady_clock::now() - 1h;
        bool recording = false;
        std::ofstream out_file;
        std::string current_temp_path;

        // Monitor pipe (stdout + stderr) for the "Motion detected" string
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
                // Write captured video data to the output file
                out_file.write(buffer.data(), bytes_read);

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
    std::thread worker;
    std::atomic<bool> stop_requested;
    std::mutex mtx;
    std::condition_variable cv;
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

    void process_conversions() {
        while (!stop_requested) {
            ConversionTask task;
            {
                std::unique_lock lock(mtx);
                cv.wait(lock, [this] { return !tasks.empty() || stop_requested; });

                if (stop_requested && tasks.empty()) return;

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
    setup_crash_handler();
    CameraService service;
    auto result = service.run();
    if (!result) {
        std::cerr << std::format("\nRuntime Error: {}", result.error()) << std::endl;
        print_stack_dump();
        std::exit(1);
    }
    return 0;
}
