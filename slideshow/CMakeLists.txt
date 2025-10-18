cmake_minimum_required(VERSION 3.16.3)

project(slideshow LANGUAGES C CXX)


# Link against brcmGLESv2 instead of GLESv2. 
# Note that Broadcom drivers are NOT available on the 64-bit version of RaspberrypiOS (aarch64), they're only available on 32-bit (armv7l).
set(RPI_USE_BROADCOM_DRIVER ON)

# Enable if your Raspberry supports NEON. Generally from Pi2 onwards this is supported. 
# This enables optimizations in the SDL library. 
set(RPI_HAS_NEON OFF)


#===========================================================================================


set(SDL_AUDIO OFF)
set(SDL_GPU OFF)
set(SDL_RENDER OFF)
set(SDL_CAMERA OFF)
set(SDL_JOYSTICK OFF)
set(SDL_HAPTIC OFF)
set(SDL_HIDAPI OFF)
set(SDL_POWER OFF)
set(SDL_SENSOR OFF)
set(SDL_DIALOG OFF)
set(SDL_TRAY OFF)
set(SDL_VULKAN OFF)
set(SDL_OFFSCREEN OFF)
set(SDL_OPENGL OFF)
set(SDL_DUMMYVIDEO OFF)

set(SDL_UNIX_CONSOLE_BUILD ON)

if(RPI_USE_BROADCOM_DRIVER)
    set(SDL_KMSDRM OFF)
    set(SDL_RPI ON)
else()
    set(SDL_KMSDRM ON)
    set(SDL_RPI OFF)
endif()

if(RPI_HAS_NEON)
    add_compile_options(-mfpu=neon)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SDL)


add_library(stb_image STATIC)
target_sources(stb_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/stb_image.cpp)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/stb)


add_executable(slideshow)
target_sources(slideshow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

if(RPI_USE_BROADCOM_DRIVER)
    target_link_directories(slideshow PUBLIC /opt/vc/lib)
    target_link_libraries(slideshow PUBLIC brcmGLESv2 brcmEGL)
else()
    target_link_libraries(slideshow PUBLIC GLESv2)
endif()

target_link_libraries(slideshow PUBLIC stb_image SDL3-shared ${CMAKE_DL_LIBS})
set_target_properties(slideshow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
