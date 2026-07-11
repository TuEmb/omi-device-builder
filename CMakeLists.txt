cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
enable_language(C ASM) # OPUS has ASM

project(omi_device_builder)

# --- Core (always built) ---
target_sources(app PRIVATE
    src/main.c
    src/services/transport.c
    src/services/settings.c
)

# --- Components (gated per feature; see Kconfig) ---
if(CONFIG_OMI_LED)
    target_sources(app PRIVATE src/components/led/led.c)
endif()

if(CONFIG_OMI_MIC)
    target_sources(app PRIVATE
        src/components/mic/mic.c
        src/components/codec/codec.c
    )
endif()

if(CONFIG_OMI_BATTERY)
    target_sources(app PRIVATE src/components/battery/battery.c)
endif()

if(CONFIG_OMI_BUTTON)
    target_sources(app PRIVATE src/components/button/button.c)
endif()

# Component/service headers are included by short name (e.g. "led.h", "config.h"),
# so expose every module directory (and src/ for shared headers + the vendored
# opus path used as "opus-1.2.1/opus.h") on the include path.
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/components/led
    ${CMAKE_CURRENT_SOURCE_DIR}/src/components/mic
    ${CMAKE_CURRENT_SOURCE_DIR}/src/components/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/src/components/battery
    ${CMAKE_CURRENT_SOURCE_DIR}/src/components/button
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
)

if(CONFIG_OMI_CODEC_OPUS AND CONFIG_OMI_MIC)
    add_subdirectory(src/opus-1.2.1/)
    target_link_libraries(app PRIVATE opus_codec)
endif()
