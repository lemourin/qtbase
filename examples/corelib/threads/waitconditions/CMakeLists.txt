# Generated from waitconditions.pro.

cmake_minimum_required(VERSION 3.14)
project(waitconditions LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(INSTALL_EXAMPLEDIR "examples/corelib/threads/waitconditions")

find_package(Qt6 COMPONENTS Core)

add_executable(waitconditions
    waitconditions.cpp
)
target_link_libraries(waitconditions PUBLIC
    Qt::Core
)

install(TARGETS waitconditions
    RUNTIME DESTINATION "${INSTALL_EXAMPLEDIR}"
    BUNDLE DESTINATION "${INSTALL_EXAMPLEDIR}"
    LIBRARY DESTINATION "${INSTALL_EXAMPLEDIR}"
)
