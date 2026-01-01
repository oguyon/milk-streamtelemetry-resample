cmake_minimum_required(VERSION 3.10)
project(milk-streamtelemetry-resample-mkts C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# Find CFITSIO
find_path(CFITSIO_INCLUDE_DIR fitsio.h HINTS ${CFITSIO_ROOT}/include /usr/include /usr/local/include /opt/local/include /usr/include/cfitsio)
find_library(CFITSIO_LIBRARY cfitsio HINTS ${CFITSIO_ROOT}/lib /usr/lib /usr/local/lib /opt/local/lib)

if (CFITSIO_INCLUDE_DIR AND CFITSIO_LIBRARY)
    message(STATUS "Found CFITSIO include: ${CFITSIO_INCLUDE_DIR}")
    message(STATUS "Found CFITSIO library: ${CFITSIO_LIBRARY}")
else()
    message(FATAL_ERROR "CFITSIO not found. Please install cfitsio or set CFITSIO_ROOT environment variable.")
endif()

# First executable: mkts
add_executable(milk-streamtelemetry-resample-mkts src/main.c)
target_include_directories(milk-streamtelemetry-resample-mkts PRIVATE ${CFITSIO_INCLUDE_DIR})
target_link_libraries(milk-streamtelemetry-resample-mkts ${CFITSIO_LIBRARY} m)

# Second executable: applyts
add_executable(milk-streamtelemetry-resample-applyts src/applyts.c)
target_include_directories(milk-streamtelemetry-resample-applyts PRIVATE ${CFITSIO_INCLUDE_DIR})
target_link_libraries(milk-streamtelemetry-resample-applyts ${CFITSIO_LIBRARY} m)

install(TARGETS milk-streamtelemetry-resample-mkts milk-streamtelemetry-resample-applyts DESTINATION bin)
