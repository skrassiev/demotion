load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "camera_service_lib",
    srcs = ["src/camera_service.cc"],
    hdrs = ["src/camera_service.h"],
    linkstatic = True,
    copts = [
        "-std=c++2b",
        "-g",
        "-fno-omit-frame-pointer",
        "-fno-optimize-sibling-calls",
    ],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/cleanup",
    ],
)

cc_binary(
    name = "camera-service",
    srcs = ["src/main.cc"],
    copts = [
        "-std=c++2b",
        "-g",
        "-fno-omit-frame-pointer",
        "-fno-optimize-sibling-calls",
    ], 
    linkopts = [
        "-rdynamic",
        "-lpthread",
        "-lm",
    ],
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
    copts = ["-std=c++2b"],
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
    copts = select({
        "@platforms//cpu:aarch64": [
            "-std=c++2b",
            "-O3",
            "-march=armv8-a+simd", # Enables NEON SIMD
            "-ffast-math",
        ],
        "@platforms//cpu:x86_64": [
            "-std=c++2b",
            "-O3",
            "-march=native",       # Enables AVX2/SSE4 on your dev machine
            "-ffast-math",
        ],
        "//conditions:default": ["-O2"],
    }),
    visibility = ["//visibility:public"],
)

cc_test(
    name = "background_subtractor_test",
    srcs = ["src/background_subtractor_test.cc"],
    copts = ["-std=c++2b"],
    linkstatic = True,
    deps = [
        ":background_subtractor_lib",
        "@googletest//:gtest_main",
    ],
)


platform(
    name = "rpi3",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:linux",
    ],
)
