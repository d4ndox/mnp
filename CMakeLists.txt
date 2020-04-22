#-DCMAKE_BUILD_TYPE=Debug
#-DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 2.8.11.2)

project(mnp)
add_definitions(-std=c99)

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

#stndup warning
set (CMAKE_C_FLAGS "-D_GNU_SOURCE")
set(HEADER_FILES ../inih/ini.h ../cjson/cJSON.h ../wallet.h ../rpc_call ../globaldefs.h)
add_executable(mnp ../mnp.c ../inih/ini.c ../cjson/cJSON.c ../rpc_call ../wallet.c ${HEADER_FILES})

target_link_libraries (mnp curl)
install(FILES .mnp.ini DESTINATION ~ COMPONENT config)
install(TARGETS mnp DESTINATION bin COMPONENT binaries)
#INSTALL_TARGETS(/bin mnp)
