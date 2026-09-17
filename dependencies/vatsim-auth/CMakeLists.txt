cmake_minimum_required(VERSION 3.16)

project(vatsim-auth LANGUAGES CXX)

# This public checkout is deliberately incapable of VATSIM authentication.
option(XPILOT_ALLOW_AUTH_STUB "Allow development builds that cannot authenticate to VATSIM" OFF)
if(NOT XPILOT_ALLOW_AUTH_STUB)
    message(FATAL_ERROR "The public vatsim-auth stub cannot be released. Supply the real auth library, or explicitly set XPILOT_ALLOW_AUTH_STUB=ON for an unverified development build.")
endif()
message(WARNING "UNVERIFIED DEVELOPMENT BUILD: vatsim-auth is a stub; VATSIM authentication will not work.")

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
