cmake_minimum_required(VERSION 3.12)

set(target_name Pico-RJ45-Sock_spdif)

include(pico_sdk_import.cmake)
project(${target_name})

pico_sdk_init()

add_executable(${target_name})

pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/ser_10base_t.pio)
pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/des_10base_t.pio)
pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_BINARY_DIR}/spdif_rx_48000.pio)
pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_BINARY_DIR}/spdif_rx_96000.pio)
pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_BINARY_DIR}/spdif_rx_192000.pio)
pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/spdif_rx_capture.pio)

add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/spdif_rx_48000.pio
        ${CMAKE_CURRENT_BINARY_DIR}/spdif_rx_96000.pio
        ${CMAKE_CURRENT_BINARY_DIR}/spdif_rx_192000.pio
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/spdif_rx.pio
    COMMAND "python"
        ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_pio_by_freq.py
        ${CMAKE_CURRENT_LIST_DIR}/spdif_rx.pio
        ${CMAKE_CURRENT_BINARY_DIR}
        48000
        96000
        192000
    )
 
target_sources(${target_name} PRIVATE
    main.c
    hwinit.c
    eth.c
    udp.c
    arp.c
    icmp.c
    spdif_rx.c
    rx890x_i2c.c
    )

target_link_libraries(${target_name} PRIVATE
    pico_stdlib
    hardware_pio
    hardware_irq
    hardware_dma
    hardware_i2c
    pico_multicore
    )

# enable usb output, disable uart output
pico_enable_stdio_usb(${target_name} 1)
pico_enable_stdio_uart(${target_name} 0)

# Default binaries to Copy code to RAM when booting from flash.
# Code execution speed may improve, but code size is limited by RAM capacity.
pico_set_binary_type(${target_name} copy_to_ram)

pico_add_extra_outputs(${target_name})
