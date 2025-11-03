cmake_minimum_required(VERSION 3.22)

project(cpp-sensors-project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg toolchain
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(VCPKG_ROOT "C:/dev/tools/vcpkg")
  if (EXISTS "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file")
  else()
    message(FATAL_ERROR "VCPKG toolchain not found at ${VCPKG_ROOT}. Fix path or pass -DCMAKE_TOOLCHAIN_FILE=<path>")
  endif()
endif()
set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "")

# Английский вывод
set(CMAKE_VS_GLOBALS "PreferredUILang=en-US")

# Общие флаги и кодировка исходников
add_library(project_options INTERFACE)
target_compile_definitions(project_options INTERFACE BOOST_ASIO_NO_DEPRECATED NOMINMAX _WIN32_WINNT=0x0A00)
if (MSVC)
  target_compile_options(project_options INTERFACE /permissive- /W4 /EHsc /utf-8)
  # Единый рантайм для всех целей: MD/MDd
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Boost/nlohmann_json в CONFIG-режиме (избежим ворнинга CMP0167)
if (POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost CONFIG REQUIRED COMPONENTS thread)
find_package(nlohmann_json CONFIG REQUIRED)

# LZ4 / ZSTD / CityHash / Abseil из vcpkg
find_package(lz4  CONFIG REQUIRED)      # => lz4::lz4
find_package(zstd CONFIG REQUIRED)      # => zstd::libzstd
find_package(absl CONFIG REQUIRED)      # => absl::int128 (и при надобности absl::base)

# Redis client (redis-plus-plus)
find_package(redis++ CONFIG REQUIRED)

# CityHash встречается по-разному — попробуем все варианты
find_package(cityhash CONFIG QUIET)
set(_CITYHASH_CANDIDATES
  cityhash::cityhash
  unofficial::cityhash
  cityhash
)
set(CITYHASH_TARGET "")
foreach(t IN LISTS _CITYHASH_CANDIDATES)
  if (TARGET ${t})
    set(CITYHASH_TARGET ${t})
    break()
  endif()
endforeach()
if (NOT CITYHASH_TARGET)
  message(FATAL_ERROR "CityHash CMake target not found. Install it via vcpkg and/or update to a recent port. Tried: cityhash::cityhash, unofficial::cityhash, cityhash")
endif()

# ClickHouse: сперва нормальный импорт из vcpkg
find_package(clickhouse-cpp CONFIG QUIET)

# Пути vcpkg на случай fallback
if (NOT DEFINED VCPKG_INSTALLED_DIR)
  get_filename_component(_VCPKG_ROOT "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
  get_filename_component(_VCPKG_ROOT "${_VCPKG_ROOT}" DIRECTORY)
  set(VCPKG_INSTALLED_DIR "${_VCPKG_ROOT}/installed")
endif()
set(_INC_DIR     "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include")
set(_LIB_DIR_REL "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
set(_LIB_DIR_DBG "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib")

# Источники
file(GLOB_RECURSE HDRS CONFIGURE_DEPENDS "include/**/*.hpp")
file(GLOB SRCS  CONFIGURE_DEPENDS "src/*.cpp")

add_executable(${PROJECT_NAME} ${SRCS} ${HDRS})
target_include_directories(${PROJECT_NAME} PRIVATE include)

# Базовые линки
target_link_libraries(${PROJECT_NAME}
  PRIVATE
    project_options
    Boost::thread
    nlohmann_json::nlohmann_json
    lz4::lz4
    zstd::libzstd
    ${CITYHASH_TARGET}
    absl::int128
    redis++::redis++ 
)

# ClickHouse: либо нормальная цель, либо .lib из installed
if (TARGET clickhouse-cpp::clickhouse-cpp)
  target_link_libraries(${PROJECT_NAME} PRIVATE clickhouse-cpp::clickhouse-cpp)
else()
  find_library(CLICKHOUSECPP_RELEASE
    NAMES clickhouse-cpp clickhouse-cpp-lib
    PATHS "${_LIB_DIR_REL}" NO_DEFAULT_PATH)
  find_library(CLICKHOUSECPP_DEBUG
    NAMES clickhouse-cpp clickhouse-cpp-lib
    PATHS "${_LIB_DIR_DBG}" NO_DEFAULT_PATH)
  if (NOT CLICKHOUSECPP_RELEASE OR NOT CLICKHOUSECPP_DEBUG)
    message(FATAL_ERROR "clickhouse-cpp not found via config or libs. Try: vcpkg install clickhouse-cpp:${VCPKG_TARGET_TRIPLET}")
  endif()
  target_link_libraries(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:${CLICKHOUSECPP_DEBUG}>
    $<$<NOT:$<CONFIG:Debug>>:${CLICKHOUSECPP_RELEASE}>
  )
  target_include_directories(${PROJECT_NAME} PRIVATE "${_INC_DIR}")
endif()

# Сокеты на Windows
if (WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# Копирование конфига рядом с exe
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/config/server.json
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/server.json
)


# Gtest + unit tests
find_package(GTest CONFIG REQUIRED)

enable_testing()

add_executable(unit_tests
  tests/test_time.cpp
)

target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(unit_tests
  PRIVATE
    project_options
    GTest::gtest
    GTest::gtest_main
)

add_test(NAME unit_tests COMMAND unit_tests)