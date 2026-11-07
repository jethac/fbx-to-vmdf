cmake_minimum_required(VERSION 3.14)
project(fbx-to-vmdf VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)

# The output has to be byte-identical on every host, so pin IEEE semantics:
# no fused multiply-add contraction (clang contracts by default, and arm64
# has FMA), no fast-math, and no x87 excess precision.
if(MSVC)
  # /fp:precise does not contract a*b+c into FMA (only /fp:fast or /fp:contract do)
  set(FBX2VMDF_FP_FLAGS /fp:precise)
  add_compile_options(/utf-8 /W3 /bigobj ${FBX2VMDF_FP_FLAGS})
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  set(FBX2VMDF_FP_FLAGS -ffp-contract=off -fno-fast-math)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86)$")
    list(APPEND FBX2VMDF_FP_FLAGS -msse2 -mfpmath=sse)
  endif()
  add_compile_options(${FBX2VMDF_FP_FLAGS})
endif()

# ---------------------------------------------------------------- vendored
add_library(fbx2vmdf_thirdparty STATIC
  third_party/ufbx/ufbx.c
  third_party/pugixml/pugixml.cpp
  third_party/meshoptimizer/src/allocator.cpp
  third_party/meshoptimizer/src/clusterizer.cpp
  third_party/meshoptimizer/src/indexcodec.cpp
  third_party/meshoptimizer/src/indexgenerator.cpp
  third_party/meshoptimizer/src/overdrawanalyzer.cpp
  third_party/meshoptimizer/src/overdrawoptimizer.cpp
  third_party/meshoptimizer/src/simplifier.cpp
  third_party/meshoptimizer/src/spatialorder.cpp
  third_party/meshoptimizer/src/stripifier.cpp
  third_party/meshoptimizer/src/vcacheanalyzer.cpp
  third_party/meshoptimizer/src/vcacheoptimizer.cpp
  third_party/meshoptimizer/src/vertexcodec.cpp
  third_party/meshoptimizer/src/vertexfilter.cpp
  third_party/meshoptimizer/src/vfetchanalyzer.cpp
  third_party/meshoptimizer/src/vfetchoptimizer.cpp
)
file(GLOB FBX2VMDF_YAML_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/src/contrib/*.cpp)
target_sources(fbx2vmdf_thirdparty PRIVATE ${FBX2VMDF_YAML_SOURCES})
target_include_directories(fbx2vmdf_thirdparty PUBLIC
  third_party/ufbx
  third_party/pugixml
  third_party/meshoptimizer/src
  third_party/yaml-cpp/include)
target_compile_definitions(fbx2vmdf_thirdparty PUBLIC PUGIXML_NO_EXCEPTIONS)
if(NOT MSVC)
  target_compile_options(fbx2vmdf_thirdparty PRIVATE -w)
endif()

# ---------------------------------------------------------------- the tool
add_executable(fbx-to-vmdf
  src/main.cpp
  src/Common.cpp
  src/Compare.cpp
  src/DetMath.cpp
  src/Exchange.cpp
  src/FbxScene.cpp
  src/Loader.cpp
  src/MaterialDefinition.cpp
  src/MaterialOverrides.cpp
  src/Writer.cpp
)
target_link_libraries(fbx-to-vmdf PRIVATE fbx2vmdf_thirdparty)
target_compile_definitions(fbx-to-vmdf PRIVATE FBX2VMDF_VERSION="${PROJECT_VERSION}")
if(NOT MSVC)
  target_compile_options(fbx-to-vmdf PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-deprecated-declarations)
endif()

# ---------------------------------------------------------------- tests
enable_testing()
add_executable(fbx2vmdf_selftest tests/selftest.cpp src/Common.cpp src/DetMath.cpp)
target_include_directories(fbx2vmdf_selftest PRIVATE src)
add_test(NAME selftest COMMAND fbx2vmdf_selftest)

install(TARGETS fbx-to-vmdf RUNTIME DESTINATION bin)
