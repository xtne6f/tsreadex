cmake_minimum_required(VERSION 3.13)
project(tsreadex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

set(TSREADEX_LIBRARY_SRC util.cpp id3conv.cpp servicefilter.cpp aac.cpp huffman.cpp traceb24.cpp)
set(TSREADEX_LIBRARY_HDR util.hpp id3conv.hpp servicefilter.hpp aac.hpp huffman.hpp traceb24.hpp)

add_library(tsreadexlib ${TSREADEX_LIBRARY_SRC} ${TSREADEX_LIBRARY_HDR})
set_property(TARGET tsreadexlib PROPERTY OUTPUT_NAME tsreadex)
add_library(tsreadex::lib ALIAS tsreadexlib)
target_include_directories(tsreadexlib PUBLIC ${PROJECT_SOURCE_DIR})
export(TARGETS tsreadexlib NAMESPACE tsreadex FILE ${PROJECT_BINARY_DIR}/tsreadex-targets.cmake)

add_executable(tsreadex tsreadex.cpp ${TSREADEX_LIBRARY_SRC})

if(MINGW)
  target_link_options(tsreadex PRIVATE -municode -static)
endif()
