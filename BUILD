
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
        "-Lexternal/toolchains_arm_gnu~~arm_toolchain~aarch64_none_linux_gnu_linux_x86_64/aarch64-none-linux-gnu/libc/usr/lib64",
        "-static-libstdc++", 
        "-static-libgcc",
        "-static",
        "-Wl,--start-group",
        "-lstdc++",
        "-lpthread",
        "-latomic",
        "-l:libm-2.38.a",
        "-l:libmvec.a",
        "-lc",
        "-Wl,--end-group",
    ],
    linkstatic = True,
)

platform(
    name = "rpi3",
    constraint_values = [
        "@platforms//cpu:aarch64",
        "@platforms//os:linux",
    ],
)
