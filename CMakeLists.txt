cmake_minimum_required(VERSION 3.12)
project(chat_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(chat_server src/server.cpp)
add_executable(chat_client src/client.cpp)
