cmake_minimum_required(VERSION 3.10)
project(midi2uge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add all midifile sources
file(GLOB MIDIFILE_SRC
    ${CMAKE_SOURCE_DIR}/third_party/midifile/src/*.cpp
)

add_executable(midi2uge
    src/main.cpp
    src/midi2uge.cpp
    src/uge_writer.cpp
    ${MIDIFILE_SRC}
)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/midifile/include
    src
)

# nlohmann_json.hpp is now present in the project root and can be included in uge2json.cpp as #include "nlohmann_json.hpp"

add_executable(uge2json src/uge2json.cpp)

# Gather midifile sources
file(GLOB MIDIFILE_SRC
    "third_party/midifile/src/*.cpp"
)

add_executable(midi2json src/midi2json.cpp ${MIDIFILE_SRC})
target_include_directories(midi2json PRIVATE src third_party/midifile/include)
