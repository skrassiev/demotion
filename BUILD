load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

COMMON_COPTS = [
    "-std=c++2b",
    "-g",
    "-fno-omit-frame-pointer",
    "-fno-optimize-sibling-calls",
]

COMMON_LINKOPTS = [
    "-rdynamic",
    "-lpthread",
    "-lm",
]

# Move platform-specific options here
# This library provides flags that should be applied to performance-critical code
cc_library(
    name = "optimized_copts",
    copts = select({
        "@platforms//cpu:aarch64": [
            "-O3",
            "-march=armv8-a+crc",
            "-mcpu=cortex-a53",
            "-mtune=cortex-a53",
            "-ffast-math",
        ],
        "@platforms//cpu:x86_64": [
            "-O3",
            "-march=native",
            "-ffast-math",
        ],
        "//conditions:default": ["-O2"],
    }),
)

cc_library(
    name = "camera_service_lib",
    srcs = ["src/camera_service.cc"],
    hdrs = ["src/camera_service.h"],
    linkstatic = True,
    copts = COMMON_COPTS,
    strip_include_prefix = "src",
    deps = [
        ":optimized_copts",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/cleanup",
    ],
)

cc_binary(
    name = "camera-service",
    srcs = ["src/main.cc"],
    copts = COMMON_COPTS, 
    linkopts = COMMON_LINKOPTS,
    deps = [
        ":camera_service_lib",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/debugging:failure_signal_handler",
    ],
)

cc_test(
    name = "camera-service-test",
    srcs = ["src/main_test.cc"],
    copts = COMMON_COPTS,
    linkopts = COMMON_LINKOPTS,
    linkstatic = True,
    deps = [
        ":camera_service_lib",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "background_subtractor_lib",
    srcs = ["src/background_subtractor.cc"],
    hdrs = ["src/background_subtractor.h"],
    strip_include_prefix = "src",
    copts = COMMON_COPTS,
    visibility = ["//visibility:public"],
    deps = [":optimized_copts"],
)

cc_test(
    name = "background_subtractor_test",
    srcs = ["src/background_subtractor_test.cc"],
    copts = COMMON_COPTS,
    linkopts = COMMON_LINKOPTS,
    linkstatic = True,
    data = glob(["testdata/**"]),
    deps = [
        ":background_subtractor_lib",
        "@googletest//:gtest_main",
    ],
)

# In your BUILD file
cc_binary(
    name = "motion_plugin.so",
    srcs = ["src/motion_plugin.cc"],
    linkshared = True,
    copts = [
        "-std=c++2b",
        "-fPIC",
    ],
    deps = [
        ":background_subtractor_lib",
        ":hermetic_libcamera",
        "@libcamera_apps//:post_processing_plugin_hdrs",
    ],
)

cc_library(
    name = "hermetic_libcamera",
    srcs = select({
        ":rpi3_config": [
            "third_party/libcamera/usr/lib/aarch64-linux-gnu/libcamera.so",
            "third_party/libcamera/usr/lib/aarch64-linux-gnu/libcamera.so.0.7",
            "third_party/libcamera/usr/lib/aarch64-linux-gnu/libcamera.so.0.7.0",
            "third_party/libcamera/usr/lib/aarch64-linux-gnu/libcamera-base.so",
            "third_party/libcamera/usr/lib/aarch64-linux-gnu/libcamera-base.so.0.7",
            "third_party/libcamera/usr/lib/aarch64-linux-gnu/libcamera-base.so.0.7.0",
        ],
        "//conditions:default": [],
    }),
    hdrs = glob(["third_party/libcamera/usr/include/libcamera/libcamera/**/*.h"]),
    includes = ["third_party/libcamera/usr/include/libcamera"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "debug_sandbox",
    outs = ["debug_sandbox.txt"],
    cmd = "find . -maxdepth 5 > $@",
)

config_setting(
    name = "rpi3_config",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:linux",
    ],
)

platform(
    name = "rpi3",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:linux",
    ],
)

platform(
    name = "x86_64",
    constraint_values = [
        "@platforms//cpu:x86_64",
        "@platforms//os:linux",
    ],
)
