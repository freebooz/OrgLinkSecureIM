# MVC 静态门禁：用目录和令牌检查阻止最常见的越层依赖，复杂语义仍由单元测试和评审补充。
file(GLOB_RECURSE view_files
    "${ORGLINK_SOURCE_DIR}/apps/client/src/view/*.h"
    "${ORGLINK_SOURCE_DIR}/apps/client/src/view/*.cpp")
foreach(view_file IN LISTS view_files)
    file(READ "${view_file}" source)
    if(source MATCHES "QSql|QTcpSocket|QNetworkAccessManager|google/protobuf|\.proto")
        message(FATAL_ERROR "View 禁止依赖数据库、网络或 Protobuf: ${view_file}")
    endif()
endforeach()

file(GLOB_RECURSE model_files
    "${ORGLINK_SOURCE_DIR}/apps/client/src/model/*.h"
    "${ORGLINK_SOURCE_DIR}/apps/client/src/model/*.cpp")
foreach(model_file IN LISTS model_files)
    file(READ "${model_file}" source)
    if(source MATCHES "#include[ \t]*[<\"]QWidget")
        message(FATAL_ERROR "Model 禁止依赖 QWidget: ${model_file}")
    endif()
endforeach()

file(READ "${ORGLINK_SOURCE_DIR}/libs/domain/CMakeLists.txt" domain_cmake)
if(domain_cmake MATCHES "Qt[56]?::Widgets|Qt[56]?::Network|Qt[56]?::Sql")
    message(FATAL_ERROR "Domain target 禁止链接 Qt UI、网络或数据库模块")
endif()

message(STATUS "MVC dependency boundaries passed")
