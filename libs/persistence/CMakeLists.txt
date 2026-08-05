find_package(PostgreSQL QUIET)

add_library(orglink-persistence-postgres STATIC
    src/PostgresConnection.cpp
    src/PostgresMigrator.cpp
)
add_library(orglink::persistence-postgres ALIAS orglink-persistence-postgres)
target_include_directories(orglink-persistence-postgres PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(orglink-persistence-postgres PUBLIC cxx_std_20)

if(PostgreSQL_FOUND)
    target_link_libraries(orglink-persistence-postgres PRIVATE PostgreSQL::PostgreSQL)
    target_compile_definitions(orglink-persistence-postgres PRIVATE ORGLINK_HAS_LIBPQ=1)
    message(STATUS "PostgreSQL libpq found: server database health checks enabled")
else()
    message(WARNING "PostgreSQL libpq not found: native server builds only support --preflight; Docker image installs libpq.")
endif()
