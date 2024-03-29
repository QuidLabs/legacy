cmake_minimum_required(VERSION 3.5)

project(vigor_contracts)

# set(EOSIO_WASM_OLD_BEHAVIOR "Off")


set(VERSION_MAJOR 1)
set(VERSION_MINOR 8)
set(VERSION_PATCH 3)
#set(VERSION_SUFFIX develop)

if (VERSION_SUFFIX)
   set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${VERSION_SUFFIX}")
else()
   set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
endif()

include(ExternalProject)

find_package(eosio.cdt)

message(STATUS "Building eosio.contracts v${VERSION_FULL}")

set(EOSIO_CDT_VERSION_MIN "1.6")
set(EOSIO_CDT_VERSION_SOFT_MAX "1.7")
#set(EOSIO_CDT_VERSION_HARD_MAX "")

### Check the version of eosio.cdt
set(VERSION_MATCH_ERROR_MSG "")
EOSIO_CHECK_VERSION(VERSION_OUTPUT "${EOSIO_CDT_VERSION}"
                                   "${EOSIO_CDT_VERSION_MIN}"
                                   "${EOSIO_CDT_VERSION_SOFT_MAX}"
                                   "${EOSIO_CDT_VERSION_HARD_MAX}"
                                   VERSION_MATCH_ERROR_MSG)
if(VERSION_OUTPUT STREQUAL "MATCH")
   message(STATUS "Using eosio.cdt version ${EOSIO_CDT_VERSION}")
elseif(VERSION_OUTPUT STREQUAL "WARN")
   message(WARNING "Using eosio.cdt version ${EOSIO_CDT_VERSION} even though it exceeds the maximum supported version of ${EOSIO_CDT_VERSION_SOFT_MAX}; continuing with configuration, however build may fail.\nIt is recommended to use eosio.cdt version ${EOSIO_CDT_VERSION_SOFT_MAX}.x")
else() # INVALID OR MISMATCH
   message(FATAL_ERROR "Found eosio.cdt version ${EOSIO_CDT_VERSION} but it does not satisfy version requirements: ${VERSION_MATCH_ERROR_MSG}\nPlease use eosio.cdt version ${EOSIO_CDT_VERSION_SOFT_MAX}.x")
endif(VERSION_OUTPUT STREQUAL "MATCH")


add_subdirectory(contracts)

set(EOSIO_CONTRACTS_PATH ${CMAKE_SOURCE_DIR}/tests/eosio_contracts)
set(VIGOR_CONTRACTS_PATH ${CMAKE_SOURCE_DIR}/build/usr/local/contracts)
set(EXPECTED_RESULTS_PATH ${CMAKE_SOURCE_DIR}/tests/expected)

if(CONFIGURE_TESTS)
   message("configuring tests")
   configure_file(${CMAKE_SOURCE_DIR}/tests/config/config.json.in ${CMAKE_SOURCE_DIR}/tests/config/config.json @ONLY)
endif()



