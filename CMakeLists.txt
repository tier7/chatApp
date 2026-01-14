cmake_minimum_required(VERSION 3.12)
project(chat_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(chat_server src/server.cpp)
add_executable(chat_client src/client.cpp)

if (WIN32)
  target_link_libraries(chat_server ws2_32)
  target_link_libraries(chat_client ws2_32)
  target_compile_definitions(chat_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(chat_client PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
