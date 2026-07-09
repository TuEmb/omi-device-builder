cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
enable_language(C ASM) # OPUS has ASM

project(omi_device_builder)

target_sources(app PRIVATE
    src/main.c
    src/transport.c
    src/settings.c
)

if(CONFIG_OMI_ENABLE_MIC)
    target_sources(app PRIVATE src/mic.c src/codec.c)
endif()

if(CONFIG_OMI_ENABLE_BATTERY)
    target_sources(app PRIVATE src/battery.c)
endif()

if(CONFIG_OMI_ENABLE_BUTTON)
    target_sources(app PRIVATE src/button.c)
endif()

if(CONFIG_OMI_ENABLE_LED)
    target_sources(app PRIVATE src/led.c)
endif()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CONFIG_OMI_CODEC_OPUS AND CONFIG_OMI_ENABLE_MIC)
    add_subdirectory(src/opus-1.2.1/)
    target_link_libraries(app PRIVATE opus_codec)
endif()
