project(test)

set(CMAKE_CXX_STANDARD 20)

# if (CMAKE_BUILD_TYPE STREQUAL "Debug")
#     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
#     set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
# endif()

add_executable(test main.cpp)
target_link_libraries(test PRIVATE lu_hazard_pointers)
