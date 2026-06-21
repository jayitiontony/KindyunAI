cmake_minimum_required(VERSION 3.10)
project(WordToolPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 插件需要输出为 DLL
add_library(WordTool SHARED WordTool.cpp)

# 链接 Windows 核心库：COM 和 OLE 自动化
target_link_libraries(WordTool PRIVATE ole32 oleaut32)

# 设置输出目录，方便直接放入 KindyunAI 的插件目录
set_target_properties(WordTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins"
)

# 导出符号 (Windows DLL 必需)
set_target_properties(WordTool PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
