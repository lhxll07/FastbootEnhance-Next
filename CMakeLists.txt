cmake_minimum_required(VERSION 3.24)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FASTBOOT_ENHANCE_VERSION LIMIT_COUNT 1)
if(NOT FASTBOOT_ENHANCE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain a semantic version such as 0.2.0")
endif()

project(FastbootEnhance VERSION ${FASTBOOT_ENHANCE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent)
find_package(Protobuf REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(BZip2 REQUIRED)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    pkg_check_modules(LIBZIP QUIET IMPORTED_TARGET libzip)
endif()
if(NOT TARGET PkgConfig::LIBZIP)
    find_package(libzip CONFIG REQUIRED)
endif()
if(NOT TARGET PkgConfig::LIBZIP AND NOT TARGET libzip::zip)
    message(FATAL_ERROR "libzip CMake package or pkg-config module is required")
endif()

protobuf_generate_cpp(PAYLOAD_PROTO_SRCS PAYLOAD_PROTO_HDRS src/update_metadata.proto)

set(FASTBOOT_ENHANCE_SOURCES
    src/main.cpp
    src/payload.cpp
    src/platform_paths.cpp
    ${PAYLOAD_PROTO_SRCS}
    ${PAYLOAD_PROTO_HDRS}
)

if(WIN32)
    add_executable(FastbootEnhance WIN32 ${FASTBOOT_ENHANCE_SOURCES})
elseif(APPLE)
    add_executable(FastbootEnhance MACOSX_BUNDLE ${FASTBOOT_ENHANCE_SOURCES})
else()
    add_executable(FastbootEnhance ${FASTBOOT_ENHANCE_SOURCES})
endif()

set_target_properties(FastbootEnhance PROPERTIES
    OUTPUT_NAME FastbootEnhance
    MACOSX_BUNDLE_BUNDLE_NAME FastbootEnhance
    MACOSX_BUNDLE_GUI_IDENTIFIER io.fastbootenhance.FastbootEnhance
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

if(APPLE)
    set_target_properties(FastbootEnhance PROPERTIES
        INSTALL_RPATH "@loader_path/../Frameworks"
    )
endif()

target_include_directories(FastbootEnhance PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${Protobuf_INCLUDE_DIRS}
    ${LIBLZMA_INCLUDE_DIRS}
    ${BZIP2_INCLUDE_DIRS}
)

target_link_libraries(FastbootEnhance PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    ${LIBLZMA_LIBRARIES}
    ${BZIP2_LIBRARIES}
)

if(TARGET PkgConfig::LIBZIP)
    target_link_libraries(FastbootEnhance PRIVATE PkgConfig::LIBZIP)
else()
    target_link_libraries(FastbootEnhance PRIVATE libzip::zip)
endif()

if(TARGET protobuf::libprotobuf)
    target_link_libraries(FastbootEnhance PRIVATE protobuf::libprotobuf)
elseif(Protobuf_LIBRARIES)
    target_link_libraries(FastbootEnhance PRIVATE ${Protobuf_LIBRARIES})
else()
    message(FATAL_ERROR "protobuf runtime target or libraries were not found")
endif()

target_compile_definitions(FastbootEnhance PRIVATE
    QT_NO_KEYWORDS
    FASTBOOT_ENHANCE_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FastbootEnhance PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(FastbootEnhance PRIVATE /W4 /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS FastbootEnhance
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME fastboot-enhance-version COMMAND $<TARGET_FILE:FastbootEnhance> --version)

    add_executable(FastbootEnhancePayloadTest
        tests/payload_test.cpp
        src/payload.cpp
        src/platform_paths.cpp
        ${PAYLOAD_PROTO_SRCS}
        ${PAYLOAD_PROTO_HDRS}
    )
    target_include_directories(FastbootEnhancePayloadTest PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Protobuf_INCLUDE_DIRS}
        ${LIBLZMA_INCLUDE_DIRS}
        ${BZIP2_INCLUDE_DIRS}
    )
    target_link_libraries(FastbootEnhancePayloadTest PRIVATE
        Qt6::Core
        ${LIBLZMA_LIBRARIES}
        ${BZIP2_LIBRARIES}
    )
    if(TARGET PkgConfig::LIBZIP)
        target_link_libraries(FastbootEnhancePayloadTest PRIVATE PkgConfig::LIBZIP)
    else()
        target_link_libraries(FastbootEnhancePayloadTest PRIVATE libzip::zip)
    endif()
    if(TARGET protobuf::libprotobuf)
        target_link_libraries(FastbootEnhancePayloadTest PRIVATE protobuf::libprotobuf)
    else()
        target_link_libraries(FastbootEnhancePayloadTest PRIVATE ${Protobuf_LIBRARIES})
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(FastbootEnhancePayloadTest PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(FastbootEnhancePayloadTest PRIVATE /W4 /utf-8)
    endif()
    add_test(NAME fastboot-enhance-payload COMMAND $<TARGET_FILE:FastbootEnhancePayloadTest>)
endif()
