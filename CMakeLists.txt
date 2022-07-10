cmake_minimum_required(VERSION 3.1.0)

project(floodfill_mt)

set(CMAKE_CXX_STANDARD 17)

find_package(Qt5 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(
    floodfill_mt
    main.cpp
    window.cpp
    window.h
    floodfill.cpp
    floodfill.h
    res.qrc
)

target_link_libraries(floodfill_mt PRIVATE Qt5::Widgets Qt5::Concurrent)

set_target_properties(
    floodfill_mt
    PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)