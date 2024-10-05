cmake_minimum_required(VERSION 3.22)

project(tm-server)

find_package(PkgConfig)

pkg_check_modules(WLROOTS REQUIRED wlroots-0.19)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(${PROJECT_NAME} src/entry.c)

target_compile_options(
  ${PROJECT_NAME}
  PRIVATE
    "$<$<CONFIG:DEBUG>:-g;-Wall;-Wextra;-Wno-missing-field-initializers;${WLROOTS_CFLAGS};-DWLR_USE_UNSTABLE;-lrt>"
)

target_compile_features(${PROJECT_NAME} PUBLIC c_std_17)

target_compile_options(
  ${PROJECT_NAME}
  PUBLIC
    "$<$<CONFIG:RELEASE>:-Ofast;-fno-signed-zeros;-flto;-Wall;-Wextra;-Wno-missing-field-initializers;${WLROOTS_CFLAGS};-DWLR_USE_UNSTABLE>"
)

target_include_directories(
  ${PROJECT_NAME} SYSTEM
  PRIVATE src
  PUBLIC ${WLROOTS_INCLUDE_DIRS})

target_link_libraries(
  ${PROJECT_NAME}
  PUBLIC wayland-server
  PUBLIC xkbcommon
  PUBLIC rt
  PUBLIC ${WLROOTS_LINK_LIBRARIES})
