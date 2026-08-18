# Fastboot Enhance Next

A lightweight Linux port of Fastboot Enhance for fastboot tools, payload images, and dynamic partitions.

## Download

Download the latest `FastbootEnhance-Linux-x86_64.AppImage` from [Releases](https://github.com/lhxll07/fastboot-enhance-next/releases).

The AppImage includes the application, `adb`, and `fastboot`. It does not install files into the system and can be deleted after use.

```sh
chmod +x FastbootEnhance-Linux-x86_64.AppImage
./FastbootEnhance-Linux-x86_64.AppImage
```

## Features

- Fastboot device and variable inspection
- Image flashing and partition erasing
- Logical partition create, resize, and delete operations
- Bootloader, fastbootd, recovery, and system reboot actions
- A/B slot switching and snapshot update control
- Payload manifest inspection and dynamic partition metadata
- Payload partition extraction and flashing
- Built-in Android platform tools

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/FastbootEnhanceLinux
```

To build the portable AppImage:

```sh
./linux/package-appimage.sh
```

Linux USB access still follows the system's udev rules. On Arch, install `android-udev` if `fastboot devices` cannot see the phone as the current user.

The Linux port is distributed under the original MIT license. Android Platform Tools are redistributed with their upstream notices.

The original Windows implementation is preserved as the baseline. See [README-WINDOWS.md](README-WINDOWS.md) for its documentation.
