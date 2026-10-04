cmake_minimum_required(VERSION 3.31)
project(OurScheme)

set(CMAKE_CXX_STANDARD 23)

file(GLOB OURC_SOURCE_FILES src/*.cpp src/*.hpp)
add_executable(${PROJECT_NAME} ${OURC_SOURCE_FILES})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options("-Wall" "-Wpedantic" "-Werror" "-Wextra")
endif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")

if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_link_libraries(${PROJECT_NAME} PRIVATE "-lstdc++exp")
endif(WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
