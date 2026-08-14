# Android native acquisition benchmark

The Android path builds only the acquisition benchmark and its reference,
NEON, and optionally SME2 acquisition sources. It produces a native
`arm64-v8a` command-line executable for `adb shell`; there is no APK, Gradle,
Kotlin, JNI, or Android UI surface.

## Prerequisites

- Android SDK Platform-Tools, including `adb`
- Android NDK with CMake toolchain support
- host `cmake` and `python3`
- an `arm64-v8a` Android device with developer mode and USB debugging enabled
- an authorized device visible in `adb devices -l`

Set `ANDROID_NDK_HOME`, or install the NDK below `ANDROID_SDK_ROOT/ndk` or
`ANDROID_HOME/ndk`. The scripts default to API 28 and static libc++, so no
separate C++ runtime library needs to be pushed.

## Build and inspect without a phone

```sh
bash scripts/verify_android_benchmark_build.sh --sme2 auto
```

The output is `build/android/arm64-v8a/benchmark_acquisition`. The
benchmark-only CMake mode is `SATCOMFEC_ANDROID_BENCHMARK_ONLY=ON`. Reference
code retains scalar auto-vectorization controls, the NEON source is built for
baseline Armv8-A SIMD, and only `src/acquisition/acquisition_sme2.cpp` receives
the SME2 target flag.

`--sme2 auto` prints whether the resulting binary contains the SME2 kernel.
Use `--sme2 on` when compilation of that kernel is a test requirement.

This verifies the AArch64 PIE, static libc++ linkage, NEON instruction evidence,
scalar-reference isolation, and SME2 streaming/ZA instructions when compiled.

## Run through ADB

```sh
bash scripts/run_android_benchmark.sh \
  --sme2 auto \
  --output build/android-results/device-small.json
```

Use `--serial SERIAL` when more than one device is connected. The default
device run uses the fixed `small` workload, one warm-up, seven timed samples,
and a 20 ms minimum sample duration.

The run script verifies the selected device is authorized and reports
`arm64-v8a`, pushes the executable to `/data/local/tmp`, executes it, and pulls
the authoritative JSON result. The report includes:

- Android release, API level, ABI, kernel release, and device model
- raw `AT_HWCAP` and `AT_HWCAP2` masks
- runtime NEON, SVE, SME, and SME2 capability status
- compiled and actually executed implementation identity
- correctness against the scalar oracle before timing
- fixed workload dimensions and all three timing modes
- raw timing samples, distribution statistics, throughput, and relative speedups
- implementation-specific workspace accounting

## Runtime safety

The executable never labels a fallback as NEON or SME2. On Android/Linux, the
SME2 path checks the kernel-provided `AT_HWCAP2` SME2 bit before requesting SME
streaming state or entering the SME2 kernel. If the NDK lacks SME2 ACLE support,
the kernel is not compiled. If the device lacks runtime support, SME2 is
reported unavailable and is not executed or timed.

An Android build does not prove device execution. Mobile timing exists only
when `run_android_benchmark.sh` successfully retrieves a JSON report from a
connected device. No Android performance result is embedded in the docs.
