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
