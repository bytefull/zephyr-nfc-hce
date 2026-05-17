# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zephyr_nfc_hce)

if (CONFIG_NETWORKING)
  include(${ZEPHYR_BASE}/samples/net/common/common.cmake)
endif()

target_sources(app PRIVATE
    # src/main.c
    src/web.c
    # src/socket_server.c
)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(NAME http_resource_desc_my_service
                      KVMA RAM_REGION GROUP RODATA_REGION)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

set(source_file_index src/index.html)
generate_inc_file_for_target(app ${source_file_index} ${gen_dir}/index.html.gz.inc --gzip)

set(source_file_bootstrap_min_css src/bootstrap.min.css)
generate_inc_file_for_target(app ${source_file_bootstrap_min_css} ${gen_dir}/bootstrap.min.css.gz.inc --gzip)

set(source_file_style src/style.css)
generate_inc_file_for_target(app ${source_file_style} ${gen_dir}/style.css.gz.inc --gzip)

set(source_file_script src/script.js)
generate_inc_file_for_target(app ${source_file_script} ${gen_dir}/script.js.gz.inc --gzip)
