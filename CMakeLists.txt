cmake_minimum_required(VERSION 3.24)

project(FastbootEnhanceLinux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent)
find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(LZMA REQUIRED IMPORTED_TARGET liblzma)
pkg_check_modules(BZIP2 REQUIRED IMPORTED_TARGET bzip2)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

protobuf_generate_cpp(PAYLOAD_PROTO_SRCS PAYLOAD_PROTO_HDRS linux/update_metadata.proto)

add_executable(FastbootEnhanceLinux
    linux/main.cpp
    linux/payload.cpp
    ${PAYLOAD_PROTO_SRCS}
    ${PAYLOAD_PROTO_HDRS}
)

target_include_directories(FastbootEnhanceLinux PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/linux
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(FastbootEnhanceLinux PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    protobuf::libprotobuf
    PkgConfig::LZMA
    PkgConfig::BZIP2
    PkgConfig::LIBZIP
)

target_compile_definitions(FastbootEnhanceLinux PRIVATE
    QT_NO_KEYWORDS
    FASTBOOT_ENHANCE_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FastbootEnhanceLinux PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS FastbootEnhanceLinux RUNTIME DESTINATION .)
