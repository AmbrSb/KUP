# BSD 3-Clause License
# 
# Copyright (c) 2020, Amin Saba
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(kuplib VERSION 0.0.1 DESCRIPTION "KUP user space library")

set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_C_STANDARD 11)

include(InstallRequiredSystemLibraries)
include(GNUInstallDirs)
include(CTest)

find_package(Git)
find_package(Threads)

find_program(MAKE_EXE NAMES make)
find_program(GIT_EXE NAMES git)

if(${PROJECT_NAME}_ENABLE_DEBUG)
        message(WARNING "Debug messages are enabled.")
        add_definitions(-DPROXY_DEBUG)
endif()

enable_testing()

add_compile_options(
        -Wall -Wpedantic -fexceptions
        -Wno-gnu-statement-expression
        "$<$<CONFIG:Debug>:-O0;-g3;-ggdb>"
        "$<$<CONFIG:Release>:-O2>"
)

if(GIT_FOUND AND NOT EXISTS "${CMAKE_SOURCE_DIR}/.git")
    message(WARNING "Configre Git!")
elseif(GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --max-count=1 HEAD
            OUTPUT_VARIABLE GIT_REVISION
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            ERROR_QUIET
    )
    if(NOT ${GIT_REVISION} STREQUAL "")
        string(STRIP ${GIT_REVISION} GIT_REVISION)
    endif()
endif()

add_compile_definitions(
        FORTIFY_SOURCE=2
        "$<$<CONFIG:Debug>:MALLOC_CHECK_=3;_GLIBCXX_DEBUG>"
)

include_directories(/usr/local/include include)
link_directories(/usr/local/lib)

add_library(kup SHARED
            ${PROJECT_SOURCE_DIR}/kuplib.h
            ${PROJECT_SOURCE_DIR}/kuplib.c)
target_link_libraries(kup util)

include(CPack)
