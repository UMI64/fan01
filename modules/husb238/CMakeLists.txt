# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_HUSB238_DRIVER husb238.c)
zephyr_include_directories(include)
