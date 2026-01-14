cmake_minimum_required(VERSION 3.12)
project(chat_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(CHATAPP_BUILD_GUI "Build the Qt GUI client." ON)

add_executable(chat_server src/server.cpp)
if (CHATAPP_BUILD_GUI)
  add_executable(chat_client src/client.cpp)

  find_package(Qt6 COMPONENTS Widgets Network QUIET)
  if (Qt6_FOUND)
    target_link_libraries(chat_client Qt6::Widgets Qt6::Network)
  else()
    find_package(Qt5 COMPONENTS Widgets Network QUIET)
    if (Qt5_FOUND)
      target_link_libraries(chat_client Qt5::Widgets Qt5::Network)
    else()
      message(WARNING "Qt5/Qt6 not found; chat_client will not be built. Install Qt or set Qt5_DIR/Qt6_DIR, or disable CHATAPP_BUILD_GUI.")
      set_target_properties(chat_client PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
    endif()
  endif()
endif()

find_package(Qt6 COMPONENTS Widgets Network QUIET)
if (Qt6_FOUND)
  target_link_libraries(chat_client Qt6::Widgets Qt6::Network)
else()
  find_package(Qt5 COMPONENTS Widgets Network QUIET)
  if (Qt5_FOUND)
    target_link_libraries(chat_client Qt5::Widgets Qt5::Network)
  else()
    message(WARNING "Qt5/Qt6 not found; chat_client will not be built. Install Qt or set Qt5_DIR/Qt6_DIR.")
    set_target_properties(chat_client PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
  endif()
  find_package(Qt5 COMPONENTS Widgets Network REQUIRED)
  target_link_libraries(chat_client Qt5::Widgets Qt5::Network)
endif()

if (WIN32)
  target_link_libraries(chat_server ws2_32)
  target_compile_definitions(chat_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if (WIN32 AND MINGW)
  option(CHATAPP_STATIC_MINGW_RUNTIME "Link MinGW libgcc/libstdc++ statically." OFF)
  option(CHATAPP_COPY_MINGW_DLLS "Copy MinGW runtime DLLs next to the executables." OFF)

  if (CHATAPP_STATIC_MINGW_RUNTIME)
    target_link_options(chat_server PRIVATE -static-libgcc -static-libstdc++)
  endif()

  if (CHATAPP_COPY_MINGW_DLLS)
    find_file(MINGW_LIBGCC_DLL libgcc_s_seh-1.dll PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
    find_file(MINGW_LIBSTDCPP_DLL libstdc++-6.dll PATHS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})

    foreach(target chat_server chat_client)
      if (MINGW_LIBGCC_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${MINGW_LIBGCC_DLL}"
          "$<TARGET_FILE_DIR:${target}>")
      endif()
      if (MINGW_LIBSTDCPP_DLL)
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${MINGW_LIBSTDCPP_DLL}"
          "$<TARGET_FILE_DIR:${target}>")
      endif()
    endforeach()
  endif()
endif()
