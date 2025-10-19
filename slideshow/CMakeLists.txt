cmake_minimum_required(VERSION 3.16.3)

project(slideshow LANGUAGES C CXX)


# Link against brcmGLESv2 instead of GLESv2. 
# Note that Broadcom drivers are NOT available on the 64-bit version of RaspberrypiOS (aarch64), they're only available on 32-bit (armv7l).
set(RPI_USE_BROADCOM_DRIVER OFF)

set(RPI_USE_HW_JPEG_DECODE OFF) 


# Enable if your Raspberry supports NEON. Generally from Pi2 onwards this is supported. 
# This enables optimizations in the SDL library. 
set(RPI_HAS_NEON OFF)


set(CMAKE_BUILD_TYPE Release) #Important on RPi 


#===========================================================================================

set(USE_STB_IMAGE OFF)
set(USE_TURBO_JPEG OFF)
set(USE_MMAL OFF)
set(USE_V4L2 OFF)

if(RPI_USE_HW_JPEG_DECODE)
    message( FATAL_ERROR "Did not manage to get MMAL or V4L2 working at all. gave up, turbojpeg is good enough for this purpose." )

    if(RPI_USE_BROADCOM_DRIVER)
        set(USE_MMAL ON)
    else()
        set(USE_V4L2 ON)
    endif()

else()

    set(USE_TURBO_JPEG ON)

endif()



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


if(USE_STB_IMAGE)
    add_library(stb_image STATIC)
    target_sources(stb_image PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/stb_image.cpp)
    target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/stb)
endif()

if(USE_TURBO_JPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JPEG_TURBO REQUIRED libturbojpeg)
endif()

add_executable(slideshow)
target_sources(slideshow PUBLIC 
            ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/SDL_GL_window.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/load_image.cpp
)
target_include_directories(slideshow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


if(RPI_USE_BROADCOM_DRIVER)
    target_link_directories(slideshow PUBLIC /opt/vc/lib)
    target_link_libraries(slideshow PUBLIC brcmGLESv2 brcmEGL)
    set_target_properties(slideshow PROPERTIES 
                BUILD_RPATH "/opt/vc/lib" #FIXME
                INSTALL_RPATH "/opt/vc/lib"
    )
else()
    target_link_libraries(slideshow PUBLIC GLESv2)
endif()

target_link_libraries(slideshow PUBLIC SDL3-shared ${CMAKE_DL_LIBS})
set_target_properties(slideshow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})


if(USE_STB_IMAGE)
    target_compile_definitions(slideshow PUBLIC -DUSE_STB_IMAGE)
    target_link_libraries(slideshow PUBLIC stb_image)
endif()

if(USE_TURBO_JPEG)
    target_compile_definitions(slideshow PUBLIC -DUSE_TURBO_JPEG)
    target_include_directories(slideshow PUBLIC ${JPEG_TURBO_INCLUDE_DIRS})
    target_link_libraries(slideshow PUBLIC ${JPEG_TURBO_LIBRARIES})
endif()

if(USE_MMAL)
    target_compile_definitions(slideshow PUBLIC -DUSE_MMAL)
    target_include_directories(slideshow PUBLIC /opt/vc/include)
    target_link_libraries(slideshow PUBLIC bcm_host vcos vchiq_arm mmal mmal_core mmal_util mmal_vc_client)
endif()

if(USE_V4L2)
    target_compile_definitions(slideshow PUBLIC -DUSE_V4L2)
endif()

target_compile_definitions(slideshow PUBLIC -DDEBUG)
#target_compile_definitions(slideshow PUBLIC -DDEBUG_RENDER)
