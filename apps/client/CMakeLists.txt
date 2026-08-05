find_package(Qt6 6.2 QUIET COMPONENTS Widgets Test Network Sql)
if(NOT Qt6_FOUND)
    message(WARNING "Qt 6 Widgets/Test 未找到：跳过 orglink-client 和 Qt 冒烟测试；核心目标仍可构建。")
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_library(orglink-client-ui STATIC
    assets/client-assets.qrc
    src/tray/ITrayAdapter.h
    src/app/ClientApplication.cpp
    src/network/NetworkClient.cpp
    src/network/NetworkWorker.cpp
    src/storage/LocalMessageRepository.cpp
    src/storage/LocalDirectoryRepository.cpp
    src/controller/MessageController.cpp
    src/controller/GroupController.cpp
    src/controller/NotificationController.cpp
    src/controller/SettingsController.cpp
    src/controller/ContactController.cpp
    src/controller/FileCenterController.cpp
    src/controller/CalendarController.cpp
    src/controller/AuthenticationController.cpp
    src/controller/MainWindowController.cpp
    src/controller/OrganizationController.cpp
    src/controller/PersonnelController.cpp
    src/model/OrganizationTreeModel.cpp
    src/model/DepartmentPersonnelModel.cpp
    src/model/ConversationListModel.cpp
    src/model/GroupListModel.cpp
    src/model/NotificationListModel.cpp
    src/model/SettingsModel.cpp
    src/model/ContactCenterModel.cpp
    src/model/FileCenterModel.cpp
    src/model/CalendarModel.cpp
    src/view/login/LoginWindow.cpp
    src/view/common/ApplicationShell.cpp
    src/view/common/ApplicationShell.h
    src/view/group/GroupCenterView.cpp
    src/view/group/GroupCenterView.h
    src/view/notification/NotificationCenterView.cpp
    src/view/notification/NotificationCenterView.h
    src/view/settings/SettingsCenterView.cpp
    src/view/settings/SettingsCenterView.h
    src/view/file/FileCenterView.cpp
    src/view/file/FileCenterView.h
    src/view/calendar/CalendarCenterView.cpp
    src/view/calendar/CalendarCenterView.h
    src/view/main/MainWindow.cpp
    src/tray/QtTrayAdapter.cpp
    src/controller/TrayController.cpp
)

target_include_directories(orglink-client-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(orglink-client-ui
    PUBLIC Qt6::Widgets Qt6::Network Qt6::Sql orglink::application orglink::domain orglink::protocol
)
target_compile_features(orglink-client-ui PUBLIC cxx_std_20)

if(ORGLINK_ENABLE_MOCK_MODE)
    target_compile_definitions(orglink-client-ui PUBLIC ORGLINK_ENABLE_MOCK_MODE=1)
endif()

if(WIN32)
    # Windows 本地消息正文使用当前用户 DPAPI 加密；密钥材料不写入 SQLite 或配置文件。
    target_link_libraries(orglink-client-ui PRIVATE Crypt32)
endif()

add_executable(orglink-client WIN32 src/main.cpp)
target_link_libraries(orglink-client PRIVATE orglink-client-ui)

if(WIN32 AND NOT ORGLINK_ENABLE_MOCK_MODE)
    # 生产客户端必须随 EXE 部署 Qt 运行库和平台插件；否则脱离 Qt SDK 双击会在加载 Qt6Gui.dll 前失败。
    get_target_property(_orglink_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_orglink_qt_bin_directory "${_orglink_qmake_executable}" DIRECTORY)
    find_program(ORGLINK_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${_orglink_qt_bin_directory}"
        NO_DEFAULT_PATH
    )
    if(NOT ORGLINK_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "生产客户端缺少 windeployqt，无法生成可独立运行的 Windows 发布目录。")
    endif()

    # 每次重新链接生产 EXE 后同步依赖，确保干净构建不会再次退化为只能在开发机 PATH 下运行。
    add_custom_command(TARGET orglink-client POST_BUILD
        COMMAND "${ORGLINK_WINDEPLOYQT_EXECUTABLE}"
                --release
                --compiler-runtime
                --no-translations
                "$<TARGET_FILE:orglink-client>"
        COMMENT "部署 OrgLink 客户端 Qt 运行库与 Windows 平台插件"
        VERBATIM
    )
endif()
