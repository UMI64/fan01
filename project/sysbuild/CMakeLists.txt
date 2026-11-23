# SPDX-License-Identifier: Apache-2.0

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/husb238)

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sysbuild LANGUAGES)
