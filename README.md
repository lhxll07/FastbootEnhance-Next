# Fastboot Enhance

Fastboot Enhance is a portable fastboot toolbox and Payload dumper for Windows, macOS, and Linux.

The Qt/C++ implementation is the unified application. Each release contains one self-contained download per supported platform and architecture:

- Windows x86_64: self-extracting `.exe`
- macOS arm64/x86_64: `.dmg` containing the app bundle (signed/notarized when release credentials are configured)
- Linux x86_64: `.AppImage`

The application does not install itself or write beside the executable. Temporary files are created in the system temporary directory and removed when the operation finishes. The downloaded release file can be deleted after use.

## Features

- Fastboot device and variable inspection
- Image flashing and partition erasing
- Logical partition create, resize, and delete operations
- Bootloader, fastbootd, recovery, and system reboot actions
- A/B slot switching and snapshot update control
- Payload manifest inspection and dynamic partition metadata
- Payload partition extraction and flashing

## Download and use

Download the artifact for the current platform from [Releases](https://github.com/lhxll07/FastbootEnhance-Next/releases).

Linux:

```sh
chmod +x FastbootEnhance-Linux-x86_64.AppImage
./FastbootEnhance-Linux-x86_64.AppImage
```

macOS uses the normal DMG workflow: open the DMG and launch Fastboot Enhance from it. Windows uses a single self-extracting EXE.

The release bundles Android Platform Tools. USB access still follows the operating system rules: Linux may require an Android udev rule, Windows may require a WinUSB/Android driver, and macOS may require approval for a downloaded application.

## Build locally

Required libraries are Qt 6.4+, Protobuf, liblzma, bzip2, and libzip. The package names vary by operating system.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/FastbootEnhance --version
```

The application searches for bundled tools first. For local development, these environment variables can override the tools:

```sh
FASTBOOT_ENHANCE_FASTBOOT=/path/to/fastboot \
FASTBOOT_ENHANCE_ADB=/path/to/adb \
./build/FastbootEnhance
```

## Package

Linux:

```sh
FASTBOOT_ENHANCE_PLATFORM_TOOLS=/path/to/platform-tools \
FASTBOOT_ENHANCE_BUILD_DIR="$PWD/build" \
./packaging/package-linux.sh
```

Windows PowerShell:

```powershell
./packaging/package-windows.ps1 `
  -BuildDir .\build-windows `
  -PlatformTools C:\platform-tools `
  -RuntimeDir C:\vcpkg\installed\x64-windows\bin
```

macOS:

```sh
FASTBOOT_ENHANCE_PLATFORM_TOOLS=/path/to/platform-tools \
FASTBOOT_ENHANCE_BUILD_DIR="$PWD/build-macos" \
./packaging/package-macos.sh
```

GitHub Actions builds all artifacts from `.github/workflows/build.yml`. The release workflow pins Android Platform Tools `r37.0.1` and verifies its SHA-256 checksum before packaging.

## Project status

The old WPF/.NET Framework implementation remains in the repository as a reference for the original Windows behavior. It is not the cross-platform release target.

## License

The application is distributed under the original MIT license. Android Platform Tools and bundled third-party libraries retain their respective upstream licenses and notices.
