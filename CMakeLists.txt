cmake_minimum_required(VERSION 3.18)

project(krbproxy)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRCS
    src/main.cpp
    src/ProxyAuth.cpp
    src/ProxyFinder.cpp
    src/ProxyServer.cpp
)

add_executable(${CMAKE_PROJECT_NAME} ${SRCS})

find_file(asio_h boost/asio.hpp REQUIRED)
find_file(pacparser_h pacparser.h REQUIRED)
find_library(pacparser_lib pacparser REQUIRED)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    gssapi_krb5
    pacparser
)

if(${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE pthread)
endif()
