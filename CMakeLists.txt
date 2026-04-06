cmake_minimum_required(VERSION 3.20)
project(SysYCompiler LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置可执行文件输出目录为 build/ 而非 build/src
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 查找系统安装的 Bison 和 Flex 库, REQUIRED 表示如果没有找到会报错
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# 获取 koopa 库路径（与 Makefile 保持一致）
set(KOOPA_LIB_DIR "$ENV{CDE_LIBRARY_PATH}/native")
set(KOOPA_INCLUDE_DIR "$ENV{CDE_INCLUDE_PATH}")

message(STATUS "KOOPA_LIB_DIR: ${KOOPA_LIB_DIR}")
message(STATUS "KOOPA_INCLUDE_DIR: ${KOOPA_INCLUDE_DIR}")

# 添加子目录
add_subdirectory(include)
add_subdirectory(src)
