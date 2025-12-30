cmake_minimum_required(VERSION 3.10)
project(milk-streamtelemetry-resample C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# Find CFITSIO
find_path(CFITSIO_INCLUDE_DIR fitsio.h HINTS ${CFITSIO_ROOT}/include /usr/include /usr/local/include /opt/local/include)
find_library(CFITSIO_LIBRARY cfitsio HINTS ${CFITSIO_ROOT}/lib /usr/lib /usr/local/lib /opt/local/lib)

if (CFITSIO_INCLUDE_DIR AND CFITSIO_LIBRARY)
    message(STATUS "Found CFITSIO include: ${CFITSIO_INCLUDE_DIR}")
    message(STATUS "Found CFITSIO library: ${CFITSIO_LIBRARY}")
    include_directories(${CFITSIO_INCLUDE_DIR})
else()
    message(FATAL_ERROR "CFITSIO not found. Please install cfitsio or set CFITSIO_ROOT environment variable.")
endif()

add_executable(milk-streamtelemetry-resample src/main.c)
target_link_libraries(milk-streamtelemetry-resample ${CFITSIO_LIBRARY} m)

install(TARGETS milk-streamtelemetry-resample DESTINATION bin)
