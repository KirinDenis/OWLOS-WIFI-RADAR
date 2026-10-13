cmake_minimum_required(VERSION 3.16)

# core/ = everything that doesnt touch hardware (radar logic, fonts, drawing),
# the PC preview in tools/preview builds the same files
# NOTE: flash is 40 MHz in sdkconfig.defaults, at 80 MHz the CYD hangs
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../core")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# which screen, see core/config.h. BUILD.BAT passes it, 1 (the 320x240 CYD) if nobody does
if(NOT DEFINED RADAR_DISPLAY)
    set(RADAR_DISPLAY 1)
endif()
idf_build_set_property(COMPILE_DEFINITIONS "RADAR_DISPLAY=${RADAR_DISPLAY}" APPEND)
project(owlos-wifi-radar)
