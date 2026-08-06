find_package(Qt6 6.4 QUIET COMPONENTS Core Network)
find_package(PostgreSQL QUIET)

if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_library(orglink-server-gateway STATIC
        src/GatewayServer.cpp
        src/InMemoryRuntimeStore.cpp
        src/MinioObjectStore.cpp
        src/LiveKitConferenceProvider.cpp
        src/IRuntimeStore.h
        src/IObjectStore.h
        src/IMediaConferenceProvider.h
        src/GatewayServer.h
        src/InMemoryRuntimeStore.h
        src/MinioObjectStore.h
        src/LiveKitConferenceProvider.h
    )
    target_include_directories(orglink-server-gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(orglink-server-gateway PUBLIC Qt6::Core Qt6::Network orglink::protocol)
    target_compile_features(orglink-server-gateway PUBLIC cxx_std_20)

    if(PostgreSQL_FOUND)
        add_library(orglink-server-postgres-store STATIC
            src/PostgresRuntimeStore.cpp
            src/PostgresRuntimeStore.h
        )
        target_include_directories(orglink-server-postgres-store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(orglink-server-postgres-store PUBLIC
            orglink-server-gateway orglink::persistence-postgres PostgreSQL::PostgreSQL)
        target_compile_features(orglink-server-postgres-store PUBLIC cxx_std_20)
    endif()
endif()

add_executable(orglink-server src/main.cpp)
target_link_libraries(orglink-server PRIVATE
    orglink::application orglink::protocol orglink::persistence-postgres)

if(TARGET orglink-server-gateway)
    target_link_libraries(orglink-server PRIVATE orglink-server-gateway)
    target_compile_definitions(orglink-server PRIVATE ORGLINK_HAS_QT_GATEWAY=1)
    if(ORGLINK_ENABLE_MOCK_MODE)
        target_compile_definitions(orglink-server PRIVATE ORGLINK_ENABLE_MOCK_MODE=1)
    endif()
endif()

if(TARGET orglink-server-postgres-store)
    target_link_libraries(orglink-server PRIVATE orglink-server-postgres-store)
    target_compile_definitions(orglink-server PRIVATE ORGLINK_HAS_POSTGRES_RUNTIME_STORE=1)
endif()

if(COMMAND orglink_deploy_postgresql_runtime AND PostgreSQL_FOUND)
    orglink_deploy_postgresql_runtime(orglink-server "${PostgreSQL_LIBRARY_RELEASE}")
endif()

if(WIN32 AND Qt6_FOUND AND NOT ORGLINK_ENABLE_MOCK_MODE)
    # Windows 原生服务端同时依赖 Qt Core/Network 与 TLS 后端；发布目录必须能脱离 Qt SDK 启动。
    get_target_property(_orglink_server_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_orglink_server_qt_bin "${_orglink_server_qmake}" DIRECTORY)
    find_program(ORGLINK_SERVER_WINDEPLOYQT_EXECUTABLE NAMES windeployqt
        HINTS "${_orglink_server_qt_bin}" NO_DEFAULT_PATH)
    if(NOT ORGLINK_SERVER_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "生产服务端缺少 windeployqt，无法生成可独立运行的 Windows 发布目录。")
    endif()
    add_custom_command(TARGET orglink-server POST_BUILD
        COMMAND "${ORGLINK_SERVER_WINDEPLOYQT_EXECUTABLE}"
                --release --compiler-runtime --no-translations
                "$<TARGET_FILE:orglink-server>"
        COMMENT "部署 OrgLink 服务端 Qt Network 与 TLS 运行库"
        VERBATIM)
endif()
