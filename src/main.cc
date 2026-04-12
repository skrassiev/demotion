#include "src/camera_service.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"

int main(int argc, char* argv[]) {
    absl::InitializeSymbolizer(argv[0]);
    absl::FailureSignalHandlerOptions options;
    absl::InstallFailureSignalHandler(options);
    
    absl::SetProgramUsageMessage("Camera monitoring and recording service. Monitors hardware ISP motion signals and saves recorded video to MP4.");
    absl::ParseCommandLine(argc, argv);

    CameraService service(absl::GetFlag(FLAGS_temp_dir), 
                          absl::GetFlag(FLAGS_final_dir),
                          absl::GetFlag(FLAGS_motion_detect_file));
    try {
        auto result = service.run();
        if (!result) {
            Log(std::format("\nRuntime Error: {}\n", result.error()), true);
            std::exit(1);
        }
    } catch (const std::exception& e) {
        Log(std::format("Uncaught exception: {}\n", e.what()), true);
        throw;
    }

    return 0;
}
