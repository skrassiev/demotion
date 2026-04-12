#include "camera_service.h"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

class CameraServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = "./test_temp";
        final_dir = "./test_final";
        fs::create_directories(temp_dir);
        fs::create_directories(final_dir);
    }

    void TearDown() override {
        fs::remove_all(temp_dir);
        fs::remove_all(final_dir);
    }

    std::string temp_dir;
    std::string final_dir;
};

TEST_F(CameraServiceTest, OutFileIsSetToTimestamp) {
    CameraService service(temp_dir, final_dir, "test.json");
    
    bool recording = false;
    std::ofstream out_file;
    std::string current_temp_path;

    service.start_recording(recording, out_file, current_temp_path);
    
    ASSERT_TRUE(recording);
    ASSERT_TRUE(out_file.is_open());
    ASSERT_FALSE(current_temp_path.empty());
    
    // Check if path is in temp_dir
    EXPECT_EQ(fs::path(current_temp_path).parent_path().string(), temp_dir);
    
    // Check if filename is a timestamp (digits only before .h264)
    std::string stem = fs::path(current_temp_path).stem().string();
    for (char c : stem) {
        EXPECT_TRUE(std::isdigit(c));
    }

    service.stop_recording(recording, out_file, current_temp_path);
    
    ASSERT_FALSE(recording);
    ASSERT_FALSE(out_file.is_open());
}

TEST_F(CameraServiceTest, FinalPathHasCorrectFormat) {
    CameraService service(temp_dir, final_dir, "test.json");
    
    bool recording = false;
    std::ofstream out_file;
    std::string current_temp_path;

    service.start_recording(recording, out_file, current_temp_path);
    
    // Simulate some time passed
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    service.stop_recording(recording, out_file, current_temp_path);
    
    // In our implementation, stop_recording pushes to tasks_.
    // We can't easily check private tasks_ without friends or exposure,
    // but we can check if the directory was created.
    
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm;
    localtime_r(&t, &local_tm);
    char date_buf[32];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local_tm);
    
    fs::path daily_dir = fs::path(final_dir) / date_buf;
    EXPECT_TRUE(fs::exists(daily_dir));
}
