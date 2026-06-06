cmake_minimum_required(VERSION 3.10)

project(yolov5_detect_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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
# spdlog (header-only)
# ============================================================
set(SPDLOG_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/spdlog/include)

# ============================================================
# OpenCV (可选，用于视频处理)
# ============================================================
option(ENABLE_VIDEO "Enable video processing (requires OpenCV)" OFF)

if(ENABLE_VIDEO)
    if(DEFINED OpenCV_DIR)
        # 使用指定路径
    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv/lib/cmake/opencv4)
        set(OpenCV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv/lib/cmake/opencv4)
    endif()
    find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio)

    # FFmpeg + x264 静态库（OpenCV videoio 后端依赖，顺序关键：依赖者在前）
    set(FFMPEG_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg)
    set(X264_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/x264)
    if(EXISTS ${FFMPEG_PATH}/lib)
        set(OPENCV_3RDPARTY_PATH ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv/lib/opencv4/3rdparty)
        set(FFMPEG_EXTRA_LIBS
            ${FFMPEG_PATH}/lib/libavformat.a
            ${FFMPEG_PATH}/lib/libavcodec.a
            ${X264_PATH}/lib/libx264.a
            ${FFMPEG_PATH}/lib/libswresample.a
            ${FFMPEG_PATH}/lib/libswscale.a
            ${FFMPEG_PATH}/lib/libavutil.a
            ${OPENCV_3RDPARTY_PATH}/libzlib.a
            m
        )
    endif()
endif()

# ============================================================
# Threads
# ============================================================
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

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
# 核心检测库（不依赖 OpenCV 的部分）
# ============================================================
set(PIPELINE_SOURCES
    src/core/pipeline.cc
    src/source/image_source.cc
    src/detector/yolov5_detector.cc
    src/output/image_file_sink.cc
)

if(ENABLE_VIDEO)
    list(APPEND PIPELINE_SOURCES
        src/source/video_file_source.cc
        src/output/video_file_sink.cc
    )
endif()

add_library(detect_pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(detect_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${LIBRKNNRT_INCLUDES}
    ${SPDLOG_INCLUDES}
)

target_link_libraries(detect_pipeline
    imageutils
    fileutils
    imagedrawing
    ${LIBRKNNRT}
    Threads::Threads
    dl
)

if(ENABLE_VIDEO)
    target_include_directories(detect_pipeline PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(detect_pipeline ${OpenCV_LIBS} ${FFMPEG_EXTRA_LIBS})
endif()

# ============================================================
# Demo: 阶段一 - 图片检测
# ============================================================
add_executable(image_detect src/demo/image_detect.cc)
target_link_libraries(image_detect detect_pipeline)

# ============================================================
# Demo: 阶段二 - 视频文件检测（需要 OpenCV）
# ============================================================
if(ENABLE_VIDEO)
    add_executable(video_detect src/demo/video_detect.cc)
    target_link_libraries(video_detect detect_pipeline)
endif()

# ============================================================
# 安装规则
# ============================================================
set(INSTALL_TARGETS image_detect)
if(ENABLE_VIDEO)
    list(APPEND INSTALL_TARGETS video_detect)
endif()

install(TARGETS ${INSTALL_TARGETS} DESTINATION .)
install(FILES model/bus.jpg model/coco_80_labels_list.txt DESTINATION model)
file(GLOB RKNN_FILES "model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)
