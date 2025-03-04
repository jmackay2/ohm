find_package(GLM)
find_package(ZLIB)

set(SOURCES
  ohm2ply.cpp
)

add_executable(ohm2ply ${SOURCES})
leak_track_target_enable(ohm2ply CONDITION OHM_LEAK_TRACK)

set_target_properties(ohm2ply PROPERTIES FOLDER utils)
if(MSVC)
  set_target_properties(ohm2ply PROPERTIES DEBUG_POSTFIX "d")
endif(MSVC)

target_include_directories(ohm2ply SYSTEM
  PRIVATE
    "${GLM_INCLUDE_DIR}"
    "${ZLIB_INCLUDE_DIRS}"
)

target_link_libraries(ohm2ply PUBLIC ohm ohmheightmap ohmtools ohmutil ${ZLIB_LIBRARIES})

clang_tidy_target(ohm2ply)

source_group("source" REGULAR_EXPRESSION ".*$")
# Needs CMake 3.8+:
# source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" PREFIX source FILES ${SOURCES})

install(TARGETS ohm2ply DESTINATION bin)
