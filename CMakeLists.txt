cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE
            "$ENV{VITASDK}/share/vita.toolchain.cmake"
            CACHE PATH "VitaSDK toolchain")
    else()
        message(FATAL_ERROR "Set VITASDK before configuring this project")
    endif()
endif()

project(vita_mcpe_test CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type for the Vita target" FORCE)
endif()

option(MCPE_USE_VITAGL "Render the MCPE GLES 1.x path through vitaGL" ON)

add_executable(${PROJECT_NAME}
    main.cpp
    mcpe_elf.cpp
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)

if(MCPE_USE_VITAGL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MCPE_USE_VITAGL=1)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,--wrap=invoke_splashscreen"
        "-Wl,--wrap=abort")
    target_link_libraries(${PROJECT_NAME}
        vitaGL
        png
        Z
        m
        SceAppUtil_stub
        SceCommonDialog_stub
        SceGxm_stub
        SceDisplay_stub
        SceAppMgr_stub
        mathneon
        vitashark
        kubridge_stub_weak
        SceCtrl_stub
        SceShaccCgExt
        taihen_stub
        SceShaccCg_stub
        SceKernelDmacMgr_stub
        SceSysmem_stub
        SceKernelThreadMgr_stub
        SceIofilemgr_stub
        SceNet_stub
        SceTouch_stub
        SceAudio_stub
    )
else()
    target_link_libraries(${PROJECT_NAME}
        png
        Z
        m
        SceAppUtil_stub
        SceCommonDialog_stub
        SceCtrl_stub
        SceDisplay_stub
        SceSysmem_stub
        SceGxm_stub
        SceKernelThreadMgr_stub
        SceIofilemgr_stub
        SceNet_stub
        SceTouch_stub
        SceAudio_stub
    )
endif()

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Minecraft PE 0.9.0 Vita")
set(VITA_TITLEID "VITAMCPE0")
set(VITA_VERSION "01.33")

function(mcpe_validate_livearea_png RELATIVE_PATH EXPECTED_WIDTH EXPECTED_HEIGHT)
    set(PNG_PATH "${CMAKE_SOURCE_DIR}/livearea/sce_sys/${RELATIVE_PATH}")
    if(NOT EXISTS "${PNG_PATH}")
        return()
    endif()
    file(READ "${PNG_PATH}" PNG_HEADER OFFSET 0 LIMIT 26 HEX)
    string(TOLOWER "${PNG_HEADER}" PNG_HEADER)
    string(LENGTH "${PNG_HEADER}" PNG_HEADER_LENGTH)
    if(PNG_HEADER_LENGTH LESS 52)
        message(FATAL_ERROR "LiveArea PNG is truncated: ${PNG_PATH}")
    endif()
    string(SUBSTRING "${PNG_HEADER}" 0 16 PNG_SIGNATURE)
    string(SUBSTRING "${PNG_HEADER}" 32 8 PNG_WIDTH)
    string(SUBSTRING "${PNG_HEADER}" 40 8 PNG_HEIGHT)
    string(SUBSTRING "${PNG_HEADER}" 48 2 PNG_BIT_DEPTH)
    string(SUBSTRING "${PNG_HEADER}" 50 2 PNG_COLOR_TYPE)
    if(NOT PNG_SIGNATURE STREQUAL "89504e470d0a1a0a"
       OR NOT PNG_WIDTH STREQUAL "${EXPECTED_WIDTH}"
       OR NOT PNG_HEIGHT STREQUAL "${EXPECTED_HEIGHT}"
       OR NOT PNG_BIT_DEPTH STREQUAL "08"
       OR NOT PNG_COLOR_TYPE STREQUAL "03")
        message(FATAL_ERROR
            "Invalid LiveArea PNG ${RELATIVE_PATH}: expected indexed 8-bit "
            "${EXPECTED_WIDTH}x${EXPECTED_HEIGHT} IHDR, got "
            "width=${PNG_WIDTH} height=${PNG_HEIGHT} "
            "depth=${PNG_BIT_DEPTH} type=${PNG_COLOR_TYPE}")
    endif()
endfunction()

mcpe_validate_livearea_png("icon0.png" "00000080" "00000080")
mcpe_validate_livearea_png("pic0.png" "000003c0" "00000220")
mcpe_validate_livearea_png("livearea/contents/bg0.png" "00000348" "000001f4")
mcpe_validate_livearea_png("livearea/contents/default_gate.png" "00000118" "0000009e")

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

set(MCPE_SO "${CMAKE_SOURCE_DIR}/data/libminecraftpe.so")
if(EXISTS "${MCPE_SO}")
    set(MCPE_VPK_FILES FILE ${MCPE_SO} data/libminecraftpe.so)
    set(MCPE_SHADER_RUNTIME "${CMAKE_SOURCE_DIR}/data/libshacccg.suprx")
    if(EXISTS "${MCPE_SHADER_RUNTIME}")
        list(APPEND MCPE_VPK_FILES
            FILE "${MCPE_SHADER_RUNTIME}" "data/libshacccg.suprx")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
        file(GLOB_RECURSE MCPE_ASSETS
            RELATIVE "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_SOURCE_DIR}/assets/*")
        foreach(MCPE_ASSET IN LISTS MCPE_ASSETS)
            if(NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/${MCPE_ASSET}"
               AND NOT MCPE_ASSET MATCHES "(^|/)\\.DS_Store$")
                list(APPEND MCPE_VPK_FILES
                    FILE "${CMAKE_SOURCE_DIR}/assets/${MCPE_ASSET}"
                    "data/assets/${MCPE_ASSET}")
            endif()
        endforeach()
    endif()

    set(MCPE_LIVEAREA_FILES
        "icon0.png"
        "pic0.png"
        "livearea/contents/bg0.png"
        "livearea/contents/default_gate.png"
        "livearea/contents/template.xml"
    )
    foreach(MCPE_LIVEAREA_FILE IN LISTS MCPE_LIVEAREA_FILES)
        set(MCPE_LIVEAREA_SOURCE
            "${CMAKE_SOURCE_DIR}/livearea/sce_sys/${MCPE_LIVEAREA_FILE}")
        if(EXISTS "${MCPE_LIVEAREA_SOURCE}")
            list(APPEND MCPE_VPK_FILES
                FILE "${MCPE_LIVEAREA_SOURCE}"
                "sce_sys/${MCPE_LIVEAREA_FILE}")
        endif()
    endforeach()

    vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        ${MCPE_VPK_FILES}
    )
else()
    message(WARNING
        "data/libminecraftpe.so is missing: the ELF will build, but the VPK target will not be created")
endif()
