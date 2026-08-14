cmake_minimum_required(VERSION 3.16)

project(vatsim-auth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
endif()

if(WIN32)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

add_library(vatsim-auth STATIC src/vatsimauth.cpp)
