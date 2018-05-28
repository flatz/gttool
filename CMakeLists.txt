cmake_minimum_required(VERSION 3.5)

string(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(THIRDPARTY_PATH "${CMAKE_SOURCE_DIR}/thirdparty")

project(gttool)

#
# Compiler settings.
#

string(APPEND CMAKE_CXX_FLAGS "-std=c++14 -march=native -Wall -Wextra -Werror-implicit-function-declaration -Wno-unused-function -Wno-unused-parameter -Wno-unused-label -fno-limit-debug-info -fno-strict-aliasing")

if(WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
	add_definitions(-DNOMINMAX)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_definitions(-D_DEBUG)
else()
	add_definitions(-DNDEBUG)
endif()

#
# Third-party dependendices.
#

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIRS})
endif()

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost REQUIRED COMPONENTS system filesystem iostreams program_options)
if(Boost_FOUND)
	include_directories(${Boost_INCLUDE_DIRS})
	message("-- Boost ${Boost_MAJOR_VERSION}.${Boost_MINOR_VERSION}.${Boost_SUBMINOR_VERSION} found!")
endif()

link_directories(thirdparty/lib)

#
# Project settings.
#

set(SOURCE_FILES
	src/btree.cpp
	src/btree.hpp
	src/common.hpp
	src/compression.cpp
	src/compression.hpp
	src/crc.cpp
	src/crc.hpp
	src/debug.cpp
	src/debug.hpp
	src/io_util.hpp
	src/crypto.cpp
	src/crypto.hpp
	src/main.cpp
	src/util.cpp
	src/util.hpp
	src/volume.cpp
	src/volume.hpp
	src/io_util.cpp)

set(THIRDPARTY_LIBRARIES ${ZLIB_LIBRARIES} ${Boost_LIBRARIES})

add_executable(gttool ${SOURCE_FILES})
target_link_libraries(gttool ${THIRDPARTY_LIBRARIES})
