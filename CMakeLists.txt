#-DCMAKE_BUILD_TYPE=Debug
#-DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.0)

project(mnp)
add_definitions(-std=c99)

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})

#stndup warning
set (CMAKE_C_FLAGS "-D_GNU_SOURCE")
set(HEADER_FILES ../inih/ini.h ../cjson/cJSON.h ../wallet.h ../rpc_call.h ../delquotes.h ../validate.h ../globaldefs.h)
add_executable(mnp ../mnp.c ../inih/ini.c ../cjson/cJSON.c ../rpc_call.c ../delquotes.c ../validate.c ../wallet.c ${HEADER_FILES})
add_executable(mnpd ../mnpd.c ../inih/ini.c ../cjson/cJSON.c ../rpc_call.c ../delquotes.c ../wallet.c ${HEADER_FILES})
add_executable(mnp-payment ../mnp-payment.c ../inih/ini.c ../cjson/cJSON.c ../rpc_call.c ../delquotes.c ../validate.c ../wallet.c ${HEADER_FILES})

target_link_libraries (mnp curl)
target_link_libraries (mnpd curl)
target_link_libraries (mnp-payment curl)
install(FILES .mnp.ini DESTINATION ~ COMPONENT config)
install(TARGETS mnp mnpd mnp-payment DESTINATION bin COMPONENT binaries)
