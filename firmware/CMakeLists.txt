# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(fcbussim_tuna C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(
	fcbussim_tuna
	main.c
	usb_drv.c
	bus_sim.c
	flash_memory.c
)

pico_set_program_name(fcbussim_tuna "fcbussim_tuna")
pico_set_program_version(fcbussim_tuna "0.1")

pico_enable_stdio_uart(fcbussim_tuna 1)
pico_enable_stdio_usb(fcbussim_tuna 0)

# include directory
include_directories(fcbussim_tuna PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# PIO
pico_generate_pio_header(fcbussim_tuna ${CMAKE_CURRENT_LIST_DIR}/bus_sim.pio)

# Add the standard library to the build
target_link_libraries(
	fcbussim_tuna
	pico_stdlib
	pico_multicore
	tinyusb_device
	tinyusb_board
)

# Add any user requested libraries
target_link_libraries(fcbussim_tuna
	hardware_pio
)

pico_add_extra_outputs(fcbussim_tuna)

