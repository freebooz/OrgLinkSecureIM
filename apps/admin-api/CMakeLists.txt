find_package(Qt6 6.4 QUIET COMPONENTS Core Network)
find_package(PostgreSQL QUIET)

if(Qt6_FOUND AND PostgreSQL_FOUND)
    set(CMAKE_AUTOMOC ON)
    add_executable(orglink-admin-api
        src/main.cpp
        src/AdminHttpServer.cpp
        src/AdminHttpServer.h
        src/PostgresAdminRepository.cpp
        src/PostgresAdminRepository.h
    )
    target_include_directories(orglink-admin-api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(orglink-admin-api PRIVATE
        Qt6::Core Qt6::Network orglink::persistence-postgres PostgreSQL::PostgreSQL)
    target_compile_features(orglink-admin-api PRIVATE cxx_std_20)

    if(COMMAND orglink_deploy_postgresql_runtime)
        orglink_deploy_postgresql_runtime(orglink-admin-api "${PostgreSQL_LIBRARY_RELEASE}")
    endif()
else()
    message(WARNING "Qt Network 或 PostgreSQL 不可用，跳过 Angular 管理端 C++ REST API")
endif()
