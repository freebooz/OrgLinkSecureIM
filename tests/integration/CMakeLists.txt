add_executable(orglink-gateway-tests GatewayIntegrationTests.cpp)
set_target_properties(orglink-gateway-tests PROPERTIES AUTOMOC ON)
target_link_libraries(orglink-gateway-tests PRIVATE
    orglink-server-gateway orglink::protocol Qt6::Test Qt6::Network)
add_test(NAME gateway-two-client-integration COMMAND orglink-gateway-tests)
orglink_test_with_qt_runtime(gateway-two-client-integration)
