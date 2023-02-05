cmake_minimum_required(VERSION 3.10)
project(tinykv)

# enable c++ 11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# take respectives flags for debug & release process 
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-std=c++11 -g -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-std=c++11 -g -O2")

# default build type : Debug
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()


include_directories(${PROJECT_SOURCE_DIR}/src)

aux_source_directory(. PROJECT_SRC_LIST)
aux_source_directory(./src SRC_LIST)
add_executable(${PROJECT_NAME} ${PROJECT_SRC_LIST} ${SRC_LIST})
target_link_libraries(${PROJECT_NAME} pthread)

enable_testing()

# 添加测试目录
add_subdirectory(test)

