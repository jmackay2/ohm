include(GenerateExportHeader)

find_package(GLM)

set(SOURCES
  miniply/miniply.cpp
  miniply/miniply.h
  DataChannel.h
  PointCloudReader.cpp
  PointCloudReader.h
  PointCloudReaderMiniPly.cpp
  PointCloudReaderMiniPly.h
  PointCloudReaderTraj.cpp
  PointCloudReaderTraj.h
  Points.cpp
  Points.h
  SlamCloudLoader.cpp
  SlamCloudLoader.h
  SlamIO.cpp
  SlamIO.h
  "${CMAKE_CURRENT_BINARY_DIR}/slamio/SlamIOConfig.h"
  "${CMAKE_CURRENT_BINARY_DIR}/slamio/SlamIOExport.h"
)

set(SLAMIO_HAVE_PDAL 0)
set(SLAMIO_HAVE_PDAL_STREAMS 0)
if(WITH_PDAL)
  find_package(PDAL REQUIRED)
  set(SLAMIO_HAVE_PDAL 1)
  list(APPEND SOURCES
    PointCloudReaderPdal.cpp
    PointCloudReaderPdal.h
  )
  # Need to check PDAL version. We streaming requires PDAL version 1.7+
  if(PDAL_VERSION VERSION_GREATER_EQUAL 1.7)
    set(SLAMIO_HAVE_PDAL_STREAMS 1)
    list(APPEND SOURCES
      pdal/PointStream.cpp
      pdal/PointStream.h
      PointCloudReaderPdal.cpp
      PointCloudReaderPdal.h
    )
  endif(PDAL_VERSION VERSION_GREATER_EQUAL 1.7)
endif(WITH_PDAL)

configure_file(SlamIOConfig.in.h "${CMAKE_CURRENT_BINARY_DIR}/slamio/SlamIOConfig.h")

set(PUBLIC_HEADERS
  DataChannel.h
  PointCloudReader.h
  Points.h
  SlamCloudLoader.h
  SlamIO.h
  "${CMAKE_CURRENT_BINARY_DIR}/slamio/SlamIOConfig.h"
  "${CMAKE_CURRENT_BINARY_DIR}/slamio/SlamIOExport.h"
)

add_library(slamio ${SOURCES})
# More things pointing to the need for a refactor here
if(MSVC)
  target_compile_definitions(slamio PUBLIC NOMINMAX _USE_MATH_DEFINES)
endif(MSVC)

clang_tidy_target(slamio)

generate_export_header(slamio
      EXPORT_MACRO_NAME slamio_API
      EXPORT_FILE_NAME slamio/SlamIOExport.h
      STATIC_DEFINE slamio_STATIC)

target_include_directories(slamio
  PUBLIC
    $<INSTALL_INTERFACE:${OHM_PREFIX_INCLUDE}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/slamio>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

target_include_directories(slamio SYSTEM
  PUBLIC "${GLM_INCLUDE_DIR}"
)

target_link_libraries(slamio PUBLIC ohmutil)

if(WITH_PDAL)
  target_link_libraries(slamio PUBLIC ${PDAL_LIBRARIES})
endif(WITH_PDAL)

install(TARGETS slamio EXPORT ${CMAKE_PROJECT_NAME}-config-targets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION ${OHM_PREFIX_INCLUDE}/slamio
)

install(FILES ${PUBLIC_HEADERS} DESTINATION ${OHM_PREFIX_INCLUDE}/slamio)

source_group("source" REGULAR_EXPRESSION ".*$")
# Needs CMake 3.8+:
# source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" PREFIX source FILES ${SOURCES})
