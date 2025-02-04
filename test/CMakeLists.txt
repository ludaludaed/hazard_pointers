project(test)

set(CMAKE_CXX_STANDARD 20)

add_executable(test)

target_sources(test
    PRIVATE
        main.cpp
)

target_link_libraries(test PRIVATE lu_hazard_pointers)
