cmake_minimum_required(VERSION 3.21)
project(select_socket_ipc C)

set(CMAKE_C_STANDARD 99)

add_executable(select_socket_ipc main.c)

find_package(Threads REQUIRED)
if(THREADS_HAVE_PTHREAD_ARG)
    set_property(TARGET select_socket_ipc PROPERTY COMPILE_OPTIONS "-pthread")
    set_property(TARGET select_socket_ipc PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
endif()
if(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(select_socket_ipc "${CMAKE_THREAD_LIBS_INIT}")
endif()