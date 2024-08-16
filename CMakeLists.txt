cmake_minimum_required(VERSION 3.0.0)
cmake_minimum_required (VERSION 3.11)
 
# --- Fetch FTXUI --------------------------------------------------------------
include(FetchContent)
 
set(FETCHCONTENT_UPDATES_DISCONNECTED TRUE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  # Important: Specify a GIT_TAG XXXXX here.
  GIT_TAG v4.1.1
)
 
FetchContent_GetProperties(ftxui)
if(NOT ftxui_POPULATED)
  message(STATUS "Fetching FTXUI, This might take a while.")
  FetchContent_Populate(ftxui)
  add_subdirectory(${ftxui_SOURCE_DIR} ${ftxui_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()


project(tjzdspctrl VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

# USE C++17
set(CMAKE_CXX_STANDARD 17)
# enable asan on debug build
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address,undefined -Og -fno-omit-frame-pointer")

add_executable(tjzdspctrl main.cpp)

#hidapi
target_link_libraries(tjzdspctrl PRIVATE hidapi-hidraw)
target_link_libraries(tjzdspctrl
  PRIVATE ftxui::screen
  PRIVATE ftxui::dom
  PRIVATE ftxui::component # Not needed for this example.
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
