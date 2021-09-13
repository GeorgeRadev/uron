CMAKE_MINIMUM_REQUIRED(VERSION 3.4.0)

PROJECT(uron C CXX)

#pthreads required by V8
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
include(GNUInstallDirs)

#V8 is supposed to be build in the ../V8
add_library(libv8_monolith STATIC IMPORTED)
set_target_properties(libv8_monolith PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/../v8/out.gn/x64.debug.sample/obj/libv8_monolith.a")
set_target_properties(libv8_monolith PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../v8/include")

# add Postgres SQL
# apt install libpq-dev
find_package(PostgreSQL REQUIRED)

# add src and include folder
include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB ALL_INCS "${PROJECT_SOURCE_DIR}/include/*.*")
include_directories(${PROJECT_SOURCE_DIR}/src)
file(GLOB ALL_SRCS "${PROJECT_SOURCE_DIR}/src/*.*")
add_executable(uron ${ALL_SRCS} ${ALL_INCS})

# link libiraries
target_link_libraries(uron libv8_monolith Threads::Threads ${CMAKE_DL_LIBS} PostgreSQL::PostgreSQL -luuid)