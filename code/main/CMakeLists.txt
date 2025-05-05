file(GLOB_RECURSE SOURCES *.cc devices/*.cc ui/*.cc)
idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS "." "./devices" "./ui")