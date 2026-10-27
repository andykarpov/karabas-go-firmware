# Host unit tests of lib/common (the hardware-independent logic), with Unity:
#
#   cmake -S test -B build-test -G Ninja && ninja -C build-test && ctest --test-dir build-test
#
# Every test/test_<name>/test_main.c is one executable and one ctest test.
cmake_minimum_required(VERSION 3.14)
project(karabas_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG v2.6.0
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(unity)

set(KG_COMMON ${CMAKE_CURRENT_LIST_DIR}/../lib/common)
file(GLOB KG_COMMON_SOURCES CONFIGURE_DEPENDS ${KG_COMMON}/*.c)
add_library(karabas_common STATIC ${KG_COMMON_SOURCES})
target_include_directories(karabas_common PUBLIC ${KG_COMMON})
target_compile_options(karabas_common PRIVATE -Wall -Wextra)

enable_testing()
file(GLOB KG_TEST_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_LIST_DIR}/test_*)
foreach(dir ${KG_TEST_DIRS})
  get_filename_component(name ${dir} NAME)
  file(GLOB sources CONFIGURE_DEPENDS ${dir}/*.c)
  add_executable(${name} ${sources})
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  target_link_libraries(${name} PRIVATE karabas_common unity m)
  add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${dir})
endforeach()
