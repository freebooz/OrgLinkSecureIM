# 生产客户端只依赖 Qt Quick 栈；Widgets 在桌面端作为迁移回归测试依赖单独发现，不能阻塞 Android/iOS 配置。
find_package(Qt6 6.2 QUIET COMPONENTS Network Qml Quick QuickControls2 Multimedia)
if(NOT Qt6_FOUND)
    message(WARNING "Qt 6 Network/QML/Quick/Multimedia 未找到：跳过 orglink-client；核心目标仍可构建。")
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 生产 Qt Quick 组合根只包含网络、QML 后端和必要模型，确保移动端不链接 QWidget 或桌面托盘。
add_library(orglink-client-ui STATIC
    assets/client-assets.qrc
    assets/qml-assets.qrc
    src/app/ClientApplication.cpp
    src/network/NetworkClient.cpp
    src/network/NetworkWorker.cpp
    src/model/SettingsModel.cpp
    src/controller/FileTransferController.cpp
    src/controller/FileTransferController.h
    src/qml/QmlClientBackend.cpp
    src/qml/QmlClientBackend.h
)
target_include_directories(orglink-client-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(orglink-client-ui
    PUBLIC Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia
           orglink::domain orglink::protocol
)
target_compile_features(orglink-client-ui PUBLIC cxx_std_20)
target_compile_definitions(orglink-client-ui PUBLIC
    ORGLINK_PROJECT_VERSION="${PROJECT_VERSION}")
if(ORGLINK_ENABLE_MOCK_MODE)
    target_compile_definitions(orglink-client-ui PUBLIC ORGLINK_ENABLE_MOCK_MODE=1)
endif()

# qt_add_executable 会在 Android 生成共享库/APK 入口，在 iOS 生成 Bundle，在 Windows 维持 GUI 子系统。
qt_add_executable(orglink-client MANUAL_FINALIZATION src/main.cpp)
target_link_libraries(orglink-client PRIVATE orglink-client-ui)

if(WIN32)
    # 主 ICO 编入 EXE，供桌面快捷方式、任务栏和 Windows 外壳读取；运行时窗口图标使用同源 PNG。
    target_sources(orglink-client PRIVATE assets/orglink-client.rc assets/orglink-app.ico)
endif()

if(ANDROID)
    # 自定义清单只声明局域网、通知、相机和麦克风能力；文件选择使用系统文档提供器，不申请宽泛存储权限。
    set_target_properties(orglink-client PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/deploy/client/android")
elseif(IOS)
    # iOS 必须在系统权限弹窗前声明本地网络、相机和麦克风用途，否则系统会拒绝会议能力。
    set_target_properties(orglink-client PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.orglink.secureim"
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/deploy/client/ios/Info.plist.in")
endif()

# 桌面 Widgets 代码仅作为迁移期回归夹具保留，不参与生产入口和移动端链接。
if(NOT ANDROID AND NOT IOS)
    find_package(Qt6 6.2 QUIET COMPONENTS Widgets Sql Test)
    if(TARGET Qt6::Widgets AND TARGET Qt6::Sql)
        add_library(orglink-client-widgets-legacy STATIC
            src/tray/ITrayAdapter.h
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
            src/model/ContactCenterModel.cpp
            src/model/FileCenterModel.cpp
            src/model/CalendarModel.cpp
            src/view/login/LoginWindow.cpp
            src/view/common/ApplicationShell.cpp
            src/view/common/ApplicationShell.h
            src/view/common/UiAssets.cpp
            src/view/common/UiAssets.h
            src/view/common/UiTheme.cpp
            src/view/common/UiTheme.h
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
        target_include_directories(orglink-client-widgets-legacy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(orglink-client-widgets-legacy
            PUBLIC orglink-client-ui Qt6::Widgets Qt6::Sql orglink::application)
        target_compile_features(orglink-client-widgets-legacy PUBLIC cxx_std_20)
        if(WIN32)
            # 旧本地仓储使用当前 Windows 用户 DPAPI；该实现不进入移动端生产库。
            target_link_libraries(orglink-client-widgets-legacy PRIVATE Crypt32)
        endif()
    endif()
endif()

# 内置字体采用 OFL-1.1，许可证必须随桌面可执行文件发布。
add_custom_command(TARGET orglink-client POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:orglink-client>/licenses/fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE-Sarasa-Gothic.txt"
            "$<TARGET_FILE_DIR:orglink-client>/licenses/fonts/LICENSE-Sarasa-Gothic.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE-Source-Han-Sans.txt"
            "$<TARGET_FILE_DIR:orglink-client>/licenses/fonts/LICENSE-Source-Han-Sans.txt"
    COMMENT "部署客户端内置字体许可证"
    VERBATIM)

if(WIN32 AND NOT ORGLINK_ENABLE_MOCK_MODE)
    # 生产 Windows 客户端必须同时部署 Qt 动态库、QML 导入和媒体插件，脱离 SDK 后仍能启动和预览。
    get_target_property(_orglink_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_orglink_qt_bin_directory "${_orglink_qmake_executable}" DIRECTORY)
    find_program(ORGLINK_WINDEPLOYQT_EXECUTABLE NAMES windeployqt
        HINTS "${_orglink_qt_bin_directory}" NO_DEFAULT_PATH)
    if(NOT ORGLINK_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR "生产客户端缺少 windeployqt，无法生成可独立运行的 Windows 发布目录。")
    endif()
    add_custom_command(TARGET orglink-client POST_BUILD
        COMMAND "${ORGLINK_WINDEPLOYQT_EXECUTABLE}"
                --release --compiler-runtime --no-translations
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                "$<TARGET_FILE:orglink-client>"
        COMMENT "部署 OrgLink 客户端 Qt Quick、媒体运行库与 Windows 平台插件"
        VERBATIM)
endif()

qt_finalize_executable(orglink-client)
