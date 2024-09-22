cmake_minimum_required(VERSION 3.20)
project(lu_hazard_pointers)

set(CMAKE_CXX_STANDARD 20)

add_library(lu_hazard_pointers INTERFACE)
set(LU_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
target_include_directories(lu_hazard_pointers INTERFACE ${LU_INCLUDE_DIR})
target_compile_features(lu_hazard_pointers INTERFACE cxx_std_20)

add_subdirectory(test)