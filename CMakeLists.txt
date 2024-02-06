cmake_minimum_required(VERSION 3.25)

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

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    gssapi_krb5
    pacparser
)
