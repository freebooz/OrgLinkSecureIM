add_executable(orglink-core-tests unit/CoreTests.cpp)
target_link_libraries(orglink-core-tests PRIVATE orglink::application orglink::protocol)
add_test(NAME core-domain-application-protocol COMMAND orglink-core-tests)

# 计划模式不连接数据库，可在所有构建环境验证迁移命名、排序、文件读取和 SHA-256 输出。
add_test(NAME postgresql-migration-plan COMMAND orglink-migrator --plan)

add_test(
    NAME mvc-boundaries
    COMMAND ${CMAKE_COMMAND}
        -DORGLINK_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/mvc/CheckMvcBoundaries.cmake
)

if(TARGET orglink-client)
    # Qt imported targets are directory scoped；测试目录重新发现模块，确保 Qt Test target 在此可见。
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test Widgets)
    add_subdirectory(ui)
endif()

if(TARGET orglink-server-gateway)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test Network)
    add_subdirectory(integration)
endif()

if(TARGET orglink-server-postgres-store)
    add_executable(orglink-postgres-runtime-tests integration/PostgresRuntimeStoreTests.cpp)
    target_link_libraries(orglink-postgres-runtime-tests PRIVATE orglink-server-postgres-store Qt6::Core)
    add_test(NAME postgres-runtime-optional COMMAND orglink-postgres-runtime-tests)
endif()

if(TARGET orglink-client-ui)
    add_executable(orglink-client-tls-stack-tests integration/ClientTlsStackTests.cpp)
    set_target_properties(orglink-client-tls-stack-tests PROPERTIES AUTOMOC ON)
    target_link_libraries(orglink-client-tls-stack-tests PRIVATE
        orglink-client-ui Qt6::Test Qt6::Network)
    add_test(NAME client-tls-stack-optional COMMAND orglink-client-tls-stack-tests)
endif()
