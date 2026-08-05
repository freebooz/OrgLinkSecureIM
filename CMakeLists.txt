cmake_minimum_required(VERSION 3.24)

project(OrgLinkSecureIM VERSION 0.2.0 LANGUAGES CXX)

option(ORGLINK_BUILD_QT_CLIENT "构建 Qt 桌面客户端及 Qt 测试" ON)
option(ORGLINK_ENABLE_MOCK_MODE "启用与生产代码隔离的开发模拟数据" OFF)
option(ORGLINK_BUILD_TESTS "构建并注册测试" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(libs/domain)
add_subdirectory(libs/application)
add_subdirectory(libs/protocol)
add_subdirectory(libs/persistence)
add_subdirectory(apps/server)
add_subdirectory(apps/admin)
add_subdirectory(apps/tools)

if(ORGLINK_BUILD_QT_CLIENT)
    add_subdirectory(apps/client)
endif()

if(ORGLINK_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
