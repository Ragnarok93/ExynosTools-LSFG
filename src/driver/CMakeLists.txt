cmake_minimum_required(VERSION 3.22)
project(ExynosToolsGameNativeShim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_RPATH ON)

set(EXYNOS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "ExynosTools-LSFG repository root")
set(EXYNOS_VULKAN_HEADERS_PATH "" CACHE PATH "Vulkan-Headers repository root")

set(_vulkan_candidates "")
if(EXYNOS_VULKAN_HEADERS_PATH)
    list(APPEND _vulkan_candidates
        "${EXYNOS_VULKAN_HEADERS_PATH}/include"
        "${EXYNOS_VULKAN_HEADERS_PATH}"
    )
endif()
if(EXISTS "${EXYNOS_ROOT}/external/Vulkan-Headers/include")
    list(APPEND _vulkan_candidates "${EXYNOS_ROOT}/external/Vulkan-Headers/include")
endif()
if(DEFINED ENV{VULKAN_SDK})
    list(APPEND _vulkan_candidates "$ENV{VULKAN_SDK}/include" "$ENV{VULKAN_SDK}/Include")
endif()

find_path(EXYNOS_SHIM_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h vulkan/vk_layer.h
    PATHS ${_vulkan_candidates}
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)
if(NOT EXYNOS_SHIM_VULKAN_INCLUDE_DIR)
    find_path(EXYNOS_SHIM_VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h vulkan/vk_layer.h)
endif()
if(NOT EXYNOS_SHIM_VULKAN_INCLUDE_DIR)
    message(FATAL_ERROR "Vulkan headers with vk_layer.h are required")
endif()

add_library(vulkan_exynostools SHARED gamenative_wrapper_shim.cpp)
set_target_properties(vulkan_exynostools PROPERTIES
    OUTPUT_NAME "vulkan_exynostools"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    SKIP_BUILD_RPATH ON
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH ""
    SKIP_INSTALL_RPATH ON
)
target_include_directories(vulkan_exynostools PRIVATE "${EXYNOS_SHIM_VULKAN_INCLUDE_DIR}")

if(ANDROID)
    target_link_libraries(vulkan_exynostools PRIVATE dl)
    find_program(EXYNOS_PATCHELF_EXECUTABLE patchelf)
    if(EXYNOS_PATCHELF_EXECUTABLE)
        add_custom_command(TARGET vulkan_exynostools POST_BUILD
            COMMAND "${EXYNOS_PATCHELF_EXECUTABLE}" --remove-rpath "$<TARGET_FILE:vulkan_exynostools>"
            COMMENT "Removing build-machine RPATH/RUNPATH from GameNative driver shim"
            VERBATIM
        )
    endif()
else()
    target_link_libraries(vulkan_exynostools PRIVATE dl)
endif()
