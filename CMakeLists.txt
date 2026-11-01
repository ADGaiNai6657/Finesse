cmake_minimum_required(VERSION 4.2)
project(Finesse)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FINESSE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
include(CTest)

set(FINESSE_SOURCES
    src/Common/Types.cpp
    src/GearHash/GearHash.cpp
    src/BreakingApart/BreakingApart.cpp
    src/BreakingApart/Chunker.cpp
    src/Finesse/Finesse.cpp
    src/NTransform/NTransform.cpp
    src/SuperFeature/SuperFeature.cpp
    src/Pipeline/Pipeline.cpp
    src/Experiment/IO.cpp
    src/Experiment/Input.cpp
    src/Experiment/Experiment.cpp)

# 配置时保存源码快照，任何输入变化触发重配置，使二进制中的摘要对应本次构建。
file(GLOB_RECURSE SNAPSHOT_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "src/*.h" "src/*.cpp" "tests/*.cpp" "tests/*.py" "scripts/*.py" "cmake/*.in"
    "configs/*" "docs/*.md")
list(APPEND SNAPSHOT_FILES CMakeLists.txt README.md)
list(SORT SNAPSHOT_FILES)
set(SOURCE_FILES_CSV "file,sha256\n")
foreach(FILE IN LISTS SNAPSHOT_FILES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${FILE}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${FILE}")
        file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${FILE}" FILE_HASH)
        string(APPEND SOURCE_FILES_CSV "${FILE},${FILE_HASH}\n")
    endif()
endforeach()
string(SHA256 SOURCE_DIGEST "${SOURCE_FILES_CSV}")
set(CODE_REVISION "no-git; use sourceDigest")
set(SOURCE_STATE "source-files.csv records exact build inputs")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE CODE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(GIT_STATUS)
        set(SOURCE_STATE "dirty; see source-files.csv")
    else()
        set(SOURCE_STATE "clean; see source-files.csv")
    endif()
endif()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/configs/parameters-v1.csv" PARAMETER_FILE_SHA256)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/src/GearHash/GearHash.h" GEAR_TABLE_SHA256)
set(COMPILER_INFO "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}; ${CMAKE_CXX_COMPILER}")
set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")
string(TOUPPER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_UPPER)
set(BUILD_OPTIONS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${BUILD_TYPE_UPPER}}; C++20 extensions=OFF; warnings=-Wall -Wextra -Wpedantic; sanitizers=${FINESSE_SANITIZERS}; LTO=OFF")
configure_file(cmake/BuildInfo.h.in generated/BuildInfo.h @ONLY)

add_library(finesse_core STATIC ${FINESSE_SOURCES})
target_include_directories(finesse_core PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(finesse_core PUBLIC OpenSSL::Crypto)
target_compile_options(finesse_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(finesse_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION FALSE)
if(FINESSE_SANITIZERS)
    target_compile_options(finesse_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(finesse_core PUBLIC -fsanitize=address,undefined)
endif()
add_executable(Finesse src/main.cpp)
target_link_libraries(Finesse PRIVATE finesse_core)
target_compile_options(Finesse PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(finesse_tests tests/CoreTests.cpp)
    target_link_libraries(finesse_tests PRIVATE finesse_core)
    target_compile_options(finesse_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME core COMMAND finesse_tests)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME experiment COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/ExperimentTests.py" "$<TARGET_FILE:Finesse>")
endif()
