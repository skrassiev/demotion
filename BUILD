
load("@rules_cc//cc:defs.bzl", "cc_binary")

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
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/cleanup",
    ],
)

platform(
    name = "rpi3",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:linux",
    ],
)
