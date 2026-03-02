file(GLOB HELPER_SOURCES "helpers/*.cc")
file(GLOB C_SOURCES "*.cc")

add_library(
  ghostdag-helpers
  ${HELPER_SOURCES}
)

option(GHOSTDAGSIM_METRICS "Enable metrics" OFF)
if(GHOSTDAGSIM_METRICS)
  message(STATUS "Enabled to build ghostdagsim metrics")
  add_compile_definitions(GHOSTDAGSIM_METRICS)
endif()

build_exec(
  EXECNAME ghostdagsim
  SOURCE_FILES ${C_SOURCES}
  LIBRARIES_TO_LINK ghostdag-helpers
  ${libcore}
  ${ns3-libs}
  ${ns3-contrib-libs}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/ghostdagsim
)


option(GHOSTDAGSIM_TESTS "Enable ghostdagsim build tests" OFF)
if(GHOSTDAGSIM_TESTS)
  find_package(GTest QUIET)

  if(GTest_FOUND)
    set(GTEST_LIBRARIES GTest::GTest GTest::Main)
  else()
    set(GTEST_LIBRARIES)
  endif()
  message(STATUS "Enabled to build ghostdagsim tests")

  set(C_SOURCES_NO_MAIN ${C_SOURCES})
  list(FILTER C_SOURCES_NO_MAIN EXCLUDE REGEX ".*/main\\.cc$")

  build_exec(
    EXECNAME ghostdagtest
    SOURCE_FILES tests/ghostdagtest.cc ${C_SOURCES_NO_MAIN}
    LIBRARIES_TO_LINK ghostdag-helpers
    ${libcore}
    ${ns3-libs}
    ${ns3-contrib-libs}
    ${GTEST_LIBRARIES}
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/ghostdagsim
  )

  build_exec(
    EXECNAME mempooltest
    SOURCE_FILES tests/mempooltest.cc ${C_SOURCES_NO_MAIN}
    LIBRARIES_TO_LINK ghostdag-helpers
    ${libcore}
    ${ns3-libs}
    ${ns3-contrib-libs}
    ${GTEST_LIBRARIES}
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/ghostdagsim
  )
endif()
