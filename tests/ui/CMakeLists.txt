# Qt imported target 具有目录作用域；在实际定义 UI 测试的目录重新发现，避免 Release 工程缺失 QML/Quick 头文件与链接传递。
find_package(Qt6 6.2 REQUIRED COMPONENTS Test Widgets Qml Quick QuickControls2 Multimedia)

add_executable(orglink-client-smoke-tests ClientSmokeTests.cpp)
set_target_properties(orglink-client-smoke-tests PROPERTIES AUTOMOC ON)
target_link_libraries(orglink-client-smoke-tests PRIVATE
    orglink-client-ui Qt6::Test Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia)
if(TARGET orglink-client-widgets-legacy)
    # 旧 QWidget 页面只参与迁移回归，不会重新进入生产 Qt Quick 可执行文件。
    target_link_libraries(orglink-client-smoke-tests PRIVATE orglink-client-widgets-legacy)
endif()
if(TARGET orglink-server-gateway)
    target_link_libraries(orglink-client-smoke-tests PRIVATE orglink-server-gateway)
    target_compile_definitions(orglink-client-smoke-tests PRIVATE ORGLINK_TEST_HAS_GATEWAY=1)
endif()
add_test(NAME qt-client-smoke COMMAND orglink-client-smoke-tests)
set_tests_properties(qt-client-smoke PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")
orglink_test_with_qt_runtime(qt-client-smoke)
