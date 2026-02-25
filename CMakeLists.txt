file(GLOB HELPER_SOURCES "helpers/*.cc")
file(GLOB source "*.cc")

add_library(
  ghostdag-helpers
  ${HELPER_SOURCES}
)

build_exec(
  EXECNAME ghostdagsim
  SOURCE_FILES ${source}
  LIBRARIES_TO_LINK ghostdag-helpers
                    ${libcore}
                    ${ns3-libs}
                    ${ns3-contrib-libs}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/ghostdagsim
)
