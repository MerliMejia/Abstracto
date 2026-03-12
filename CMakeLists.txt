cmake_minimum_required(VERSION 3.20)

project(Abstracto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

option(ABSTRACTO_FETCH_DEPS "Fetch GLFW/GLM/STB/TinyObjLoader when missing" ON)

find_package(Vulkan REQUIRED)

if(ABSTRACTO_FETCH_DEPS)
  include(FetchContent)
endif()

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND AND ABSTRACTO_FETCH_DEPS)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
  )
  FetchContent_MakeAvailable(glfw)
endif()

find_package(glm CONFIG QUIET)
if(NOT glm_FOUND AND ABSTRACTO_FETCH_DEPS)
  FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
  )
  FetchContent_MakeAvailable(glm)
endif()

if(ABSTRACTO_FETCH_DEPS)
  FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
  )
  FetchContent_MakeAvailable(stb)

  FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
  )
  FetchContent_MakeAvailable(tinyobjloader)
endif()

if(TARGET glfw)
  set(ABSTRACTO_GLFW_TARGET glfw)
elseif(TARGET glfw::glfw)
  set(ABSTRACTO_GLFW_TARGET glfw::glfw)
else()
  message(FATAL_ERROR "GLFW not found. Install it or set ABSTRACTO_FETCH_DEPS=ON.")
endif()

if(TARGET glm::glm)
  set(ABSTRACTO_GLM_TARGET glm::glm)
elseif(TARGET glm)
  set(ABSTRACTO_GLM_TARGET glm)
else()
  message(FATAL_ERROR "GLM not found. Install it or set ABSTRACTO_FETCH_DEPS=ON.")
endif()

add_executable(Abstracto src/main.cpp)

target_compile_definitions(
  Abstracto
  PRIVATE
    VULKAN_HPP_NO_CONSTRUCTORS
)

set(ABSTRACTO_SHADER_DIR ${CMAKE_SOURCE_DIR}/assets/shaders)
set(ABSTRACTO_SLANG_SOURCES
  ${ABSTRACTO_SHADER_DIR}/test_pass.slang
  ${ABSTRACTO_SHADER_DIR}/debug_gbuffer_pass.slang
  ${ABSTRACTO_SHADER_DIR}/geometry_pass.slang
  ${ABSTRACTO_SHADER_DIR}/light_pass.slang
  ${ABSTRACTO_SHADER_DIR}/solid_transform_pass.slang
  ${ABSTRACTO_SHADER_DIR}/texture_pass.slang
  ${ABSTRACTO_SHADER_DIR}/debug_present_pass.slang
)

find_program(ABSTRACTO_SLANGC_EXECUTABLE NAMES slangc)
set(ABSTRACTO_SLANG_OUTPUTS)
foreach(ABSTRACTO_SLANG_SOURCE IN LISTS ABSTRACTO_SLANG_SOURCES)
  if(EXISTS ${ABSTRACTO_SLANG_SOURCE})
    get_filename_component(ABSTRACTO_SLANG_NAME
                           ${ABSTRACTO_SLANG_SOURCE} NAME_WE)
    set(ABSTRACTO_SLANG_OUTPUT
        ${ABSTRACTO_SHADER_DIR}/${ABSTRACTO_SLANG_NAME}.spv)
    list(APPEND ABSTRACTO_SLANG_OUTPUTS ${ABSTRACTO_SLANG_OUTPUT})

    if(ABSTRACTO_SLANGC_EXECUTABLE)
      add_custom_command(
        OUTPUT ${ABSTRACTO_SLANG_OUTPUT}
        COMMAND ${ABSTRACTO_SLANGC_EXECUTABLE}
                ${ABSTRACTO_SLANG_SOURCE}
                -target spirv
                -profile glsl_450
                -emit-spirv-directly
                -o ${ABSTRACTO_SLANG_OUTPUT}
        DEPENDS ${ABSTRACTO_SLANG_SOURCE}
        COMMENT "Compiling Slang shader to SPIR-V: ${ABSTRACTO_SLANG_OUTPUT}"
        VERBATIM
      )
    endif()
  endif()
endforeach()

if(ABSTRACTO_SLANGC_EXECUTABLE AND ABSTRACTO_SLANG_OUTPUTS)
  add_custom_target(abstracto_shaders DEPENDS ${ABSTRACTO_SLANG_OUTPUTS})
  add_dependencies(Abstracto abstracto_shaders)
else()
  message(WARNING "slangc not found (or no Slang sources were found). Install slangc to auto-generate assets/shaders/*.spv.")
endif()

target_link_libraries(
  Abstracto
  PRIVATE
    Vulkan::Vulkan
    ${ABSTRACTO_GLFW_TARGET}
    ${ABSTRACTO_GLM_TARGET}
)

target_include_directories(
  Abstracto
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

if(DEFINED stb_SOURCE_DIR)
  target_include_directories(Abstracto PRIVATE ${stb_SOURCE_DIR})
else()
  find_path(STB_INCLUDE_DIR stb_image.h)
  if(NOT STB_INCLUDE_DIR)
    message(FATAL_ERROR "stb_image.h not found. Install stb or set ABSTRACTO_FETCH_DEPS=ON.")
  endif()
  target_include_directories(Abstracto PRIVATE ${STB_INCLUDE_DIR})
endif()

if(DEFINED tinyobjloader_SOURCE_DIR)
  target_include_directories(Abstracto PRIVATE ${tinyobjloader_SOURCE_DIR})
else()
  find_path(TINYOBJLOADER_INCLUDE_DIR tiny_obj_loader.h)
  if(NOT TINYOBJLOADER_INCLUDE_DIR)
    message(FATAL_ERROR "tiny_obj_loader.h not found. Install tinyobjloader or set ABSTRACTO_FETCH_DEPS=ON.")
  endif()
  target_include_directories(Abstracto PRIVATE ${TINYOBJLOADER_INCLUDE_DIR})
endif()
