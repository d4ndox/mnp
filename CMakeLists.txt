#-DCMAKE_BUILD_TYPE=Debug
#-DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.0)

project(mnp C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-D_GNU_SOURCE)

find_package(CURL REQUIRED)

include_directories(
    ${CURL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/inih
    ${CMAKE_CURRENT_SOURCE_DIR}/cjson
)

set(MNP_SOURCES
    main.c
    init.c
    cleanup.c
    monitor.c
    spend_proof.c
    tx_proof.c
    payment.c
    balance.c
    bc_height.c
    rpc_call.c
    validate.c
    wallet.c
    inih/ini.c
    cjson/cJSON.c
)

set(MNP_HEADERS
    init.h
    cleanup.h
    monitor.h
    spend_proof.h
    tx_proof.h
    payment.h
    balance.h
    bc_height.h
    rpc_call.h
    validate.h
    wallet.h
    globaldefs.h
    inih/ini.h
    cjson/cJSON.h
)

add_executable(
    mnp
    ${MNP_SOURCES}
    ${MNP_HEADERS}
)

target_link_libraries(
    mnp
    ${CURL_LIBRARIES}
)

include(CTest)
if(BUILD_TESTING)
    add_executable(monitor_confirmations_test
        tests/monitor_confirmations_test.c
        cjson/cJSON.c
        inih/ini.c
        validate.c
    )
    target_compile_options(monitor_confirmations_test PRIVATE -UNDEBUG)
    add_test(NAME monitor_confirmations COMMAND monitor_confirmations_test)
endif()

install(
    FILES .mnp.ini
    DESTINATION ~
    COMPONENT config
)

install(
    TARGETS mnp
    DESTINATION bin
    COMPONENT binaries
)
