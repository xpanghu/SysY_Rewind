# src 目录 CMakeLists.txt

# 确保输出目录存在
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/front_end)

# Flex/Bison 配置
# 系统内置宏
# FlEX_TARGET(<target> <input.l> <output.cpp>) 
# BISON_TARGET(<target> <input.y> <output.cpp> COMPILE_FLAGS "<flags>")
# bison -d 确保生成头文件
FLEX_TARGET(Lexer ${CMAKE_CURRENT_SOURCE_DIR}/front_end/sysy.l
    ${CMAKE_CURRENT_BINARY_DIR}/front_end/sysy.lex.cpp)
BISON_TARGET(Parser ${CMAKE_CURRENT_SOURCE_DIR}/front_end/sysy.y
    ${CMAKE_CURRENT_BINARY_DIR}/front_end/sysy.tab.cpp COMPILE_FLAGS "-d")

# 建立依赖关系，确保Flex在Bison之前运行
# Bison 生成的 sysy.tab.hpp 可能被 Flex 生成的代码使用
# 如果没有这个依赖，并行构建时可能出错
ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# 收集所有源文件
set(FRONTEND_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/front_end/ast.cpp
)

set(IR_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/symbol_table.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/rewind_ir_builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ir/koopa_ir_builder.cpp
)

set(BACKEND_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/back_end/riscv.cpp
)

# 添加编译生成的文件到源文件列表
set(GENERATED_SRCS
    ${FLEX_Lexer_OUTPUTS}
    ${BISON_Parser_OUTPUTS}
)

# 创建可执行文件
add_executable(compiler
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${FRONTEND_SRCS}
    ${IR_SRCS}
    ${BACKEND_SRCS}
    ${GENERATED_SRCS}
)

# 添加所有 include 目录到目标
target_include_directories(compiler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/front_end
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/ir
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/back_end
    ${KOOPA_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/front_end
)

# 链接 koopa 库
target_link_directories(compiler PRIVATE ${KOOPA_LIB_DIR})
target_link_libraries(compiler PRIVATE koopa)

# 编译选项
target_compile_options(compiler PRIVATE
    -Wall
    -Wno-register
)

# 导出 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
