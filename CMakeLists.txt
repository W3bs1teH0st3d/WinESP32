cmake_minimum_required(VERSION 3.16)

# Add extra component directories
set(EXTRA_COMPONENT_DIRS
    "components/lvgl"
    "components/drivers"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(win32_os)
