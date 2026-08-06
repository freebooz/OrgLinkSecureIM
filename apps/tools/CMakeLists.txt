add_executable(orglink-migrator migrator_main.cpp)
target_compile_definitions(orglink-migrator PRIVATE ORGLINK_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
target_link_libraries(orglink-migrator PRIVATE orglink::persistence-postgres)
if(COMMAND orglink_deploy_postgresql_runtime AND PostgreSQL_FOUND)
    orglink_deploy_postgresql_runtime(orglink-migrator "${PostgreSQL_LIBRARY_RELEASE}")
endif()

add_executable(orglink-protocol-tool protocol_tool_main.cpp)
target_link_libraries(orglink-protocol-tool PRIVATE orglink::protocol)

add_executable(orglink-loadtest loadtest_main.cpp)
