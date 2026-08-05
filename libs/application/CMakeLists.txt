add_library(orglink-application STATIC
    src/InMemoryOrganizationRepository.cpp
    src/SnapshotOrganizationRepository.cpp
    src/OrganizationService.cpp
    src/ConversationService.cpp
)
add_library(orglink::application ALIAS orglink-application)

target_include_directories(orglink-application PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(orglink-application PUBLIC orglink::domain)
target_compile_features(orglink-application PUBLIC cxx_std_20)

if(ORGLINK_ENABLE_MOCK_MODE)
    target_compile_definitions(orglink-application PUBLIC ORGLINK_ENABLE_MOCK_MODE=1)
endif()
