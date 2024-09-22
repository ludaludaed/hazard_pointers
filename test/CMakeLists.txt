project(test)

set(CMAKE_CXX_STANDARD 20)

add_executable(test main.cpp)
target_link_libraries(test PRIVATE lu_hazard_pointers)
