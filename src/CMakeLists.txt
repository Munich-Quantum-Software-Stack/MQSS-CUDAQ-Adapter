# Copyright (c) 2024 - 2026 MQSS Project
# All rights reserved.
#
# Licensed under the Apache License v2.0 with LLVM Exceptions (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# https://llvm.org/LICENSE.txt
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(LIBRARY_NAME mqss-adapter)

add_library(${LIBRARY_NAME} SHARED MQSSQPU.cpp)

target_include_directories(
  ${LIBRARY_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src> $<INSTALL_INTERFACE:include>
                         ${CUDAQ_INCLUDE_DIR} ${MLIR_INCLUDE_DIRS})

target_link_libraries(
  ${LIBRARY_NAME}
  PUBLIC cudaq::cudaq-operator cudaq::cudaq-common
  PRIVATE cudaq::cudaq-logger
          fmt::fmt-header-only
          cudaq::cudaq
          mqss_client
          MLIRIR
          MLIRParser
          MLIRSupport
          Threads::Threads)

install(TARGETS mqss-adapter LIBRARY DESTINATION ${CUDAQ_LIBRARY_DIR})

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/mqss.yml DESTINATION ${CUDAQ_INSTALL_DIR}/targets)

set_target_properties(
  mqss-adapter
  PROPERTIES BUILD_RPATH "${CUDAQ_LIBRARY_DIR}"
             INSTALL_RPATH "${CUDAQ_LIBRARY_DIR}"
             POSITION_INDEPENDENT_CODE ON)
