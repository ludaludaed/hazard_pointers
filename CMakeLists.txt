cmake_minimum_required(VERSION 3.20)
project(lu_hazard_pointers)

add_library(lu_hazard_pointers INTERFACE)

target_include_directories(lu_hazard_pointers
    INTERFACE
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(lu_hazard_pointers
    INTERFACE
        cxx_std_20
)

add_subdirectory(test)