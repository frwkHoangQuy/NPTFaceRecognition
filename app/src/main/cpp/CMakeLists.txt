cmake_minimum_required(VERSION 3.22.1)

# Tên dự án của bạn
project("npufacerecognition")

# 1. Chỉ đường dẫn tới "Cuốn từ điển" (.h) để C++ đọc được rknn_api.h
include_directories(include)

# 2. Khai báo "Anh lực sĩ" NPU (librknnrt.so) là một thư viện đã có sẵn
add_library(rknnrt_lib SHARED IMPORTED)

# Chỉ định vị trí chính xác của file .so dựa trên kiến trúc chip (arm64-v8a)
set_target_properties(rknnrt_lib PROPERTIES IMPORTED_LOCATION
        ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/librknnrt.so)

# 3. Tạo thư viện chính của bạn (đặt tên là native-lib cho giống mặc định Java)
add_library(npufacerecognition SHARED
        native-lib.cpp)

# 4. Ký hợp đồng liên kết: Gắn "Anh lực sĩ NPU" và "Hệ thống Log" vào code của bạn
target_link_libraries(npufacerecognition
        rknnrt_lib
        android # Thư viện Android cơ bản
        log     # Thư viện để bạn in log debug (Log.d trong Java)
)