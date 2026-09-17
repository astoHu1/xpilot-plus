cmake_minimum_required(VERSION 3.21)
project(afv_wrapper_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CLIENT "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(afv_wrapper_tests
    wrapper_tests.cpp
    ${CLIENT}/src/audio/afv.cpp ${CLIENT}/src/audio/afv.h
    ${CLIENT}/src/config/appconfig.cpp ${CLIENT}/src/config/appconfig.h
    ${CLIENT}/src/common/simplecrypt.cpp
    ${CLIENT}/src/common/enums.h
    ${CLIENT}/src/config/windowconfig.h
    ${CLIENT}/src/network/connectinfo.h
    ${CLIENT}/src/aircrafts/radio_stack_state.h
    ${CLIENT}/src/aircrafts/user_aircraft_data.h
    ${CLIENT}/src/controllers/controller.h
    stubs/simulator/xplane_adapter.h
    stubs/network/networkmanager.h
    stubs/controllers/controller_manager.h)
target_include_directories(afv_wrapper_tests PRIVATE stubs ${CLIENT}/src
    ${CLIENT}/afv-native/include ${CLIENT}/../dependencies/qtpromise/include)
target_compile_definitions(afv_wrapper_tests PRIVATE CLIENT_SOURCE_DIR="${CLIENT}")
target_link_libraries(afv_wrapper_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::Test)
if(WIN32)
    target_link_libraries(afv_wrapper_tests PRIVATE ws2_32)
endif()
enable_testing()
add_test(NAME afv_wrapper_offline COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_LIST_DIR}/run_isolated.py $<TARGET_FILE:afv_wrapper_tests>)
set_tests_properties(afv_wrapper_offline PROPERTIES TIMEOUT 60)
