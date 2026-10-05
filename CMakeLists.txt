cmake_minimum_required(VERSION 3.20)
include(FetchContent)

project(vulkan-starter-app LANGUAGES C CXX)

add_executable(${PROJECT_NAME}
    source/main.cpp
    source/graphics_internal.cpp
    source/graphics.cpp
    source/scene.cpp
    source/cube_renderer.cpp
    source/application.cpp
)

if(LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(WIN32)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
		target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /entry:mainCRTStartup)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
		target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
	endif()
	target_compile_definitions(${PROJECT_NAME} PRIVATE -D_USE_MATH_DEFINES)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS FALSE)
target_compile_definitions(${PROJECT_NAME} PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.5.1
)

FetchContent_Declare(
	vk-bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
	GIT_TAG v1.4.321
)

FetchContent_Declare(
	VulkanMemoryAllocator
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.4.0
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.92.9
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
    glm
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
    glm::glm
)

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${vulkanmemoryallocator_SOURCE_DIR}/include)
target_sources(${PROJECT_NAME} PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

# Compile shaders
find_program(GLSLC_FOUND NAMES glslc HINTS "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC_FOUND)
    message(FATAL_ERROR "glslc was not found. Install the Vulkan SDK and source its setup-env.sh before configuring.")
endif()
if(GLSLC_FOUND)
	set(_SHADER_BINARIES)

	macro(compile_shader SHADER_FILE)
		set(SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE})
		set(SHADER_BINARY ${SHADER_FILE}.spv)
		set(SHADER_BINARY_PATH ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_BINARY})

		add_custom_command(
			OUTPUT ${SHADER_BINARY_PATH}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
			COMMAND ${GLSLC_FOUND} --target-env=vulkan1.1 ${SHADER_SOURCE} -o ${SHADER_BINARY_PATH}
			DEPENDS ${SHADER_SOURCE}
			COMMENT "Compiling ${SHADER_FILE} shader"
            VERBATIM
		)

		list(APPEND _SHADER_BINARIES ${SHADER_BINARY_PATH})
	endmacro()

    compile_shader(lab1.vert)
    compile_shader(lab1.frag)

	add_custom_target(shaders DEPENDS ${_SHADER_BINARIES})
	add_dependencies(${PROJECT_NAME} shaders)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    LAB1_SHADER_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(lab1-scene-tests tests/scene_tests.cpp source/scene.cpp)
    target_include_directories(lab1-scene-tests PRIVATE source)
    target_link_libraries(lab1-scene-tests PRIVATE glm::glm)
    target_compile_features(lab1-scene-tests PRIVATE cxx_std_20)
    target_compile_definitions(lab1-scene-tests PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS)
    add_test(NAME lab1-scene COMMAND lab1-scene-tests)

    # A separate executable reuses the production code and real Vulkan renderer.
    # Run explicitly on a desktop; ordinary CTest stays independent of the GPU.
    get_target_property(LAB1_APP_SOURCES ${PROJECT_NAME} SOURCES)
    list(REMOVE_ITEM LAB1_APP_SOURCES source/main.cpp)
    add_executable(lab1-runtime-smoke EXCLUDE_FROM_ALL tests/runtime_smoke.cpp ${LAB1_APP_SOURCES})
    get_target_property(LAB1_APP_INCLUDES ${PROJECT_NAME} INCLUDE_DIRECTORIES)
    get_target_property(LAB1_APP_DEFINITIONS ${PROJECT_NAME} COMPILE_DEFINITIONS)
    get_target_property(LAB1_APP_LIBRARIES ${PROJECT_NAME} LINK_LIBRARIES)
    target_include_directories(lab1-runtime-smoke PRIVATE source ${LAB1_APP_INCLUDES})
    target_include_directories(lab1-runtime-smoke SYSTEM PRIVATE ${vulkanmemoryallocator_SOURCE_DIR}/include)
    target_compile_definitions(lab1-runtime-smoke PRIVATE ${LAB1_APP_DEFINITIONS})
    target_link_libraries(lab1-runtime-smoke PRIVATE ${LAB1_APP_LIBRARIES})
    target_compile_features(lab1-runtime-smoke PRIVATE cxx_std_20)
    add_dependencies(lab1-runtime-smoke shaders)
endif()
