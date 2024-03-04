#-DCMAKE_BUILD_TYPE=Debug
#-DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.0)

project(mnp)
add_definitions(-std=c99 -Wall)

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

#stndup warning
set (CMAKE_C_FLAGS "-D_GNU_SOURCE")
set(HEADER_FILES ../inih/ini.h ../cjson/cJSON.h ../wallet.h ../rpc_call.h ../version.h ../bc_height.h ../balance.h ../payments.h ../delquotes.h ../globaldefs.h)
add_executable(mnp ../mnp.c ../inih/ini.c ../cjson/cJSON.c ../rpc_call.c ../version.c ../bc_height.c ../balance.c ../payments.c ../delquotes.c ../wallet.c ${HEADER_FILES})
add_executable(mnp-payment ../mnp-payment.c ../inih/ini.c ../cjson/cJSON.c ../rpc_call.c ../delquotes.c ../wallet.c ${HEADER_FILES})

target_link_libraries (mnp curl)
target_link_libraries (mnp-payment curl)
install(FILES .mnp.ini DESTINATION ~ COMPONENT config)
install(TARGETS mnp mnp-payment DESTINATION bin COMPONENT binaries)
#INSTALL_TARGETS(/bin mnp)
