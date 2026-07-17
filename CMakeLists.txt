# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Xiaohulu_AI_Car)

# 修复RGB LCD gcc14.2.0 ICE崩溃
target_compile_options(__idf_esp_lcd PRIVATE -O0)