# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../modules/husb238)
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fan01)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_sources(app PRIVATE
	src/main.c
	src/fan_ctrl.c
	src/pd_manager.c
	src/display_manager.c
	src/input_manager.c
	src/settings_manager.c
	src/wifi_manager.c
	src/web_server.c
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_web_service KVMA RAM_REGION GROUP RODATA_REGION)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)
generate_inc_file_for_target(app
	${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
	${gen_dir}/index.html.gz.inc
	--gzip
)
