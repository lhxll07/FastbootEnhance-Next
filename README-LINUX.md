# Fastboot Enhance Linux

Linux port of Fastboot Enhance. The original Windows source remains in this repository as the baseline.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/FastbootEnhanceLinux
```

## Portable package

```sh
./linux/package-appimage.sh
```

The result is `dist/FastbootEnhance-Linux-x86_64.AppImage`. It contains the application and Android platform tools and does not install files into the system.

Linux USB access still follows the system's udev rules. On Arch, `android-udev` is the usual one-time setup if `fastboot devices` cannot see the phone as the current user.

The Linux port is distributed under the original MIT license. Android Platform Tools are redistributed with their upstream notices.
