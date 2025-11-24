cmake_minimum_required(VERSION 3.16.3)

project(slideshow LANGUAGES C CXX)


set(RPI_USE_HW_JPEG_DECODE OFF) 


set(CMAKE_BUILD_TYPE Release) #Important on RPi 


#===========================================================================================

set(USE_TURBO_JPEG OFF)
set(USE_GST OFF)

if(RPI_USE_HW_JPEG_DECODE)
    set(USE_GST ON)
else()
    set(USE_TURBO_JPEG ON)
endif()


if(USE_TURBO_JPEG)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JPEG_TURBO REQUIRED libturbojpeg)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)
find_library(GBM_LIB gbm)
find_library(EGL_LIB EGL)
find_library(GLES2_LIB GLESv2)

add_executable(slideshow)
target_sources(slideshow PUBLIC 
            ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/load_image.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/gl_util.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/drm_util.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/gbm_util.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/egl_util.cpp
)
target_include_directories(slideshow PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LIBDRM_INCLUDE_DIRS} "/usr/include/libdrm")
target_link_libraries(slideshow PUBLIC 
            ${LIBDRM_LIBRARIES}
            ${GBM_LIB}
            ${EGL_LIB}
            ${GLES2_LIB}
)
set_target_properties(slideshow PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})


if(USE_TURBO_JPEG)
    target_compile_definitions(slideshow PUBLIC -DUSE_TURBO_JPEG)
    target_include_directories(slideshow PUBLIC ${JPEG_TURBO_INCLUDE_DIRS})
    target_link_libraries(slideshow PUBLIC ${JPEG_TURBO_LIBRARIES})
endif()

if(USE_GST)
    target_compile_definitions(slideshow PUBLIC -DUSE_GST)
endif()

#target_compile_definitions(slideshow PUBLIC -DDEBUG)
#target_compile_definitions(slideshow PUBLIC -DDEBUG_RENDER)
