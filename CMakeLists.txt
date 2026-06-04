cmake_minimum_required(VERSION 3.10)

project(yolov5_detect_demo)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

# ============================================================
# 平台与架构配置
# ============================================================
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TARGET_LIB_ARCH aarch64)
else()
    set(TARGET_LIB_ARCH armhf)
endif()

# ============================================================
# RKNN Runtime
# ============================================================
set(RKNN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/rknpu2)
set(LIBRKNNRT ${RKNN_PATH}/Linux/${TARGET_LIB_ARCH}/librknnrt.so)
set(LIBRKNNRT_INCLUDES ${RKNN_PATH}/include)

# ============================================================
# RGA (Rockchip Graphics Acceleration)
# ============================================================
set(RGA_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/librga)
set(LIBRGA ${RGA_PATH}/Linux/${TARGET_LIB_ARCH}/librga.a)
set(LIBRGA_INCLUDES ${RGA_PATH}/include)

# ============================================================
# STB Image
# ============================================================
set(STB_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/stb_image)

# ============================================================
# JPEG Turbo
# ============================================================
set(JPEG_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jpeg_turbo)
set(LIBJPEG ${JPEG_PATH}/Linux/${TARGET_LIB_ARCH}/libturbojpeg.a)
set(LIBJPEG_INCLUDES ${JPEG_PATH}/include)

# ============================================================
# Utils 静态库
# ============================================================
add_library(fileutils STATIC utils/file_utils.c)
target_include_directories(fileutils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils)

add_library(imagedrawing STATIC utils/image_drawing.c)
target_include_directories(imagedrawing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/utils)

add_library(imageutils STATIC utils/image_utils.c)
target_include_directories(imageutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${STB_INCLUDES}
    ${LIBRGA_INCLUDES}
    ${LIBJPEG_INCLUDES}
)
target_link_libraries(imageutils ${LIBRGA} ${LIBJPEG})

# ============================================================
# 主可执行程序
# ============================================================
add_executable(${PROJECT_NAME}
    src/main.cc
    src/postprocess.cc
    src/yolov5.cc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${LIBRKNNRT_INCLUDES}
)

target_link_libraries(${PROJECT_NAME}
    imageutils
    fileutils
    imagedrawing
    ${LIBRKNNRT}
    dl
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} Threads::Threads)

# ============================================================
# 安装规则
# ============================================================
install(TARGETS ${PROJECT_NAME} DESTINATION .)
install(FILES model/bus.jpg model/coco_80_labels_list.txt DESTINATION model)
file(GLOB RKNN_FILES "model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)
