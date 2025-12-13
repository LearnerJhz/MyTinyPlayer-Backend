cmake_minimum_required(VERSION 3.10)

# 项目名称
project(MyTinyPlayerCore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 OpenSSL 包 (必须加上 REQUIRED，找不到就报错)
#find_package(OpenSSL REQUIRED)

# 设置头文件搜索路径
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/3rdparty
    ${CMAKE_SOURCE_DIR}/3rdparty/miniaudio
    ${CMAKE_SOURCE_DIR}/3rdparty/asio/asio-1.30.2/include
    ${CMAKE_SOURCE_DIR}/3rdparty/openSSL/openSSL-Win64/include
    )

link_directories(${CMAKE_SOURCE_DIR}/3rdparty/openSSL/openSSL-Win64/lib/VC/x64/MDd/)

# 收集源文件 GLOB_RECURSE 会自动把 src 文件夹下所有的 .cpp 文件都找出来
# 优点：方便，新增文件不用改 CMakeLists
# 缺点：如果删了文件，CMake 可能感知不到，需要重新生成一下 Cache
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 指定生成可执行文件
add_executable(MyTinyPlayer ${SOURCES})


# 链接库
target_link_libraries(MyTinyPlayer PRIVATE 
    libcrypto 
    libssl
)
# (可选) 如果是 Windows，Asio 可能需要链接 ws2_32 等网络库，
if(WIN32)
    target_link_libraries(MyTinyPlayer PRIVATE ws2_32)
endif()

# 如果需要导出或者安装
#install(TARGETS MyTinyPlayer DESTINATION bin)
