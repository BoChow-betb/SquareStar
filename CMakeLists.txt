cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# project() must be a literal call in the top-level CMakeLists.txt. Hiding it
# in an included file makes CMake synthesize project(Project), emits developer
# warnings, and can leave Ninja repeatedly trying to regenerate build.ninja.
project(SquareStar VERSION 1.0.1 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

# Stable root entry point for the rest of the single-executable portable build.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SquareStarMain.cmake")
