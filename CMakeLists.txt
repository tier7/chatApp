cmake_minimum_required(VERSION 3.12)
project(chat_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHATAPP_BUILD_GUI "Build the Qt GUI client." ON)

add_executable(chat_server src/server.cpp)
if (CHATAPP_BUILD_GUI)
  find_package(Qt6 COMPONENTS Widgets Network QUIET)
  if (Qt6_FOUND)
    add_executable(chat_client src/client.cpp)
    set_target_properties(chat_client PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
    target_link_libraries(chat_client Qt6::Widgets Qt6::Network)
  else()
    find_package(Qt5 COMPONENTS Widgets Network QUIET)
    if (Qt5_FOUND)
      add_executable(chat_client src/client.cpp)
      set_target_properties(chat_client PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
      target_link_libraries(chat_client Qt5::Widgets Qt5::Network)
    else()
      message(WARNING "Qt5/Qt6 not found; chat_client will not be built. Install Qt or set Qt5_DIR/Qt6_DIR, or disable CHATAPP_BUILD_GUI.")
    endif()
  endif()
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

    set(CHATAPP_MINGW_TARGETS chat_server)
    if (TARGET chat_client)
      list(APPEND CHATAPP_MINGW_TARGETS chat_client)
    endif()

    foreach(target ${CHATAPP_MINGW_TARGETS})
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
