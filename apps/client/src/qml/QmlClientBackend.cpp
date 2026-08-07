#include "qml/QmlClientBackend.h"

#include <orglink/domain/DomainTypes.h>

#include <QDateTime>
#include <QClipboard>
#include <QCoreApplication>
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QCamera>
#include <QCameraDevice>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QSettings>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>
#include <QVersionNumber>
#include <QVideoSink>

#include <algorithm>
#include <optional>
#include <utility>

namespace orglink::client
{
namespace
{

#if !defined(ORGLINK_PROJECT_VERSION)
#define ORGLINK_PROJECT_VERSION "0.0.0"
#endif

/** @brief 读取可选部署配置并去除首尾空白；空值表示能力未配置，界面必须如实呈现。 */
QString deploymentValue(const char* name)
{
    return qEnvironmentVariable(name).trimmed();
}

/**
 * @brief 统计应用专属目录占用量；符号链接不跟随，避免扫描越过受控目录边界。
 * @param rootPath 已由调用方从 QStandardPaths 获取的本地目录。
 * @return 可读取普通文件的总字节数；目录不存在时返回 0。
 */
qulonglong directorySizeWithoutLinks(const QString& rootPath)
{
    if (rootPath.trimmed().isEmpty() || !QFileInfo(rootPath).isDir()) return 0;
    qulonglong total = 0;
    QDirIterator iterator(rootPath, QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        iterator.next();
        const auto size = iterator.fileInfo().size();
        if (size > 0) total += static_cast<qulonglong>(size);
    }
    return total;
}

/** @brief 返回应用专属备份目录；目录位于用户数据区，绝不使用程序安装目录。 */
QString fileStorageBackupDirectory()
{
    const auto applicationData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return applicationData.isEmpty() ? QString() : QDir(applicationData).filePath(QStringLiteral("backups"));
}

/** @brief 把原始设备 ID 编码成本机 UI 令牌；令牌不进入服务端设置或日志。 */
QString deviceToken(const QByteArray& deviceId)
{
    return QString::fromLatin1(deviceId.toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

/** @brief 为 QML 构造音频端点投影；只暴露名称、默认标记和本地令牌。 */
QVariantList audioDeviceProjection(const QList<QAudioDevice>& devices)
{
    QVariantList result;
    for (const auto& device : devices)
        result.push_back(QVariantMap{{QStringLiteral("token"), deviceToken(device.id())},
            {QStringLiteral("name"), device.description()},
            {QStringLiteral("isDefault"), device.isDefault()}});
    return result;
}

/** @brief 为 QML 构造摄像头投影；禁止暴露原始硬件 ID、位置序列号或驱动路径。 */
QVariantList cameraDeviceProjection(const QList<QCameraDevice>& devices)
{
    QVariantList result;
    for (const auto& device : devices)
        result.push_back(QVariantMap{{QStringLiteral("token"), deviceToken(device.id())},
            {QStringLiteral("name"), device.description()},
            {QStringLiteral("isDefault"), device.isDefault()}});
    return result;
}

/** @brief 在设备仍存在时沿用本机选择，否则回退到系统默认项并返回其令牌。 */
template<typename Device>
QString availableDeviceToken(const QList<Device>& devices, const QString& preferred)
{
    for (const auto& device : devices)
        if (deviceToken(device.id()) == preferred) return preferred;
    for (const auto& device : devices)
        if (device.isDefault()) return deviceToken(device.id());
    return devices.isEmpty() ? QString() : deviceToken(devices.front().id());
}

/** @brief 按本地令牌查找设备；令牌无效或设备已拔出时返回空可选值。 */
template<typename Device>
std::optional<Device> findDeviceByToken(const QList<Device>& devices, const QString& token)
{
    for (const auto& device : devices)
        if (deviceToken(device.id()) == token) return device;
    return std::nullopt;
}

/** @brief 仅允许关于页打开 HTTPS、mailto 和 tel 地址，避免部署变量注入本地文件或脚本协议。 */
bool isAllowedExternalUrl(const QUrl& url)
{
    const auto scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QStringLiteral("https")
        || scheme == QStringLiteral("mailto") || scheme == QStringLiteral("tel"));
}

/** @brief 将 64 位标识显式装入 QVariant，避免 QML 把数据库标识截断为 32 位。 */
QVariant identifier(qulonglong value)
{
    return QVariant::fromValue(value);
}

/** @brief 把服务端 Qt 资源标识转换为 QML 可加载 URL；普通网络/本地 URL 保持不变。 */
QString qmlAssetUrl(const QString& resourceId)
{
    const auto normalized = resourceId.trimmed();
    return normalized.startsWith(QStringLiteral(":/"))
        ? QStringLiteral("qrc:") + normalized.mid(1) : normalized;
}

/** @brief 生成仅用于界面和名片分享的手机号脱敏文本；短号码不推测结构。 */
QString maskedPhone(const QString& phone)
{
    const auto normalized = phone.trimmed();
    if (normalized.size() < 7) return normalized;
    return normalized.left(3) + QStringLiteral(" **** ") + normalized.right(4);
}

/** @brief 映射联系人权威资料；私有偏好与组织字段保持独立键，QML 不解释协议对象。 */
QVariantMap contactDetailMap(const RemoteContactDetail& remote)
{
    QVariantList groups;
    for (const auto& group : remote.groups)
        groups.push_back(QVariantMap{{QStringLiteral("groupId"), identifier(group.groupId)},
                          {QStringLiteral("name"), group.name},
                          {QStringLiteral("type"), group.groupType}});
    return {{QStringLiteral("personId"), identifier(remote.personId)},
        {QStringLiteral("displayName"), remote.displayName},
        {QStringLiteral("avatar"), qmlAssetUrl(remote.avatarResourceId)},
        {QStringLiteral("employeeNumber"), remote.employeeNumber},
        {QStringLiteral("phone"), remote.workPhone},
        {QStringLiteral("maskedPhone"), maskedPhone(remote.workPhone)},
        {QStringLiteral("extension"), remote.extensionNumber},
        {QStringLiteral("email"), remote.workEmail},
        {QStringLiteral("department"), remote.departmentName},
        {QStringLiteral("position"), remote.positionName},
        {QStringLiteral("office"), remote.officeLocation},
        {QStringLiteral("managerPersonId"), identifier(remote.managerPersonId)},
        {QStringLiteral("managerName"), remote.managerName},
        {QStringLiteral("presence"), remote.presenceState},
        {QStringLiteral("favorite"), remote.favorite},
        {QStringLiteral("note"), remote.note},
        {QStringLiteral("tags"), remote.tags}, {QStringLiteral("groups"), groups},
        {QStringLiteral("revision"), identifier(remote.revision)}};
}

/** @brief 将服务端会话摘要映射为只读 QML 展示投影。 */
QVariantMap conversationMap(const RemoteConversationSummary& item)
{
    return {{QStringLiteral("conversationId"), identifier(item.conversationId)},
            {QStringLiteral("peerPersonId"), identifier(item.peerPersonId)},
            {QStringLiteral("displayName"), item.displayName},
            {QStringLiteral("preview"), item.lastMessagePreview},
            {QStringLiteral("unread"), item.unreadCount},
            {QStringLiteral("pinned"), item.pinned},
            {QStringLiteral("muted"), item.muted},
            {QStringLiteral("time"), QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(item.lastActivityUtcMs)).toLocalTime().toString(QStringLiteral("HH:mm"))}};
}

/** @brief 映射群组列表；标签只用于视觉筛选，不作为权限依据。 */
QVariantMap groupMap(const RemoteGroupSummary& item)
{
    return {{QStringLiteral("groupId"), identifier(item.groupId)},
            {QStringLiteral("conversationId"), identifier(item.conversationId)},
            {QStringLiteral("code"), item.groupCode},
            {QStringLiteral("name"), item.name},
            {QStringLiteral("type"), item.type},
            {QStringLiteral("memberCount"), item.memberCount},
            {QStringLiteral("preview"), item.lastMessagePreview},
            {QStringLiteral("unread"), item.unreadCount},
            {QStringLiteral("activity"), item.activityScore},
            {QStringLiteral("tags"), item.tags},
            {QStringLiteral("owner"), item.owner},
            {QStringLiteral("administrator"), item.administrator},
            {QStringLiteral("favorite"), item.favorite}};
}

/** @brief 映射通知摘要并预格式化时间，QML 不解释协议枚举或 UTC。 */
QVariantMap notificationMap(const RemoteNotificationSummary& item)
{
    return {{QStringLiteral("notificationId"), identifier(item.notificationId)},
            {QStringLiteral("category"), item.category},
            {QStringLiteral("title"), item.title},
            {QStringLiteral("summary"), item.summary},
            {QStringLiteral("source"), item.sourceName},
            {QStringLiteral("priority"), item.priority},
            {QStringLiteral("status"), item.status},
            {QStringLiteral("actor"), item.actorDisplayName},
            {QStringLiteral("time"), QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(item.occurredAtUtcMs)).toLocalTime().toString(QStringLiteral("MM/dd HH:mm"))}};
}

/** @brief 映射文件中心行；资产标识只可回传 C++ 下载用例。 */
QVariantMap fileMap(const RemoteFileCenterItem& item)
{
    return {{QStringLiteral("itemUuid"), item.itemUuid},
            {QStringLiteral("kind"), item.kind},
            {QStringLiteral("name"), item.name},
            {QStringLiteral("assetUuid"), item.assetUuid},
            {QStringLiteral("mediaType"), item.mediaType},
            {QStringLiteral("category"), item.category},
            {QStringLiteral("sizeBytes"), identifier(item.sizeBytes)},
            {QStringLiteral("ownerPersonId"), identifier(item.ownerPersonId)},
            {QStringLiteral("owner"), item.ownerDisplayName},
            {QStringLiteral("location"), item.location},
            {QStringLiteral("modified"), QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(item.modifiedAtUtcMs)).toLocalTime().toString(QStringLiteral("MM/dd HH:mm"))},
            {QStringLiteral("favorite"), item.favorite},
            {QStringLiteral("deleted"), item.deleted},
            {QStringLiteral("sharedCount"), item.sharedCount},
            {QStringLiteral("revision"), identifier(item.revision)},
            {QStringLiteral("securityStatus"), item.securityStatus}};
}

/** @brief 映射联系人摘要，头像保持 Qt 资源 URL 或空值。 */
QVariantMap contactMap(const RemoteContactSummary& item)
{
    return {{QStringLiteral("personId"), identifier(item.personId)},
            {QStringLiteral("displayName"), item.displayName},
            {QStringLiteral("avatar"), qmlAssetUrl(item.avatarResourceId)},
            {QStringLiteral("presence"), item.presenceState},
            {QStringLiteral("favorite"), item.favorite},
            {QStringLiteral("interactionCount"), item.interactionCount}};
}

} // namespace

QmlClientBackend::QmlClientBackend(NetworkClient* networkClient, QObject* parent)
    : QObject(parent), networkClient_(networkClient), settingsModel_(this),
      fileTransferController_(networkClient, &settingsModel_, this)
{
    // 服务器地址属于设备级前端配置，不跟随账号漫游；无效旧值回退到安全的本机开发端点。
    const QSettings startupSettings;
    const auto configuredServer = startupSettings.value(
        QStringLiteral("connection/serverAddress"), loginServerAddress_).toString().trimmed();
    const QUrl configuredUrl(QStringLiteral("tcp://") + configuredServer, QUrl::StrictMode);
    if (configuredUrl.isValid() && !configuredUrl.host().isEmpty()
        && configuredUrl.port(-1) > 0 && configuredUrl.port(-1) <= 65535)
        loginServerAddress_ = configuredServer;
    // 设备监视器只枚举本机端点；原始硬件标识不会写入网络 DTO、日志或服务端设置。
    mediaDevices_ = new QMediaDevices(this);
    connect(mediaDevices_, &QMediaDevices::audioInputsChanged,
            this, &QmlClientBackend::rebuildCallDeviceProjection);
    connect(mediaDevices_, &QMediaDevices::audioOutputsChanged,
            this, &QmlClientBackend::rebuildCallDeviceProjection);
    connect(mediaDevices_, &QMediaDevices::videoInputsChanged,
            this, &QmlClientBackend::rebuildCallDeviceProjection);
    rebuildCallDeviceProjection();
    // 即使离线冒烟测试未装配 NetworkClient，关于页也必须展示真实客户端构建和本机环境。
    rebuildAboutSystemProjection();
    connect(&fileTransferController_, &FileTransferController::notificationRequested,
            this, &QmlClientBackend::showToast);
    connect(&fileTransferController_, &FileTransferController::previewRequested, this,
            [this](const QUrl& url, const QString& name, const QString& mediaType, int kind) {
        previewUrl_ = url;
        previewName_ = name;
        previewMediaType_ = mediaType;
        previewKind_ = kind;
        previewVisible_ = true;
        emit previewChanged();
    });
    connect(&fileTransferController_, &FileTransferController::fileAvailable, this,
            [this](const QString& assetUuid, const QString& localPath, const QString& mediaType) {
        if (fileDetail_.value(QStringLiteral("assetUuid")).toString() != assetUuid) return;
        fileDetail_.insert(QStringLiteral("localUrl"), QUrl::fromLocalFile(localPath));
        fileDetail_.insert(QStringLiteral("localMediaType"), mediaType);
        emit fileDetailChanged();
    });

    rebuildFileStorageProjection();
    if (networkClient_ == nullptr) return;
    connect(networkClient_, &NetworkClient::connectionStateChanged, this,
            [this](const QString& state, bool connected) {
        connected_ = connected;
        statusText_ = state;
        emit connectionChanged();
        emit statusTextChanged();
        rebuildFileStorageProjection();
        rebuildCallDeviceProjection();
    });
    connect(networkClient_, &NetworkClient::loginSucceeded, this,
            [this](qulonglong, qulonglong personId, qulonglong, const QString& displayName) {
        busy_ = false;
        emit busyChanged();
        initializeAuthenticatedSession(personId, displayName);
    });
    connect(networkClient_, &NetworkClient::loginFailed, this, [this](const QString& message) {
        busy_ = false;
        // 失败账号不进入已认证状态，避免后续公共头像误把登录名当作人员姓名展示。
        pendingLoginName_.clear();
        errorText_ = message;
        emit busyChanged();
        emit errorTextChanged();
    });
    connect(networkClient_, &NetworkClient::protocolWarning,
            this, &QmlClientBackend::showToast);
    connect(networkClient_, &NetworkClient::fileTransferFailed,
            this, &QmlClientBackend::showToast);
    connect(networkClient_, &NetworkClient::conferenceFailed,
            this, &QmlClientBackend::showToast);
    connect(networkClient_, &NetworkClient::conferenceReady, this,
            [this](const QUrl& joinUrl, const QString& conferenceUuid) {
        // 媒体采集只能在 HTTPS 安全上下文启用；拒绝 HTTP 可避免 Chromium 隐藏 mediaDevices 后
        // 产生 getUserMedia 未定义异常。短效令牌只停留在内存 URL fragment 中，关闭时立即清空。
        const auto scheme = joinUrl.scheme().toLower();
        if (!joinUrl.isValid() || scheme != QStringLiteral("https"))
        {
            showToast(QStringLiteral("会议服务必须使用 HTTPS 安全连接，请联系管理员检查会议配置。"));
            return;
        }
        conferenceUrl_ = joinUrl;
        conferenceUuid_ = conferenceUuid;
        conferenceVisible_ = true;
        emit conferenceChanged();
        showToast(QStringLiteral("音视频会议已在安域通应用内打开。"));
    });

    connect(networkClient_, &NetworkClient::conversationListReady, this,
            [this](const QList<RemoteConversationSummary>& remote) {
        conversations_.clear();
        unreadMessages_ = 0;
        for (const auto& item : remote)
        {
            conversations_.push_back(conversationMap(item));
            unreadMessages_ += std::max(0, item.unreadCount);
        }
        emit conversationsChanged();
        emit unreadMessagesChanged();
    });
    connect(networkClient_, &NetworkClient::messageHistoryReady, this,
            [this](qulonglong conversationId, const QList<RemoteMessageItem>& remote, bool) {
        if (conversationId == currentConversationId_) replaceMessages(remote);
    });
    connect(networkClient_, &NetworkClient::conversationReady, this,
            [this](qulonglong peerPersonId, qulonglong conversationId, const QString& displayName) {
        openConversation(conversationId, displayName);
        if (pendingConferencePersonId_ == peerPersonId)
        {
            const auto videoEnabled = pendingConferenceVideoEnabled_;
            pendingConferencePersonId_ = 0;
            pendingConferenceVideoEnabled_ = false;
            // 只有服务端确认单聊成员关系后才申请媒体凭据，避免用目录编号自行拼装会议房间。
            networkClient_->joinConference(conversationId, videoEnabled);
            showToast(videoEnabled ? QStringLiteral("正在准备视频会议…")
                                   : QStringLiteral("正在准备语音会议…"));
        }
        if (pendingFileTransferPersonId_ == peerPersonId)
        {
            pendingFileTransferPersonId_ = 0;
            // 文件选择器只能在权威 conversationId 已写入 currentConversationId_ 后打开。
            emit contactFileTransferReady();
        }
    });
    connect(networkClient_, &NetworkClient::conversationFailed, this,
            [this](qulonglong peerPersonId, const QString& friendlyMessage) {
        if (pendingConferencePersonId_ == peerPersonId)
        {
            pendingConferencePersonId_ = 0;
            pendingConferenceVideoEnabled_ = false;
        }
        if (pendingFileTransferPersonId_ == peerPersonId)
            pendingFileTransferPersonId_ = 0;
        showToast(friendlyMessage);
    });
    connect(networkClient_, &NetworkClient::directMessageReceived, this,
            [this](const QString& serverId, const QString& clientId, qulonglong conversationId,
                   qulonglong sequence, qulonglong senderId, const QString& text, qulonglong createdAt) {
        if (conversationId == currentConversationId_)
        {
            const auto duplicate = std::any_of(messages_.cbegin(), messages_.cend(),
                [&serverId](const QVariant& value) {
                return value.toMap().value(QStringLiteral("serverId")).toString() == serverId;
            });
            if (!duplicate)
            {
                messages_.push_back(QVariantMap{{QStringLiteral("serverId"), serverId},
                    {QStringLiteral("clientId"), clientId}, {QStringLiteral("sequence"), identifier(sequence)},
                    {QStringLiteral("senderId"), identifier(senderId)}, {QStringLiteral("outgoing"), false},
                    {QStringLiteral("kind"), 1}, {QStringLiteral("text"), text},
                    {QStringLiteral("time"), QDateTime::fromMSecsSinceEpoch(
                        static_cast<qint64>(createdAt)).toLocalTime().toString(QStringLiteral("HH:mm"))}});
                emit messagesChanged();
            }
            networkClient_->acknowledgeRead(serverId, conversationId, sequence);
        }
        networkClient_->acknowledgeDelivery(serverId, conversationId, sequence);
        emit incomingMessageReceived(conversationId);
        networkClient_->requestConversationList();
    });
    connect(networkClient_, &NetworkClient::fileMessageReceived, this,
            [this](const QString&, const QString&, qulonglong conversationId, qulonglong,
                   qulonglong, const QString&, qulonglong) {
        // 文件描述解析与资产去重统一由历史映射执行，避免推送和历史响应各追加一次文件名。
        if (conversationId == currentConversationId_)
            networkClient_->requestMessageHistory(conversationId, 0, 100);
        emit incomingMessageReceived(conversationId);
        networkClient_->requestConversationList();
    });

    connect(networkClient_, &NetworkClient::directorySnapshotReady, this,
            [this](const domain::OrganizationSnapshot& snapshot) {
        directoryUnits_.clear();
        directoryPeople_.clear();

        QHash<qulonglong, QString> organizationNames;
        QHash<qulonglong, qulonglong> organizationParents;
        QHash<qulonglong, QString> departmentNames;
        QHash<qulonglong, qulonglong> departmentParents;
        QHash<qulonglong, qulonglong> departmentOrganizations;
        QHash<qulonglong, QString> positionNames;
        QHash<qulonglong, int> departmentPeopleCounts;
        QHash<qulonglong, int> organizationPeopleCounts;
        QHash<qulonglong, domain::PresenceState> presenceByPerson;

        for (const auto& organization : snapshot.organizations)
        {
            const auto organizationId = organization.id.value();
            organizationNames.insert(organizationId, QString::fromStdString(organization.name));
            organizationParents.insert(organizationId,
                organization.parentId ? organization.parentId->value() : 0);
        }
        for (const auto& department : snapshot.departments)
        {
            const auto departmentId = department.id.value();
            departmentNames.insert(departmentId, QString::fromStdString(department.name));
            departmentParents.insert(departmentId,
                department.parentDepartmentId ? department.parentDepartmentId->value() : 0);
            departmentOrganizations.insert(departmentId, department.organizationId.value());
        }
        for (const auto& position : snapshot.positions)
            positionNames.insert(position.id.value(), QString::fromStdString(position.name));
        for (const auto& presence : snapshot.presences)
            presenceByPerson.insert(presence.personId.value(), presence.state);
        for (const auto& person : snapshot.people)
        {
            const auto departmentId = person.primaryDepartmentId
                ? person.primaryDepartmentId->value() : 0;
            if (departmentId != 0)
            {
                departmentPeopleCounts[departmentId] += 1;
                organizationPeopleCounts[departmentOrganizations.value(departmentId)] += 1;
            }
        }

        // 组织节点先于部门节点输出；稳定父键让 QML 能折叠显示而无需理解领域 StrongId。
        for (const auto& organization : snapshot.organizations)
        {
            const auto organizationId = organization.id.value();
            const auto parentId = organizationParents.value(organizationId);
            directoryUnits_.push_back(QVariantMap{
                {QStringLiteral("key"), QStringLiteral("organization:%1").arg(organizationId)},
                {QStringLiteral("parentKey"), parentId == 0 ? QString()
                    : QStringLiteral("organization:%1").arg(parentId)},
                {QStringLiteral("unitId"), identifier(organizationId)},
                {QStringLiteral("organizationId"), identifier(organizationId)},
                {QStringLiteral("type"), QStringLiteral("organization")},
                {QStringLiteral("name"), organizationNames.value(organizationId)},
                {QStringLiteral("depth"), parentId == 0 ? 0 : 1},
                {QStringLiteral("peopleCount"), organizationPeopleCounts.value(organizationId)}});
        }

        std::vector<const domain::Department*> sortedDepartments;
        sortedDepartments.reserve(snapshot.departments.size());
        for (const auto& department : snapshot.departments) sortedDepartments.push_back(&department);
        std::stable_sort(sortedDepartments.begin(), sortedDepartments.end(),
            [&departmentParents](const auto* left, const auto* right) {
                const auto depthOf = [&departmentParents](qulonglong id) {
                    int depth = 0;
                    QSet<qulonglong> visited;
                    while (id != 0 && departmentParents.contains(id) && depth < 32)
                    {
                        if (visited.contains(id)) break;
                        visited.insert(id);
                        id = departmentParents.value(id);
                        if (id != 0) ++depth;
                    }
                    return depth;
                };
                const auto leftDepth = depthOf(left->id.value());
                const auto rightDepth = depthOf(right->id.value());
                if (left->organizationId != right->organizationId)
                    return left->organizationId.value() < right->organizationId.value();
                if (leftDepth != rightDepth) return leftDepth < rightDepth;
                if (left->sortOrder != right->sortOrder) return left->sortOrder < right->sortOrder;
                return left->id.value() < right->id.value();
            });

        for (const auto* department : sortedDepartments)
        {
            const auto departmentId = department->id.value();
            const auto parentId = departmentParents.value(departmentId);
            int depth = 1;
            auto ancestorId = parentId;
            QSet<qulonglong> visited;
            while (ancestorId != 0 && depth < 32)
            {
                if (visited.contains(ancestorId)) break;
                visited.insert(ancestorId);
                ++depth;
                ancestorId = departmentParents.value(ancestorId);
            }
            directoryUnits_.push_back(QVariantMap{
                {QStringLiteral("key"), QStringLiteral("department:%1").arg(departmentId)},
                {QStringLiteral("parentKey"), parentId == 0
                    ? QStringLiteral("organization:%1").arg(department->organizationId.value())
                    : QStringLiteral("department:%1").arg(parentId)},
                {QStringLiteral("unitId"), identifier(departmentId)},
                {QStringLiteral("organizationId"), identifier(department->organizationId.value())},
                {QStringLiteral("type"), QStringLiteral("department")},
                {QStringLiteral("name"), departmentNames.value(departmentId)},
                {QStringLiteral("depth"), depth},
                {QStringLiteral("peopleCount"), departmentPeopleCounts.value(departmentId)}});
        }

        const auto presenceText = [](domain::PresenceState state) {
            switch (state)
            {
            case domain::PresenceState::Online: return QStringLiteral("在线");
            case domain::PresenceState::Busy: return QStringLiteral("忙碌");
            case domain::PresenceState::Away: return QStringLiteral("离开");
            case domain::PresenceState::DoNotDisturb: return QStringLiteral("勿扰");
            case domain::PresenceState::Invisible:
            case domain::PresenceState::Offline:
            default: return QStringLiteral("离线");
            }
        };
        for (const auto& person : snapshot.people)
        {
            const auto departmentId = person.primaryDepartmentId
                ? person.primaryDepartmentId->value() : 0;
            const auto positionId = person.primaryPositionId
                ? person.primaryPositionId->value() : 0;
            const auto organizationId = departmentOrganizations.value(departmentId);
            const auto presence = presenceByPerson.value(
                person.id.value(), domain::PresenceState::Offline);
            directoryPeople_.push_back(QVariantMap{
                {QStringLiteral("personId"), identifier(person.id.value())},
                {QStringLiteral("displayName"), QString::fromStdString(person.displayName)},
                {QStringLiteral("employeeNumber"), QString::fromStdString(person.employeeNumber)},
                {QStringLiteral("phone"), QString::fromStdString(person.workPhone)},
                {QStringLiteral("email"), QString::fromStdString(person.workEmail)},
                {QStringLiteral("avatar"), qmlAssetUrl(QString::fromStdString(person.avatarResourceId))},
                {QStringLiteral("organizationId"), identifier(organizationId)},
                {QStringLiteral("organizationName"), organizationNames.value(organizationId)},
                {QStringLiteral("departmentId"), identifier(departmentId)},
                {QStringLiteral("department"), departmentNames.value(departmentId)},
                {QStringLiteral("positionId"), identifier(positionId)},
                {QStringLiteral("position"), positionNames.value(positionId)},
                {QStringLiteral("presence"), static_cast<int>(presence)},
                {QStringLiteral("statusText"), presenceText(presence)}});
        }
        emit directoryUnitsChanged();
        emit directoryPeopleChanged();
    });
    connect(networkClient_, &NetworkClient::directorySnapshotFailed,
            this, &QmlClientBackend::showToast);

    connect(networkClient_, &NetworkClient::groupListReady, this,
            [this](const QList<RemoteGroupSummary>& remote, int total, int managed,
                   int active, int unread) {
        groups_.clear();
        for (const auto& item : remote) groups_.push_back(groupMap(item));
        groupDetail_.insert(QStringLiteral("totalCount"), total);
        groupDetail_.insert(QStringLiteral("managedCount"), managed);
        groupDetail_.insert(QStringLiteral("activeCount"), active);
        groupDetail_.insert(QStringLiteral("unreadCount"), unread);
        emit groupsChanged();
        emit groupDetailChanged();
    });
    connect(networkClient_, &NetworkClient::groupDetailReady, this,
            [this](const RemoteGroupDetail& remote) {
        QVariantList members;
        for (const auto& member : remote.members)
            members.push_back(QVariantMap{{QStringLiteral("personId"), identifier(member.personId)},
                {QStringLiteral("name"), member.displayName}, {QStringLiteral("department"), member.departmentName},
                {QStringLiteral("position"), member.positionName}, {QStringLiteral("avatar"), member.avatarResourceId},
                {QStringLiteral("role"), member.role}});
        QVariantList files;
        QSet<QString> seenAssets;
        for (const auto& file : remote.files)
        {
            if (file.assetUuid.isEmpty() || seenAssets.contains(file.assetUuid)) continue;
            seenAssets.insert(file.assetUuid);
            files.push_back(QVariantMap{{QStringLiteral("assetUuid"), file.assetUuid},
                {QStringLiteral("name"), file.fileName}, {QStringLiteral("mediaType"), file.mediaType},
                {QStringLiteral("sizeBytes"), identifier(file.sizeBytes)},
                {QStringLiteral("owner"), file.ownerDisplayName}});
        }
        groupDetail_ = groupMap(remote.group);
        groupDetail_.insert(QStringLiteral("ownerName"), remote.ownerDisplayName);
        groupDetail_.insert(QStringLiteral("announcement"), remote.announcement);
        groupDetail_.insert(QStringLiteral("members"), members);
        groupDetail_.insert(QStringLiteral("files"), files);
        emit groupDetailChanged();
    });
    connect(networkClient_, &NetworkClient::groupOperationFailed,
            this, &QmlClientBackend::showToast);

    connect(networkClient_, &NetworkClient::notificationListReady, this,
            [this](const QList<RemoteNotificationSummary>& remote,
                   const RemoteNotificationStatistics& statistics) {
        notifications_.clear();
        for (const auto& item : remote) notifications_.push_back(notificationMap(item));
        unreadNotifications_ = statistics.unreadCount;
        notificationStatistics_ = {
            {QStringLiteral("totalCount"), statistics.totalCount},
            {QStringLiteral("unreadCount"), statistics.unreadCount},
            {QStringLiteral("approvalCount"), statistics.approvalCount},
            {QStringLiteral("systemCount"), statistics.systemCount},
            {QStringLiteral("securityCount"), statistics.securityCount},
            {QStringLiteral("mentionCount"), statistics.mentionCount},
            {QStringLiteral("fileCount"), statistics.fileCount},
            {QStringLiteral("taskCount"), statistics.taskCount},
            {QStringLiteral("otherCount"), statistics.otherCount},
            {QStringLiteral("refreshedAt"), QDateTime::currentDateTime().toString(
                 QStringLiteral("yyyy-MM-dd HH:mm"))}};
        emit notificationsChanged();
        emit unreadNotificationsChanged();
        emit notificationStatisticsChanged();
    });
    connect(networkClient_, &NetworkClient::notificationDetailReady, this,
            [this](const RemoteNotificationDetail& remote) {
        notificationDetail_ = notificationMap(remote.notification);
        QVariantList fields;
        for (const auto& field : remote.fields)
            fields.push_back(QVariantMap{{QStringLiteral("label"), field.label},
                              {QStringLiteral("value"), field.value},
                              {QStringLiteral("emphasized"), field.emphasized}});
        QVariantList attachments;
        QSet<QString> seenAssets;
        for (const auto& item : remote.attachments)
        {
            if (item.assetUuid.isEmpty() || seenAssets.contains(item.assetUuid)) continue;
            seenAssets.insert(item.assetUuid);
            attachments.push_back(QVariantMap{{QStringLiteral("assetUuid"), item.assetUuid},
                {QStringLiteral("name"), item.fileName}, {QStringLiteral("mediaType"), item.mediaType},
                {QStringLiteral("sizeBytes"), identifier(item.sizeBytes)}});
        }
        notificationDetail_.insert(QStringLiteral("fields"), fields);
        notificationDetail_.insert(QStringLiteral("attachments"), attachments);
        notificationDetail_.insert(QStringLiteral("explanation"), remote.explanation);
        emit notificationDetailChanged();
    });
    connect(networkClient_, &NetworkClient::notificationStatusUpdated, this,
            [this](qulonglong, int, int unread) {
        unreadNotifications_ = unread;
        emit unreadNotificationsChanged();
        networkClient_->requestNotificationList();
    });
    connect(networkClient_, &NetworkClient::notificationOperationFailed,
            this, &QmlClientBackend::showToast);

    connect(networkClient_, &NetworkClient::contactCenterReady, this,
            [this](const QList<RemoteContactSummary>& recent, const QList<RemoteContactSummary>& favorites) {
        contacts_.clear();
        QSet<qulonglong> seen;
        for (const auto& list : {favorites, recent})
            for (const auto& item : list)
                if (!seen.contains(item.personId)) { seen.insert(item.personId); contacts_.push_back(contactMap(item)); }
        emit contactsChanged();
    });
    connect(networkClient_, &NetworkClient::contactDetailReady, this,
            [this](const RemoteContactDetail& remote) {
        const auto mapped = contactDetailMap(remote);
        if (remote.personId == currentPersonId_)
        {
            accountProfile_ = mapped;
            const auto authoritativeDisplayName = mapped.value(
                QStringLiteral("displayName")).toString().trimmed();
            if (!authoritativeDisplayName.isEmpty() && currentUser_ != authoritativeDisplayName)
            {
                // 人员详情可能比登录响应更新，头像旁姓名跟随权威主数据但账号名保持不变。
                currentUser_ = authoritativeDisplayName;
                emit currentDisplayNameChanged();
                emit currentUserChanged();
            }
            mergeAccountSystemInfo();
            emit accountProfileChanged();
        }
        // 自资料和通讯录详情共用协议响应，但只有当前选择目标可以更新联系人面板。
        if (remote.personId == selectedContactPersonId_)
        {
            contactDetail_ = mapped;
            emit contactDetailChanged();
        }
    });
    connect(networkClient_, &NetworkClient::contactOperationFailed,
            this, &QmlClientBackend::showToast);

    connect(networkClient_, &NetworkClient::fileCenterListReady, this,
            [this](const QList<RemoteFileCenterItem>& remote,
                   const RemoteFileCenterStatistics& statistics) {
        files_.clear();
        QSet<QString> seenItems;
        for (const auto& item : remote)
        {
            if (item.itemUuid.isEmpty() || seenItems.contains(item.itemUuid)) continue;
            seenItems.insert(item.itemUuid);
            files_.push_back(fileMap(item));
        }
        fileStatistics_ = {{QStringLiteral("totalCount"), statistics.totalCount},
            {QStringLiteral("usedBytes"), identifier(statistics.usedBytes)},
            {QStringLiteral("quotaBytes"), identifier(statistics.quotaBytes)},
            {QStringLiteral("documentBytes"), identifier(statistics.documentBytes)},
            {QStringLiteral("imageBytes"), identifier(statistics.imageBytes)},
            {QStringLiteral("videoBytes"), identifier(statistics.videoBytes)},
            {QStringLiteral("otherBytes"), identifier(statistics.otherBytes)}};
        emit filesChanged();
        emit fileStatisticsChanged();
        if (!remote.isEmpty()) networkClient_->requestFileCenterDetail(remote.constFirst().itemUuid);
    });
    connect(networkClient_, &NetworkClient::fileCenterDetailReady, this,
            [this](const RemoteFileCenterDetail& remote) {
        fileDetail_ = fileMap(remote.item);
        fileDetail_.insert(QStringLiteral("sha256"), remote.sha256Hex);
        fileDetail_.insert(QStringLiteral("created"), QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(remote.createdAtUtcMs)).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        QVariantList versions;
        for (const auto& version : remote.versions)
            versions.push_back(QVariantMap{{QStringLiteral("version"), version.versionNumber},
                {QStringLiteral("assetUuid"), version.assetUuid}, {QStringLiteral("sizeBytes"), identifier(version.sizeBytes)},
                {QStringLiteral("creator"), version.createdByDisplayName}, {QStringLiteral("current"), version.current}});
        QVariantList permissions;
        for (const auto& permission : remote.permissions)
            permissions.push_back(QVariantMap{{QStringLiteral("personId"), identifier(permission.personId)},
                {QStringLiteral("name"), permission.displayName}, {QStringLiteral("permission"), permission.permission}});
        fileDetail_.insert(QStringLiteral("versions"), versions);
        fileDetail_.insert(QStringLiteral("permissions"), permissions);
        emit fileDetailChanged();
    });
    connect(networkClient_, &NetworkClient::fileCenterFolderCreated, this,
            [this](const RemoteFileCenterItem&) { showToast(QStringLiteral("文件夹已创建。")); refreshCurrentSection(); });
    connect(networkClient_, &NetworkClient::fileCenterItemUpdated, this,
            [this](const RemoteFileCenterDetail&) { showToast(QStringLiteral("文件信息已更新。")); refreshCurrentSection(); });
    connect(networkClient_, &NetworkClient::fileCenterOperationFailed,
            this, &QmlClientBackend::showToast);
    connect(networkClient_, &NetworkClient::fileUploaded, this,
            [this](const QString&, const QString&, const QString&, qulonglong conversationId,
                   qulonglong, qulonglong) {
        showToast(QStringLiteral("文件传输完成。"));
        if (conversationId == 0) networkClient_->requestFileCenter();
        else if (conversationId == currentConversationId_)
            networkClient_->requestMessageHistory(conversationId, 0, 100);
    });

    connect(networkClient_, &NetworkClient::calendarEventsReady, this,
            [this](const QList<RemoteCalendarEvent>& remote) {
        calendarEvents_.clear();
        for (const auto& item : remote)
            calendarEvents_.push_back(QVariantMap{{QStringLiteral("eventUuid"), item.eventUuid},
                {QStringLiteral("title"), item.title}, {QStringLiteral("description"), item.description},
                {QStringLiteral("location"), item.location}, {QStringLiteral("calendar"), item.calendarName},
                {QStringLiteral("color"), item.color}, {QStringLiteral("organizer"), item.organizerDisplayName},
                {QStringLiteral("startsAt"), QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(item.startsAtUtcMs))},
                {QStringLiteral("endsAt"), QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(item.endsAtUtcMs))},
                {QStringLiteral("allDay"), item.allDay}, {QStringLiteral("cancelled"), item.cancelled},
                {QStringLiteral("meetingNumber"), item.meetingNumber}, {QStringLiteral("editable"), item.editable},
                {QStringLiteral("revision"), identifier(item.revision)}});
        emit calendarEventsChanged();
    });
    connect(networkClient_, &NetworkClient::calendarOperationFailed,
            this, &QmlClientBackend::showToast);

    connect(networkClient_, &NetworkClient::settingsReady, this,
            [this](const RemoteUserSettings& settings, const RemoteSettingsSystemInfo& info) {
        applySettings(settings, &info);
    });
    connect(networkClient_, &NetworkClient::settingsUpdated, this,
            [this](const RemoteUserSettings& settings) { applySettings(settings); showToast(QStringLiteral("设置已保存。")); });
    connect(networkClient_, &NetworkClient::settingsReset, this,
            [this](const RemoteUserSettings& settings) { applySettings(settings); showToast(QStringLiteral("已恢复默认设置。")); });
    connect(networkClient_, &NetworkClient::settingsOperationFailed,
            this, &QmlClientBackend::showToast);
}

QmlClientBackend::~QmlClientBackend()
{
    // 销毁前显式停采，避免依赖 QObject 子对象析构顺序延长摄像头占用时间。
    stopCameraPreview();
}

void QmlClientBackend::setCurrentSection(int section)
{
    const auto normalized = std::clamp(section, 0, 6);
    if (currentSection_ == normalized) return;
    currentSection_ = normalized;
    currentSearch_.clear();
    emit currentSectionChanged();
    refreshCurrentSection();
}

void QmlClientBackend::configureSystemTray(bool available)
{
    if (systemTrayAvailable_ == available) return;
    systemTrayAvailable_ = available;
    emit systemTrayAvailableChanged();
}

bool QmlClientBackend::requestWindowCloseToTray()
{
    if (!systemTrayAvailable_) return false;
    // 关闭事件必须同步得到“已接管”结果；实际隐藏由桌面控制器完成，QML 不接触平台 API。
    emit windowCloseToTrayRequested();
    return true;
}

void QmlClientBackend::acknowledgeWindowForeground()
{
    if (!systemTrayAvailable_) return;
    emit windowForegroundAcknowledged();
}

void QmlClientBackend::login(const QString& loginName, const QString& password)
{
    login(loginServerAddress_, loginName, password);
}

bool QmlClientBackend::configureLoginServerAddress(const QString& serverAddress)
{
    const auto candidate = serverAddress.trimmed();
    const QUrl parsed(QStringLiteral("tcp://") + candidate, QUrl::StrictMode);
    const auto port = parsed.port(-1);
    const bool validPath = parsed.path().isEmpty() || parsed.path() == QStringLiteral("/");
    if (!parsed.isValid() || parsed.host().isEmpty() || port <= 0 || port > 65535
        || !validPath || !parsed.query().isEmpty() || !parsed.fragment().isEmpty())
    {
        showToast(QStringLiteral("服务器地址格式无效，请输入“主机:端口”。"));
        return false;
    }

    const auto host = parsed.host().contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(parsed.host()) : parsed.host();
    const auto normalized = QStringLiteral("%1:%2").arg(host).arg(port);
    if (loginServerAddress_ == normalized) return true;

    // 只写入 Qt 的用户配置目录，禁止把运行配置写入安装目录或发送给业务服务端。
    QSettings localSettings;
    localSettings.setValue(QStringLiteral("connection/serverAddress"), normalized);
    localSettings.sync();
    if (localSettings.status() != QSettings::NoError)
    {
        showToast(QStringLiteral("服务器配置保存失败，请检查当前用户配置目录权限。"));
        return false;
    }
    loginServerAddress_ = normalized;
    emit loginServerAddressChanged();
    showToast(QStringLiteral("服务器配置已保存。"));
    return true;
}

void QmlClientBackend::login(
    const QString& serverAddress, const QString& loginName, const QString& password)
{
    errorText_.clear();
    emit errorTextChanged();
    if (serverAddress.trimmed().isEmpty() || !serverAddress.contains(QLatin1Char(':')))
    {
        errorText_ = QStringLiteral("请输入“主机:端口”格式的服务器地址。");
        emit errorTextChanged();
        return;
    }
    if (loginName.trimmed().isEmpty() || password.isEmpty())
    {
        errorText_ = QStringLiteral("请输入账号和密码。");
        emit errorTextChanged();
        return;
    }
    if (networkClient_ == nullptr)
    {
        errorText_ = QStringLiteral("当前构建未装配认证网络服务。");
        emit errorTextChanged();
        return;
    }
    // 登录账号只在认证窗口内暂存；人员姓名必须等待服务端身份响应，不能由账号推断。
    pendingLoginName_ = loginName.trimmed();
    busy_ = true;
    emit busyChanged();
    networkClient_->login(serverAddress.trimmed(), pendingLoginName_, password);
}

void QmlClientBackend::refreshCurrentSection()
{
    if (networkClient_ == nullptr || !authenticated_)
    {
        if (authenticated_) showToast(QStringLiteral("当前处于离线状态。"));
        return;
    }
    switch (currentSection_)
    {
    case 0: networkClient_->requestConversationList(); break;
    case 1: networkClient_->requestDirectorySync(0); networkClient_->requestContactCenter(); break;
    case 2: networkClient_->requestGroupList(0, currentSearch_); break;
    case 3: networkClient_->requestFileCenter(0, 0, currentSearch_, 0, 20); break;
    case 4: networkClient_->requestNotificationList(0, false, currentSearch_, 0, 20); break;
    case 5: requestCurrentCalendarRange(); break;
    case 6:
        networkClient_->requestSettings();
        if (currentPersonId_ != 0) networkClient_->requestContactDetail(currentPersonId_);
        break;
    default: break;
    }
}

void QmlClientBackend::globalSearch(const QString& keyword)
{
    currentSearch_ = keyword.trimmed();
    if (networkClient_ == nullptr || !authenticated_)
    {
        showToast(QStringLiteral("当前未连接服务端，无法执行全局搜索。"));
        return;
    }
    if (currentSection_ == 2) networkClient_->requestGroupList(0, currentSearch_);
    else if (currentSection_ == 3) networkClient_->requestFileCenter(0, 0, currentSearch_, 0, 20);
    else if (currentSection_ == 4)
        networkClient_->requestNotificationList(0, false, currentSearch_, 0, 20);
    else
        showToast(QStringLiteral("当前模块已在本页按“%1”筛选。").arg(currentSearch_));
}

void QmlClientBackend::openConversation(qulonglong conversationId, const QString& displayName)
{
    if (networkClient_ == nullptr || conversationId == 0) return;
    currentConversationId_ = conversationId;
    currentConversationName_ = displayName;
    messages_.clear();
    sharedFiles_.clear();
    emit messagesChanged();
    emit sharedFilesChanged();
    networkClient_->requestMessageHistory(conversationId, 0, 100);
}

void QmlClientBackend::startDirectConversation(qulonglong personId, const QString& displayName)
{
    if (networkClient_ == nullptr || !authenticated_ || !connected_ || personId == 0)
    {
        showToast(QStringLiteral("当前未连接服务端，无法打开联系人会话。"));
        return;
    }

    // 先切换到消息模块再异步申请会话；conversationReady 会写入权威会话编号并加载历史。
    if (currentSection_ != 0)
    {
        currentSection_ = 0;
        emit currentSectionChanged();
    }
    networkClient_->requestDirectConversation(personId, displayName.trimmed());
}

void QmlClientBackend::startContactConference(qulonglong personId, const QString& displayName,
                                              bool videoEnabled)
{
    if (networkClient_ == nullptr || !authenticated_ || !connected_ || personId == 0)
    {
        showToast(QStringLiteral("当前未连接服务端，无法发起联系人通话。"));
        return;
    }

    // 目录人员编号不能直接作为会议房间；保存短暂意图并等待服务端创建/复用单聊会话。
    pendingConferencePersonId_ = personId;
    pendingConferenceVideoEnabled_ = videoEnabled;
    networkClient_->requestDirectConversation(personId, displayName.trimmed());
    showToast(videoEnabled ? QStringLiteral("正在建立视频通话…")
                           : QStringLiteral("正在建立语音通话…"));
}

void QmlClientBackend::prepareContactFileTransfer(qulonglong personId,
                                                  const QString& displayName)
{
    if (networkClient_ == nullptr || !authenticated_ || !connected_ || personId == 0)
    {
        showToast(QStringLiteral("当前未连接服务端，无法向联系人发送文件。"));
        return;
    }
    pendingFileTransferPersonId_ = personId;
    networkClient_->requestDirectConversation(personId, displayName.trimmed());
    showToast(QStringLiteral("正在建立安全文件会话…"));
}

void QmlClientBackend::sendMessage(const QString& text)
{
    const auto normalized = text.trimmed();
    if (networkClient_ == nullptr || currentConversationId_ == 0 || normalized.isEmpty()) return;
    const auto clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    messages_.push_back(QVariantMap{{QStringLiteral("clientId"), clientId},
        {QStringLiteral("senderId"), identifier(currentPersonId_)}, {QStringLiteral("outgoing"), true},
        {QStringLiteral("kind"), 1}, {QStringLiteral("text"), normalized},
        {QStringLiteral("status"), QStringLiteral("发送中")},
        {QStringLiteral("time"), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"))}});
    emit messagesChanged();
    networkClient_->sendTextMessage(currentConversationId_, clientId, normalized);
}

void QmlClientBackend::startConference(qulonglong conversationId, bool videoEnabled)
{
    const auto effectiveConversationId = conversationId == 0
        ? currentConversationId_ : conversationId;
    if (networkClient_ == nullptr || !authenticated_ || !connected_)
    {
        showToast(QStringLiteral("当前未连接服务端，无法发起音视频会议。"));
        return;
    }
    if (effectiveConversationId == 0)
    {
        showToast(QStringLiteral("请先选择一个会话。"));
        return;
    }

    // 服务端重新校验会话成员身份并签发短效 LiveKit JWT，客户端不能自行拼装房间或访问令牌。
    conferenceTitle_ = videoEnabled ? QStringLiteral("安域通视频会议")
                                    : QStringLiteral("安域通语音会议");
    networkClient_->joinConference(effectiveConversationId, videoEnabled);
    showToast(videoEnabled ? QStringLiteral("正在准备视频会议…")
                           : QStringLiteral("正在准备语音会议…"));
}

void QmlClientBackend::closeConference()
{
    if (!conferenceVisible_ && conferenceUrl_.isEmpty() && conferenceUuid_.isEmpty()) return;

    const auto conferenceUuid = conferenceUuid_;
    // 先清理 WebView 地址，确保 fragment 中的短效令牌不会继续驻留在界面或导航历史中。
    conferenceVisible_ = false;
    conferenceUrl_.clear();
    conferenceUuid_.clear();
    conferenceTitle_ = QStringLiteral("安域通会议");
    emit conferenceChanged();

    if (networkClient_ != nullptr && !conferenceUuid.isEmpty())
        networkClient_->leaveConference(conferenceUuid);
}

void QmlClientBackend::selectGroup(qulonglong groupId)
{
    if (networkClient_ != nullptr && groupId != 0) networkClient_->requestGroupDetail(groupId);
}

void QmlClientBackend::selectNotification(qulonglong notificationId)
{
    if (networkClient_ != nullptr && notificationId != 0)
        networkClient_->requestNotificationDetail(notificationId);
}

void QmlClientBackend::selectContact(qulonglong personId)
{
    selectedContactPersonId_ = personId;
    if (networkClient_ != nullptr && personId != 0) networkClient_->requestContactDetail(personId);
}

void QmlClientBackend::selectFile(const QString& itemUuid)
{
    if (networkClient_ != nullptr && !itemUuid.isEmpty()) networkClient_->requestFileCenterDetail(itemUuid);
}

void QmlClientBackend::selectCalendarEvent(const QString& eventUuid)
{
    const auto found = std::find_if(calendarEvents_.cbegin(), calendarEvents_.cend(),
        [&eventUuid](const QVariant& value) {
        return value.toMap().value(QStringLiteral("eventUuid")).toString() == eventUuid;
    });
    if (found != calendarEvents_.cend()) showToast(found->toMap().value(QStringLiteral("title")).toString());
}

void QmlClientBackend::uploadFile(const QUrl& localUrl, qulonglong conversationId)
{
    if (networkClient_ == nullptr || !localUrl.isLocalFile()) return;
    const auto path = localUrl.toLocalFile();
    if (!QFileInfo(path).isFile()) { showToast(QStringLiteral("请选择有效的本地文件。")); return; }
    const auto effectiveConversationId = conversationId == 0
        ? currentConversationId_ : conversationId;
    if (effectiveConversationId == 0)
    {
        showToast(QStringLiteral("请先打开联系人或群组会话。"));
        return;
    }
    if (networkClient_->uploadFile(effectiveConversationId, path).isEmpty())
        showToast(QStringLiteral("文件未进入上传队列。"));
    else
        showToast(QStringLiteral("文件正在上传…"));
}

void QmlClientBackend::openAsset(const QString& assetUuid)
{
    fileTransferController_.requestOpen(assetUuid);
}

void QmlClientBackend::downloadAsset(const QString& assetUuid)
{
    fileTransferController_.requestDownload(assetUuid);
}

void QmlClientBackend::createFolder(const QString& name)
{
    if (networkClient_ != nullptr && !name.trimmed().isEmpty())
        networkClient_->createFileCenterFolder({}, name.trimmed());
}

void QmlClientBackend::toggleCurrentFileFavorite()
{
    if (networkClient_ == nullptr || fileDetail_.isEmpty()) return;
    networkClient_->updateFileCenterItem(fileDetail_.value(QStringLiteral("itemUuid")).toString(),
        fileDetail_.value(QStringLiteral("revision")).toULongLong(), 1,
        !fileDetail_.value(QStringLiteral("favorite")).toBool());
}

void QmlClientBackend::recycleCurrentFile()
{
    if (networkClient_ == nullptr || fileDetail_.isEmpty()) return;
    const auto restore = fileDetail_.value(QStringLiteral("deleted")).toBool();
    networkClient_->updateFileCenterItem(fileDetail_.value(QStringLiteral("itemUuid")).toString(),
        fileDetail_.value(QStringLiteral("revision")).toULongLong(), restore ? 3 : 2);
}

void QmlClientBackend::markCurrentNotificationRead()
{
    const auto id = notificationDetail_.value(QStringLiteral("notificationId")).toULongLong();
    if (networkClient_ != nullptr && id != 0) networkClient_->updateNotificationStatus(id, 1);
}

void QmlClientBackend::updateSetting(const QString& key, const QVariant& value)
{
    if (networkClient_ == nullptr || remoteSettings_.revision == 0) return;
    if (key == QStringLiteral("twoFactorEnabled")) remoteSettings_.twoFactorEnabled = value.toBool();
    else if (key == QStringLiteral("startupEnabled")) remoteSettings_.startupEnabled = value.toBool();
    else if (key == QStringLiteral("autoLoginEnabled")) remoteSettings_.autoLoginEnabled = value.toBool();
    else if (key == QStringLiteral("chatWatermarkEnabled")) remoteSettings_.chatWatermarkEnabled = value.toBool();
    else if (key == QStringLiteral("screenshotProtectionEnabled")) remoteSettings_.screenshotProtectionEnabled = value.toBool();
    else if (key == QStringLiteral("autoLockMinutes")) remoteSettings_.autoLockMinutes = value.toInt();
    else if (key == QStringLiteral("downloadPath")) remoteSettings_.downloadPath = value.toString();
    else if (key == QStringLiteral("language")) remoteSettings_.language = value.toString();
    else if (key == QStringLiteral("theme")) remoteSettings_.theme = value.toString();
    else if (key == QStringLiteral("phoneVisibility")) remoteSettings_.phoneVisibility = value.toInt();
    else if (key == QStringLiteral("emailVisibility")) remoteSettings_.emailVisibility = value.toInt();
    else if (key == QStringLiteral("searchVisibility")) remoteSettings_.searchVisibility = value.toInt();
    else if (key == QStringLiteral("phoneSearchEnabled")) remoteSettings_.phoneSearchEnabled = value.toBool();
    else if (key == QStringLiteral("profileSignature"))
        remoteSettings_.profileSignature = value.toString().trimmed().left(160);
    else if (key == QStringLiteral("newMessageNotificationEnabled")) remoteSettings_.newMessageNotificationEnabled = value.toBool();
    else if (key == QStringLiteral("notificationSoundEnabled")) remoteSettings_.notificationSoundEnabled = value.toBool();
    else if (key == QStringLiteral("notificationSoundName")) remoteSettings_.notificationSoundName = value.toString().trimmed().left(64);
    else if (key == QStringLiteral("desktopPopupEnabled")) remoteSettings_.desktopPopupEnabled = value.toBool();
    else if (key == QStringLiteral("unreadBadgeEnabled")) remoteSettings_.unreadBadgeEnabled = value.toBool();
    else if (key == QStringLiteral("mentionNotificationEnabled")) remoteSettings_.mentionNotificationEnabled = value.toBool();
    else if (key == QStringLiteral("groupNotificationLevel")) remoteSettings_.groupNotificationLevel = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("systemNotificationEnabled")) remoteSettings_.systemNotificationEnabled = value.toBool();
    else if (key == QStringLiteral("approvalNotificationEnabled")) remoteSettings_.approvalNotificationEnabled = value.toBool();
    else if (key == QStringLiteral("fileNotificationEnabled")) remoteSettings_.fileNotificationEnabled = value.toBool();
    else if (key == QStringLiteral("calendarNotificationEnabled")) remoteSettings_.calendarNotificationEnabled = value.toBool();
    else if (key == QStringLiteral("calendarReminderMinutes")) remoteSettings_.calendarReminderMinutes = std::clamp(value.toInt(), 0, 10'080);
    else if (key == QStringLiteral("doNotDisturbEnabled")) remoteSettings_.doNotDisturbEnabled = value.toBool();
    else if (key == QStringLiteral("doNotDisturbStartMinutes")) remoteSettings_.doNotDisturbStartMinutes = std::clamp(value.toInt(), 0, 1'439);
    else if (key == QStringLiteral("doNotDisturbEndMinutes")) remoteSettings_.doNotDisturbEndMinutes = std::clamp(value.toInt(), 0, 1'439);
    else if (key == QStringLiteral("notificationPreviewMode")) remoteSettings_.notificationPreviewMode = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("readReceiptEnabled")) remoteSettings_.readReceiptEnabled = value.toBool();
    else if (key == QStringLiteral("enterToSendEnabled")) remoteSettings_.enterToSendEnabled = value.toBool();
    else if (key == QStringLiteral("messageBubbleDensity")) remoteSettings_.messageBubbleDensity = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("primaryColor")) remoteSettings_.primaryColor = value.toString().trimmed().left(9);
    else if (key == QStringLiteral("accentColor")) remoteSettings_.accentColor = value.toString().trimmed().left(9);
    else if (key == QStringLiteral("sidebarStyle")) remoteSettings_.sidebarStyle = std::clamp(value.toInt(), 0, 3);
    else if (key == QStringLiteral("cardRadiusMode")) remoteSettings_.cardRadiusMode = std::clamp(value.toInt(), 0, 3);
    else if (key == QStringLiteral("uiDensity")) remoteSettings_.uiDensity = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("fontSizeMode")) remoteSettings_.fontSizeMode = std::clamp(value.toInt(), 0, 3);
    else if (key == QStringLiteral("chatBackground")) remoteSettings_.chatBackground = value.toString().trimmed().left(64);
    else if (key == QStringLiteral("messageBubbleStyle")) remoteSettings_.messageBubbleStyle = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("contentViewMode")) remoteSettings_.contentViewMode = std::clamp(value.toInt(), 0, 1);
    else if (key == QStringLiteral("windowTransparency")) remoteSettings_.windowTransparency = std::clamp(value.toInt(), 0, 40);
    else if (key == QStringLiteral("animationEnabled")) remoteSettings_.animationEnabled = value.toBool();
    else if (key == QStringLiteral("animationIntensity")) remoteSettings_.animationIntensity = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("autoSaveReceivedFiles")) remoteSettings_.autoSaveReceivedFiles = value.toBool();
    else if (key == QStringLiteral("recentFileRetentionDays"))
        remoteSettings_.recentFileRetentionDays = std::clamp(value.toInt(), 1, 3'650);
    else if (key == QStringLiteral("autoCacheCleanupEnabled")) remoteSettings_.autoCacheCleanupEnabled = value.toBool();
    else if (key == QStringLiteral("cacheSizeLimitMb"))
        remoteSettings_.cacheSizeLimitMb = std::clamp(value.toInt(), 256, 102'400);
    else if (key == QStringLiteral("filePreviewMode")) remoteSettings_.filePreviewMode = std::clamp(value.toInt(), 0, 1);
    else if (key == QStringLiteral("imageAutoCompressEnabled")) remoteSettings_.imageAutoCompressEnabled = value.toBool();
    else if (key == QStringLiteral("videoTranscodeMode")) remoteSettings_.videoTranscodeMode = std::clamp(value.toInt(), 0, 1);
    else if (key == QStringLiteral("fileEncryptionMode")) remoteSettings_.fileEncryptionMode = std::clamp(value.toInt(), 0, 1);
    else if (key == QStringLiteral("externalWatermarkMode")) remoteSettings_.externalWatermarkMode = std::clamp(value.toInt(), 0, 1);
    else if (key == QStringLiteral("defaultSharePermission")) remoteSettings_.defaultSharePermission = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("syncFolderPath"))
        remoteSettings_.syncFolderPath = QDir::cleanPath(value.toString()).left(1024);
    else if (key == QStringLiteral("echoCancellationEnabled")) remoteSettings_.echoCancellationEnabled = value.toBool();
    else if (key == QStringLiteral("noiseSuppressionEnabled")) remoteSettings_.noiseSuppressionEnabled = value.toBool();
    else if (key == QStringLiteral("autoGainControlEnabled")) remoteSettings_.autoGainControlEnabled = value.toBool();
    else if (key == QStringLiteral("cameraMirrorEnabled")) remoteSettings_.cameraMirrorEnabled = value.toBool();
    else if (key == QStringLiteral("videoResolutionMode")) remoteSettings_.videoResolutionMode = std::clamp(value.toInt(), 0, 2);
    else if (key == QStringLiteral("bandwidthOptimizationEnabled")) remoteSettings_.bandwidthOptimizationEnabled = value.toBool();
    else if (key == QStringLiteral("recordingPermissionEnabled")) remoteSettings_.recordingPermissionEnabled = value.toBool();
    else if (key == QStringLiteral("incomingCallWindowPosition")) remoteSettings_.incomingCallWindowPosition = std::clamp(value.toInt(), 0, 3);
    else if (key == QStringLiteral("bluetoothPreferred")) remoteSettings_.bluetoothPreferred = value.toBool();
    else if (key == QStringLiteral("callShortcut"))
        remoteSettings_.callShortcut = value.toString().trimmed().left(64);
    else return;
    networkClient_->updateSettings(remoteSettings_);
}

void QmlClientBackend::setDownloadDirectory(const QUrl& directoryUrl)
{
    if (!directoryUrl.isLocalFile())
    {
        showToast(QStringLiteral("下载目录必须是当前设备上的本地目录。"));
        return;
    }
    const auto normalizedPath = QDir::cleanPath(directoryUrl.toLocalFile());
    if (!QFileInfo(normalizedPath).isDir())
    {
        showToast(QStringLiteral("所选下载目录不存在或不可访问。"));
        return;
    }
    // 路径只作为当前账号偏好提交；服务端不会访问客户端文件系统，实际下载仍由文件控制器执行。
    updateSetting(QStringLiteral("downloadPath"), normalizedPath);
}

void QmlClientBackend::refreshAccountProfile()
{
    if (networkClient_ == nullptr || !authenticated_ || currentPersonId_ == 0)
    {
        showToast(QStringLiteral("当前未连接服务端，无法刷新账号资料。"));
        return;
    }
    networkClient_->requestContactDetail(currentPersonId_);
    networkClient_->requestSettings();
}

void QmlClientBackend::refreshNotificationSettings()
{
    if (networkClient_ == nullptr || !authenticated_)
    {
        showToast(QStringLiteral("当前未连接服务端，无法刷新消息与通知设置。"));
        return;
    }
    networkClient_->requestSettings();
    networkClient_->requestNotificationList(0, false, {}, 0, 20);
}

void QmlClientBackend::sendTestNotification()
{
    if (remoteSettings_.revision == 0)
    {
        showToast(QStringLiteral("通知设置尚未加载完成。"));
        return;
    }
    // 生产 Qt Quick 同时服务桌面和移动端，系统级通知权限由各平台部署层管理；此处只触发可信应用内预览。
    emit testNotificationRequested();
    if (!remoteSettings_.newMessageNotificationEnabled)
        showToast(QStringLiteral("测试提醒已显示；当前新消息提醒总开关处于关闭状态。"));
    else
        showToast(QStringLiteral("测试提醒已发送，请查看右侧通知效果预览。"));
}

void QmlClientBackend::refreshAppearanceSettings()
{
    if (networkClient_ == nullptr || !authenticated_)
    {
        showToast(QStringLiteral("当前未连接服务端，无法刷新界面与主题设置。"));
        return;
    }
    networkClient_->requestSettings();
}

void QmlClientBackend::refreshSecuritySettings()
{
    if (networkClient_ == nullptr || !authenticated_)
    {
        showToast(QStringLiteral("当前未连接服务端，无法刷新安全与登录设置。"));
        return;
    }
    // 设置响应同时携带设备数量、证书、传输加密和国密能力，避免多个摘要请求产生不一致视图。
    networkClient_->requestSettings();
}

void QmlClientBackend::refreshFileStorageSettings()
{
    // 本地缓存与备份状态可以立即刷新；服务端分类占用随后由设置响应覆盖。
    rebuildFileStorageProjection(true);
    if (networkClient_ == nullptr || !authenticated_)
    {
        showToast(QStringLiteral("当前未连接服务端，仅刷新了本地缓存与备份状态。"));
        return;
    }
    networkClient_->requestSettings();
}

void QmlClientBackend::setSyncDirectory(const QUrl& directoryUrl)
{
    if (!directoryUrl.isLocalFile())
    {
        showToast(QStringLiteral("同步目录必须是当前设备上的本地目录。"));
        return;
    }
    const auto normalizedPath = QDir::cleanPath(directoryUrl.toLocalFile());
    if (!QFileInfo(normalizedPath).isDir())
    {
        showToast(QStringLiteral("所选同步目录不存在或不可访问。"));
        return;
    }
    // 服务端只保存跨端设置快照中的路径文本，不会使用该路径访问客户端设备。
    updateSetting(QStringLiteral("syncFolderPath"), normalizedPath);
}

void QmlClientBackend::clearLocalFileCache()
{
    const auto cachePath = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    const auto executableDirectory = QDir::cleanPath(QCoreApplication::applicationDirPath());
    if (cachePath.isEmpty() || QDir(cachePath).isRoot()
        || cachePath.compare(executableDirectory, Qt::CaseInsensitive) == 0)
    {
        // 受控目录校验失败时宁可拒绝清理，也不能扩大删除范围。
        showToast(QStringLiteral("缓存目录安全校验失败，未删除任何文件。"));
        return;
    }
    QDir cacheDirectory(cachePath);
    if (!cacheDirectory.exists())
    {
        QDir().mkpath(cachePath);
        rebuildFileStorageProjection(true);
        showToast(QStringLiteral("当前没有可清理的文件缓存。"));
        return;
    }
    bool success = true;
    const auto entries = cacheDirectory.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto& entry : entries)
    {
        // 符号链接按文件本身移除且绝不跟随；普通子目录才允许递归删除。
        if (entry.isSymLink() || entry.isFile()) success = QFile::remove(entry.filePath()) && success;
        else if (entry.isDir()) success = QDir(entry.filePath()).removeRecursively() && success;
    }
    QDir().mkpath(cachePath);
    rebuildFileStorageProjection(true);
    showToast(success ? QStringLiteral("文件缓存已清理。")
                      : QStringLiteral("部分缓存正在被占用，未能全部清理。"));
}

void QmlClientBackend::createFileStorageBackup()
{
    if (remoteSettings_.revision == 0)
    {
        showToast(QStringLiteral("文件与存储设置尚未加载，无法创建备份。"));
        return;
    }
    const auto backupDirectory = fileStorageBackupDirectory();
    if (backupDirectory.isEmpty() || !QDir().mkpath(backupDirectory))
    {
        showToast(QStringLiteral("无法创建用户备份目录。"));
        return;
    }
    // 备份仅包含文件策略，不写入口令、会话令牌、对象键、账号标识或聊天内容。
    QJsonObject settings{{QStringLiteral("schema"), 1},
        {QStringLiteral("revision"), QString::number(remoteSettings_.revision)},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("downloadPath"), remoteSettings_.downloadPath},
        {QStringLiteral("autoSaveReceivedFiles"), remoteSettings_.autoSaveReceivedFiles},
        {QStringLiteral("recentFileRetentionDays"), remoteSettings_.recentFileRetentionDays},
        {QStringLiteral("autoCacheCleanupEnabled"), remoteSettings_.autoCacheCleanupEnabled},
        {QStringLiteral("cacheSizeLimitMb"), remoteSettings_.cacheSizeLimitMb},
        {QStringLiteral("filePreviewMode"), remoteSettings_.filePreviewMode},
        {QStringLiteral("imageAutoCompressEnabled"), remoteSettings_.imageAutoCompressEnabled},
        {QStringLiteral("videoTranscodeMode"), remoteSettings_.videoTranscodeMode},
        {QStringLiteral("fileEncryptionMode"), remoteSettings_.fileEncryptionMode},
        {QStringLiteral("externalWatermarkMode"), remoteSettings_.externalWatermarkMode},
        {QStringLiteral("defaultSharePermission"), remoteSettings_.defaultSharePermission},
        {QStringLiteral("syncFolderPath"), remoteSettings_.syncFolderPath}};
    QSaveFile output(QDir(backupDirectory).filePath(QStringLiteral("file-storage-settings.json")));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)
        || output.write(QJsonDocument(settings).toJson(QJsonDocument::Indented)) < 0
        || !output.commit())
    {
        showToast(QStringLiteral("文件与存储设置备份失败，未留下不完整文件。"));
        return;
    }
    rebuildFileStorageProjection();
    showToast(QStringLiteral("文件与存储设置已备份到用户数据目录。"));
}

void QmlClientBackend::openFileStorageLocation(const QString& locationKind)
{
    QString target;
    if (locationKind == QStringLiteral("offline"))
    {
        const auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        target = dataPath.isEmpty() ? QString() : QDir(dataPath).filePath(QStringLiteral("offline-files"));
        if (!target.isEmpty()) QDir().mkpath(target);
    }
    else if (locationKind == QStringLiteral("sync"))
    {
        target = remoteSettings_.syncFolderPath;
    }
    else if (locationKind == QStringLiteral("backup"))
    {
        target = fileStorageBackupDirectory();
        if (!target.isEmpty()) QDir().mkpath(target);
    }
    const auto normalizedPath = QDir::cleanPath(target);
    if (normalizedPath.isEmpty() || !QFileInfo(normalizedPath).isDir())
    {
        showToast(QStringLiteral("目标目录尚未配置或不可访问。"));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(normalizedPath)))
        showToast(QStringLiteral("当前平台无法打开该本地目录。"));
}

void QmlClientBackend::refreshCallDeviceSettings()
{
    rebuildCallDeviceProjection();
    if (networkClient_ == nullptr || !authenticated_)
    {
        showToast(QStringLiteral("当前未连接服务端，仅刷新了本机音视频设备。"));
        return;
    }
    networkClient_->requestSettings();
}

void QmlClientBackend::selectCallDevice(
    const QString& deviceKind, const QString& selectedToken)
{
    QSettings localSettings;
    if (deviceKind == QStringLiteral("microphone"))
    {
        if (!findDeviceByToken(QMediaDevices::audioInputs(), selectedToken)) return;
        localSettings.setValue(QStringLiteral("callDevices/microphoneToken"), selectedToken);
    }
    else if (deviceKind == QStringLiteral("speaker"))
    {
        if (!findDeviceByToken(QMediaDevices::audioOutputs(), selectedToken)) return;
        localSettings.setValue(QStringLiteral("callDevices/speakerToken"), selectedToken);
    }
    else if (deviceKind == QStringLiteral("camera"))
    {
        if (!findDeviceByToken(QMediaDevices::videoInputs(), selectedToken)) return;
        // 切换摄像头前先停止旧采集，防止两个驱动端点被同时占用。
        stopCameraPreview();
        localSettings.setValue(QStringLiteral("callDevices/cameraToken"), selectedToken);
    }
    else
    {
        return;
    }
    localSettings.sync();
    callDeviceTestStatus_.clear();
    rebuildCallDeviceProjection();
}

void QmlClientBackend::testCallDevice(const QString& deviceKind)
{
    const auto selectedMicrophone = callDeviceInfo_.value(
        QStringLiteral("selectedMicrophoneToken")).toString();
    const auto selectedSpeaker = callDeviceInfo_.value(
        QStringLiteral("selectedSpeakerToken")).toString();
    bool opened = false;
    if (deviceKind == QStringLiteral("microphone"))
    {
        const auto device = findDeviceByToken(QMediaDevices::audioInputs(), selectedMicrophone);
        if (device)
        {
            // 短暂打开输入流只验证权限和驱动可用性，不录制、不保存也不上传音频数据。
            QAudioSource source(*device, device->preferredFormat());
            QIODevice* input = source.start();
            opened = input != nullptr && source.error() == QtAudio::NoError;
            source.stop();
        }
    }
    else if (deviceKind == QStringLiteral("speaker"))
    {
        const auto device = findDeviceByToken(QMediaDevices::audioOutputs(), selectedSpeaker);
        if (device)
        {
            // 输出测试只打开 Qt 写入端点，不生成高音量测试音，避免突然播放干扰用户。
            QAudioSink sink(*device, device->preferredFormat());
            QIODevice* output = sink.start();
            opened = output != nullptr && sink.error() == QtAudio::NoError;
            sink.stop();
        }
    }
    else
    {
        return;
    }
    callDeviceTestStatus_ = opened
        ? QStringLiteral("设备打开测试通过") : QStringLiteral("设备无法打开或权限未授权");
    rebuildCallDeviceProjection();
    showToast(callDeviceTestStatus_);
}

void QmlClientBackend::startCameraPreview(QObject* videoSinkObject)
{
    auto* videoSink = qobject_cast<QVideoSink*>(videoSinkObject);
    const auto selectedToken = callDeviceInfo_.value(
        QStringLiteral("selectedCameraToken")).toString();
    const auto selectedCamera = findDeviceByToken(QMediaDevices::videoInputs(), selectedToken);
    if (videoSink == nullptr || !selectedCamera)
    {
        showToast(QStringLiteral("未找到可用于预览的摄像头。"));
        return;
    }
    stopCameraPreview();
    captureSession_ = new QMediaCaptureSession(this);
    camera_ = new QCamera(*selectedCamera, this);
    connect(camera_, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString&) {
        // 驱动错误不把可能含设备路径的原始文本暴露到日志或 UI。
        callDeviceTestStatus_ = QStringLiteral("摄像头启动失败或权限未授权");
        rebuildCallDeviceProjection();
        showToast(callDeviceTestStatus_);
    });
    captureSession_->setCamera(camera_);
    captureSession_->setVideoSink(videoSink);
    camera_->start();
    callDeviceTestStatus_ = QStringLiteral("摄像头预览已启动");
    rebuildCallDeviceProjection();
}

void QmlClientBackend::stopCameraPreview()
{
    if (camera_ != nullptr) camera_->stop();
    if (captureSession_ != nullptr)
    {
        captureSession_->setVideoSink(nullptr);
        captureSession_->setCamera(nullptr);
    }
    delete camera_;
    delete captureSession_;
    camera_ = nullptr;
    captureSession_ = nullptr;
    rebuildCallDeviceProjection();
}

void QmlClientBackend::resetCallDevices()
{
    stopCameraPreview();
    QSettings localSettings;
    localSettings.remove(QStringLiteral("callDevices/microphoneToken"));
    localSettings.remove(QStringLiteral("callDevices/speakerToken"));
    localSettings.remove(QStringLiteral("callDevices/cameraToken"));
    localSettings.sync();
    callDeviceTestStatus_ = QStringLiteral("已恢复系统默认设备");
    rebuildCallDeviceProjection();
    showToast(callDeviceTestStatus_);
}

void QmlClientBackend::runCallDeviceDiagnostics()
{
    const bool microphoneAvailable = !QMediaDevices::audioInputs().isEmpty();
    const bool speakerAvailable = !QMediaDevices::audioOutputs().isEmpty();
    const bool cameraAvailable = !QMediaDevices::videoInputs().isEmpty();
    callDeviceTestStatus_ = microphoneAvailable && speakerAvailable && cameraAvailable
        ? QStringLiteral("音视频设备枚举正常")
        : QStringLiteral("部分音视频设备缺失或权限未授权");
    rebuildCallDeviceProjection();
    showToast(callDeviceTestStatus_);
}

void QmlClientBackend::rebuildCallDeviceProjection()
{
    const auto microphones = QMediaDevices::audioInputs();
    const auto speakers = QMediaDevices::audioOutputs();
    const auto cameras = QMediaDevices::videoInputs();
    QSettings localSettings;
    const auto microphoneToken = availableDeviceToken(microphones,
        localSettings.value(QStringLiteral("callDevices/microphoneToken")).toString());
    const auto speakerToken = availableDeviceToken(speakers,
        localSettings.value(QStringLiteral("callDevices/speakerToken")).toString());
    const auto cameraToken = availableDeviceToken(cameras,
        localSettings.value(QStringLiteral("callDevices/cameraToken")).toString());
    if (!microphoneToken.isEmpty())
        localSettings.setValue(QStringLiteral("callDevices/microphoneToken"), microphoneToken);
    if (!speakerToken.isEmpty())
        localSettings.setValue(QStringLiteral("callDevices/speakerToken"), speakerToken);
    if (!cameraToken.isEmpty())
        localSettings.setValue(QStringLiteral("callDevices/cameraToken"), cameraToken);

    const auto microphone = findDeviceByToken(microphones, microphoneToken);
    const auto speaker = findDeviceByToken(speakers, speakerToken);
    const auto camera = findDeviceByToken(cameras, cameraToken);
    callDeviceInfo_ = {
        {QStringLiteral("microphones"), audioDeviceProjection(microphones)},
        {QStringLiteral("speakers"), audioDeviceProjection(speakers)},
        {QStringLiteral("cameras"), cameraDeviceProjection(cameras)},
        {QStringLiteral("selectedMicrophoneToken"), microphoneToken},
        {QStringLiteral("selectedSpeakerToken"), speakerToken},
        {QStringLiteral("selectedCameraToken"), cameraToken},
        {QStringLiteral("selectedMicrophoneName"), microphone ? microphone->description() : QStringLiteral("未检测到麦克风")},
        {QStringLiteral("selectedSpeakerName"), speaker ? speaker->description() : QStringLiteral("未检测到扬声器")},
        {QStringLiteral("selectedCameraName"), camera ? camera->description() : QStringLiteral("未检测到摄像头")},
        {QStringLiteral("microphoneAvailable"), microphone.has_value()},
        {QStringLiteral("speakerAvailable"), speaker.has_value()},
        {QStringLiteral("cameraAvailable"), camera.has_value()},
        {QStringLiteral("cameraPreviewActive"), camera_ != nullptr && camera_->isActive()},
        {QStringLiteral("connectionStatus"), connected_ ? QStringLiteral("已连接") : QStringLiteral("离线")},
        {QStringLiteral("latencyText"), QStringLiteral("待检测")},
        {QStringLiteral("packetLossText"), QStringLiteral("待检测")},
        {QStringLiteral("jitterText"), QStringLiteral("待检测")},
        {QStringLiteral("lastCallStatus"), QStringLiteral("暂无可用通话诊断记录")},
        {QStringLiteral("testStatus"), callDeviceTestStatus_}};
    emit callDeviceInfoChanged();
}

void QmlClientBackend::applyAppearancePreset(const QString& preset)
{
    if (networkClient_ == nullptr || remoteSettings_.revision == 0) return;
    // 预设只修改外观字段并一次提交，防止多次请求共享旧 revision 时产生可避免的并发冲突。
    if (preset == QStringLiteral("polar-blue"))
    {
        remoteSettings_.theme = QStringLiteral("light");
        remoteSettings_.primaryColor = QStringLiteral("#1677FF");
        remoteSettings_.accentColor = QStringLiteral("#13C2C2");
    }
    else if (preset == QStringLiteral("obsidian"))
    {
        remoteSettings_.theme = QStringLiteral("dark");
        remoteSettings_.primaryColor = QStringLiteral("#4E8CFF");
        remoteSettings_.accentColor = QStringLiteral("#697586");
    }
    else if (preset == QStringLiteral("mint"))
    {
        remoteSettings_.theme = QStringLiteral("light");
        remoteSettings_.primaryColor = QStringLiteral("#20B486");
        remoteSettings_.accentColor = QStringLiteral("#7AC7B7");
    }
    else if (preset == QStringLiteral("sunset"))
    {
        remoteSettings_.theme = QStringLiteral("light");
        remoteSettings_.primaryColor = QStringLiteral("#F97316");
        remoteSettings_.accentColor = QStringLiteral("#FB7185");
    }
    else
    {
        showToast(QStringLiteral("未知的主题预设。"));
        return;
    }
    networkClient_->updateSettings(remoteSettings_);
}

void QmlClientBackend::resetAppearanceSettings()
{
    if (networkClient_ == nullptr || remoteSettings_.revision == 0) return;
    // 仅恢复迁移 016 定义的外观默认值，绝不触碰安全与通知偏好。
    remoteSettings_.theme = QStringLiteral("system");
    remoteSettings_.primaryColor = QStringLiteral("#1677FF");
    remoteSettings_.accentColor = QStringLiteral("#13C2C2");
    remoteSettings_.sidebarStyle = 0;
    remoteSettings_.cardRadiusMode = 1;
    remoteSettings_.uiDensity = 1;
    remoteSettings_.fontSizeMode = 1;
    remoteSettings_.chatBackground = QStringLiteral("default");
    remoteSettings_.messageBubbleStyle = 0;
    remoteSettings_.contentViewMode = 0;
    remoteSettings_.windowTransparency = 30;
    remoteSettings_.animationEnabled = true;
    remoteSettings_.animationIntensity = 1;
    networkClient_->updateSettings(remoteSettings_);
}

void QmlClientBackend::shareBusinessCard()
{
    if (accountProfile_.isEmpty())
    {
        showToast(QStringLiteral("账号资料尚未加载完成。"));
        return;
    }
    // 名片文本只包含界面已展示字段，避免把人员主键、设备标识或完整手机号写入剪贴板。
    const auto card = QStringLiteral("%1\n%2 · %3\n%4\n%5\n%6")
        .arg(accountProfile_.value(QStringLiteral("displayName")).toString(),
             accountProfile_.value(QStringLiteral("position")).toString(),
             accountProfile_.value(QStringLiteral("department")).toString(),
             accountProfile_.value(QStringLiteral("maskedPhone")).toString(),
             accountProfile_.value(QStringLiteral("email")).toString(),
             accountProfile_.value(QStringLiteral("office")).toString());
    if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr)
    {
        clipboard->setText(card.trimmed());
        showToast(QStringLiteral("个人名片已复制到剪贴板。"));
    }
}

void QmlClientBackend::requestAccountAction(const QString& action)
{
    if (action == QStringLiteral("avatar"))
        showToast(QStringLiteral("头像属于组织人员主数据，请通过组织管理员审核后更新。"));
    else if (action == QStringLiteral("password"))
        showToast(QStringLiteral("请在“安全与登录”中进入账号密码流程。"));
    else if (action == QStringLiteral("devices"))
        showToast(QStringLiteral("已显示当前账号的设备与最近登录摘要。"));
    else
        showToast(QStringLiteral("该账号操作需要由安全中心继续处理。"));
}

void QmlClientBackend::requestSecurityAction(const QString& action)
{
    if (action == QStringLiteral("password"))
    {
        // 当前协议没有密码变更消息，不能用本地修改或固定成功提示绕过服务端身份校验与审计。
        showToast(QStringLiteral("密码变更接口尚未部署，请联系组织管理员通过受控流程修改。"));
    }
    else if (action == QStringLiteral("devices"))
    {
        showToast(QStringLiteral("已登记 %1 台设备，其中 %2 台为受信任设备；设备明细接口尚未部署。")
            .arg(systemInfo_.value(QStringLiteral("deviceCount"), 0).toInt())
            .arg(systemInfo_.value(QStringLiteral("trustedDeviceCount"), 0).toInt()));
    }
    else if (action == QStringLiteral("trusted-devices"))
    {
        showToast(QStringLiteral("当前共有 %1 台受信任设备；撤销信任需要设备管理接口支持。")
            .arg(systemInfo_.value(QStringLiteral("trustedDeviceCount"), 0).toInt()));
    }
    else if (action == QStringLiteral("e2e"))
    {
        showToast(systemInfo_.value(QStringLiteral("e2eAvailable")).toBool()
            ? QStringLiteral("服务端已声明端到端加密能力可用。")
            : QStringLiteral("当前部署尚未提供端到端加密能力。"));
    }
    else
    {
        showToast(QStringLiteral("该安全操作尚未配置。"));
    }
}

void QmlClientBackend::resetAllSettings()
{
    if (networkClient_ == nullptr || !authenticated_ || remoteSettings_.revision == 0)
    {
        showToast(QStringLiteral("设置快照尚未加载，无法恢复默认设置。"));
        return;
    }
    // expected revision 由服务器在事务内比较，防止覆盖其他终端刚完成的安全设置修改。
    networkClient_->resetSettings(remoteSettings_.revision);
    showToast(QStringLiteral("正在恢复服务器默认设置…"));
}

void QmlClientBackend::exportSecurityLog()
{
    auto targetDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (targetDirectory.isEmpty())
        targetDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (targetDirectory.isEmpty() || !QDir().mkpath(targetDirectory))
    {
        showToast(QStringLiteral("无法访问用户下载目录，安全日志未导出。"));
        return;
    }

    // 诊断文本只包含聚合状态和偏好开关，不写入口令、账号名、设备 UUID、证书正文或本地路径。
    const auto report = QStringLiteral(
        "安域通安全诊断日志\n"
        "生成时间：%1\n"
        "客户端版本：%2\n"
        "服务端发布版本：%3\n"
        "安全连接：%4\n"
        "内网模式：%5\n"
        "传输加密：%6\n"
        "证书状态：%7\n"
        "国密状态：%8\n"
        "端到端加密能力：%9\n"
        "登记设备数：%10\n"
        "受信任设备数：%11\n"
        "双因素认证：%12\n"
        "聊天水印：%13\n"
        "防截屏保护：%14\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
             aboutSystem_.value(QStringLiteral("version")).toString(),
             aboutSystem_.value(QStringLiteral("serverVersion")).toString(),
             connected_ ? QStringLiteral("正常") : QStringLiteral("未连接"),
             systemInfo_.value(QStringLiteral("intranetMode")).toBool()
                 ? QStringLiteral("已启用") : QStringLiteral("未启用"),
             systemInfo_.value(QStringLiteral("transportEncryption"), QStringLiteral("—")).toString(),
             systemInfo_.value(QStringLiteral("certificateStatus"), QStringLiteral("—")).toString(),
             systemInfo_.value(QStringLiteral("cryptoStatus"), QStringLiteral("—")).toString(),
             systemInfo_.value(QStringLiteral("e2eAvailable")).toBool()
                 ? QStringLiteral("可用") : QStringLiteral("未部署"),
             QString::number(systemInfo_.value(QStringLiteral("deviceCount"), 0).toInt()),
             QString::number(systemInfo_.value(QStringLiteral("trustedDeviceCount"), 0).toInt()),
             remoteSettings_.twoFactorEnabled ? QStringLiteral("已开启") : QStringLiteral("已关闭"),
             remoteSettings_.chatWatermarkEnabled ? QStringLiteral("已开启") : QStringLiteral("已关闭"),
             remoteSettings_.screenshotProtectionEnabled ? QStringLiteral("已开启") : QStringLiteral("已关闭"));

    const auto fileName = QStringLiteral("安域通安全诊断-%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QSaveFile output(QDir(targetDirectory).filePath(fileName));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        showToast(QStringLiteral("无法创建安全诊断文件，请检查目录权限。"));
        return;
    }
    output.write(report.toUtf8());
    if (!output.commit())
    {
        showToast(QStringLiteral("安全诊断写入失败，未留下不完整文件。"));
        return;
    }
    showToast(QStringLiteral("安全诊断已导出到下载目录：%1").arg(fileName));
}

void QmlClientBackend::refreshAboutSystem()
{
    rebuildAboutSystemProjection();
    if (networkClient_ != nullptr && authenticated_)
    {
        // 授权组织、服务端发布版本和安全状态属于服务端权威数据，刷新时复用设置快照接口。
        networkClient_->requestSettings();
        showToast(QStringLiteral("正在刷新系统与版本信息…"));
    }
    else
    {
        showToast(QStringLiteral("已刷新本机信息；登录后可同步组织与服务端版本状态。"));
    }
}

void QmlClientBackend::copySystemInformation()
{
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
    {
        showToast(QStringLiteral("当前平台未提供系统剪贴板。"));
        return;
    }
    clipboard->setText(sanitizedSystemInformation());
    showToast(QStringLiteral("脱敏系统信息已复制到剪贴板。"));
}

void QmlClientBackend::exportSystemInformation()
{
    auto targetDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (targetDirectory.isEmpty())
        targetDirectory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (targetDirectory.isEmpty() || !QDir().mkpath(targetDirectory))
    {
        showToast(QStringLiteral("无法访问用户下载目录，系统信息未导出。"));
        return;
    }

    const auto fileName = QStringLiteral("安域通系统信息-%1.txt")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const auto targetPath = QDir(targetDirectory).filePath(fileName);
    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        showToast(QStringLiteral("无法创建系统信息文件，请检查目录权限。"));
        return;
    }
    output.write(sanitizedSystemInformation().toUtf8());
    if (!output.commit())
    {
        showToast(QStringLiteral("系统信息文件写入失败，未留下不完整文件。"));
        return;
    }
    showToast(QStringLiteral("系统信息已导出到下载目录：%1").arg(fileName));
}

void QmlClientBackend::checkForUpdates()
{
    const auto serverVersion = systemInfo_.value(QStringLiteral("version")).toString().trimmed();
    const auto clientVersion = aboutSystem_.value(QStringLiteral("version")).toString().trimmed();
    if (serverVersion.isEmpty())
    {
        showToast(QStringLiteral("尚未取得服务端发布版本，请登录后重试。"));
        return;
    }
    const auto published = QVersionNumber::fromString(serverVersion);
    const auto installed = QVersionNumber::fromString(clientVersion);
    if (!published.isNull() && !installed.isNull()
        && QVersionNumber::compare(published, installed) > 0)
    {
        showToast(QStringLiteral("服务端已发布 %1，请联系管理员获取已签名安装包。")
            .arg(serverVersion));
        return;
    }
    showToast(QStringLiteral("当前客户端已是服务端登记的最新版本。"));
}

void QmlClientBackend::requestAboutAction(const QString& action)
{
    QString configuredAddress;
    if (action == QStringLiteral("website"))
        configuredAddress = deploymentValue("ORGLINK_SUPPORT_WEBSITE");
    else if (action == QStringLiteral("email"))
    {
        const auto value = deploymentValue("ORGLINK_SUPPORT_EMAIL");
        if (!value.isEmpty()) configuredAddress = QStringLiteral("mailto:") + value;
    }
    else if (action == QStringLiteral("phone"))
    {
        const auto value = deploymentValue("ORGLINK_SUPPORT_PHONE");
        if (!value.isEmpty()) configuredAddress = QStringLiteral("tel:") + value;
    }
    else if (action == QStringLiteral("ios"))
        configuredAddress = deploymentValue("ORGLINK_IOS_DOWNLOAD_URL");
    else if (action == QStringLiteral("android"))
        configuredAddress = deploymentValue("ORGLINK_ANDROID_DOWNLOAD_URL");
    else if (action == QStringLiteral("open-source"))
    {
        const auto licenseDirectory = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("licenses"));
        if (QFileInfo(licenseDirectory).isDir()
            && QDesktopServices::openUrl(QUrl::fromLocalFile(licenseDirectory)))
            showToast(QStringLiteral("已打开随程序发布的第三方许可证目录。"));
        else
            showToast(QStringLiteral("当前运行目录未部署第三方许可证清单。"));
        return;
    }
    else
    {
        const QHash<QString, QString> descriptions{
            {QStringLiteral("license"), QStringLiteral("许可证管理需要接入部署方授权服务。")},
            {QStringLiteral("agreement"), QStringLiteral("服务协议尚未由部署管理员配置。")},
            {QStringLiteral("privacy"), QStringLiteral("隐私政策尚未由部署管理员配置。")},
            {QStringLiteral("change-log"), QStringLiteral("更新日志服务尚未配置。")},
            {QStringLiteral("help"), QStringLiteral("帮助中心地址尚未配置。")},
            {QStringLiteral("feedback"), QStringLiteral("意见反馈地址尚未配置。")},
            {QStringLiteral("support"), QStringLiteral("技术支持渠道尚未配置。")}};
        showToast(descriptions.value(action, QStringLiteral("该系统操作尚未配置。")));
        return;
    }

    const QUrl url(configuredAddress);
    if (configuredAddress.isEmpty() || !isAllowedExternalUrl(url))
    {
        showToast(QStringLiteral("该地址未配置或不符合安全协议要求。"));
        return;
    }
    if (!QDesktopServices::openUrl(url))
        showToast(QStringLiteral("系统无法打开该地址，请检查默认应用设置。"));
}

void QmlClientBackend::closePreview()
{
    if (!previewVisible_) return;
    previewVisible_ = false;
    previewUrl_.clear();
    previewName_.clear();
    previewMediaType_.clear();
    previewKind_ = 0;
    emit previewChanged();
}

void QmlClientBackend::clearToast()
{
    if (toastText_.isEmpty()) return;
    toastText_.clear();
    emit toastTextChanged();
}

void QmlClientBackend::showToast(const QString& message)
{
    toastText_ = message.trimmed();
    emit toastTextChanged();
}

void QmlClientBackend::initializeAuthenticatedSession(
    qulonglong personId, const QString& displayName)
{
    currentPersonId_ = personId;
    currentAccountName_ = pendingLoginName_.trimmed().isEmpty()
        ? QStringLiteral("当前账号") : pendingLoginName_.trimmed();
    pendingLoginName_.clear();
    currentUser_ = displayName.trimmed().isEmpty() ? QStringLiteral("当前用户") : displayName.trimmed();
    authenticated_ = true;
    fileTransferController_.initializeForUser(personId);
    emit currentAccountNameChanged();
    emit currentDisplayNameChanged();
    emit currentUserChanged();
    emit authenticatedChanged();
    accountProfile_ = {{QStringLiteral("personId"), identifier(personId)},
        {QStringLiteral("displayName"), currentUser_},
        {QStringLiteral("loginName"), currentAccountName_}};
    emit accountProfileChanged();
    if (networkClient_ == nullptr) return;
    networkClient_->requestConversationList();
    networkClient_->requestDirectorySync(0);
    networkClient_->requestContactCenter();
    networkClient_->requestContactDetail(personId);
    networkClient_->requestGroupList();
    networkClient_->requestNotificationList(0, false, {}, 0, 20);
    networkClient_->requestFileCenter();
    networkClient_->requestSettings();
    requestCurrentCalendarRange();
}

void QmlClientBackend::replaceMessages(const QList<RemoteMessageItem>& remoteMessages)
{
    messages_.clear();
    sharedFiles_.clear();
    QSet<QString> seenMessages;
    QSet<QString> seenAssets;
    for (const auto& item : remoteMessages)
    {
        const auto messageKey = !item.serverMessageId.isEmpty() ? item.serverMessageId : item.clientMessageId;
        if (messageKey.isEmpty() || seenMessages.contains(messageKey)) continue;
        seenMessages.insert(messageKey);
        QVariantMap mapped{{QStringLiteral("serverId"), item.serverMessageId},
            {QStringLiteral("clientId"), item.clientMessageId}, {QStringLiteral("sequence"), identifier(item.sequence)},
            {QStringLiteral("senderId"), identifier(item.senderPersonId)},
            {QStringLiteral("outgoing"), item.senderPersonId == currentPersonId_},
            {QStringLiteral("kind"), item.kind},
            {QStringLiteral("time"), QDateTime::fromMSecsSinceEpoch(
                static_cast<qint64>(item.createdAtUtcMs)).toLocalTime().toString(QStringLiteral("HH:mm"))}};
        if (item.kind == 3)
        {
            const auto object = QJsonDocument::fromJson(item.content.toUtf8()).object();
            const auto assetUuid = object.value(QStringLiteral("asset_uuid")).toString();
            const auto fileName = object.value(QStringLiteral("file_name")).toString(QStringLiteral("共享文件"));
            mapped.insert(QStringLiteral("assetUuid"), assetUuid);
            mapped.insert(QStringLiteral("fileName"), fileName);
            mapped.insert(QStringLiteral("mediaType"), object.value(QStringLiteral("media_type")).toString());
            mapped.insert(QStringLiteral("sizeBytes"), object.value(QStringLiteral("size_bytes")).toVariant());
            if (!assetUuid.isEmpty() && !seenAssets.contains(assetUuid))
            {
                seenAssets.insert(assetUuid);
                sharedFiles_.push_back(QVariantMap{{QStringLiteral("assetUuid"), assetUuid},
                    {QStringLiteral("name"), fileName},
                    {QStringLiteral("mediaType"), mapped.value(QStringLiteral("mediaType"))},
                    {QStringLiteral("sizeBytes"), mapped.value(QStringLiteral("sizeBytes"))}});
            }
        }
        else
        {
            mapped.insert(QStringLiteral("text"), item.content);
        }
        messages_.push_back(mapped);
    }
    emit messagesChanged();
    emit sharedFilesChanged();
}

void QmlClientBackend::applySettings(
    const RemoteUserSettings& settings, const RemoteSettingsSystemInfo* info)
{
    remoteSettings_ = settings;
    SettingsProfileItem confirmed{settings.revision, settings.twoFactorEnabled,
        settings.startupEnabled, settings.autoLoginEnabled, settings.autoLockMinutes,
        settings.chatWatermarkEnabled, settings.screenshotProtectionEnabled,
        settings.downloadPath, settings.language, settings.theme,
        settings.phoneVisibility, settings.emailVisibility, settings.searchVisibility,
        settings.phoneSearchEnabled, settings.profileSignature};
    confirmed.newMessageNotificationEnabled = settings.newMessageNotificationEnabled;
    confirmed.notificationSoundEnabled = settings.notificationSoundEnabled;
    confirmed.notificationSoundName = settings.notificationSoundName;
    confirmed.desktopPopupEnabled = settings.desktopPopupEnabled;
    confirmed.unreadBadgeEnabled = settings.unreadBadgeEnabled;
    confirmed.mentionNotificationEnabled = settings.mentionNotificationEnabled;
    confirmed.groupNotificationLevel = settings.groupNotificationLevel;
    confirmed.systemNotificationEnabled = settings.systemNotificationEnabled;
    confirmed.approvalNotificationEnabled = settings.approvalNotificationEnabled;
    confirmed.fileNotificationEnabled = settings.fileNotificationEnabled;
    confirmed.calendarNotificationEnabled = settings.calendarNotificationEnabled;
    confirmed.calendarReminderMinutes = settings.calendarReminderMinutes;
    confirmed.doNotDisturbEnabled = settings.doNotDisturbEnabled;
    confirmed.doNotDisturbStartMinutes = settings.doNotDisturbStartMinutes;
    confirmed.doNotDisturbEndMinutes = settings.doNotDisturbEndMinutes;
    confirmed.notificationPreviewMode = settings.notificationPreviewMode;
    confirmed.readReceiptEnabled = settings.readReceiptEnabled;
    confirmed.enterToSendEnabled = settings.enterToSendEnabled;
    confirmed.messageBubbleDensity = settings.messageBubbleDensity;
    confirmed.primaryColor = settings.primaryColor;
    confirmed.accentColor = settings.accentColor;
    confirmed.sidebarStyle = settings.sidebarStyle;
    confirmed.cardRadiusMode = settings.cardRadiusMode;
    confirmed.uiDensity = settings.uiDensity;
    confirmed.fontSizeMode = settings.fontSizeMode;
    confirmed.chatBackground = settings.chatBackground;
    confirmed.messageBubbleStyle = settings.messageBubbleStyle;
    confirmed.contentViewMode = settings.contentViewMode;
    confirmed.windowTransparency = settings.windowTransparency;
    confirmed.animationEnabled = settings.animationEnabled;
    confirmed.animationIntensity = settings.animationIntensity;
    confirmed.autoSaveReceivedFiles = settings.autoSaveReceivedFiles;
    confirmed.recentFileRetentionDays = settings.recentFileRetentionDays;
    confirmed.autoCacheCleanupEnabled = settings.autoCacheCleanupEnabled;
    confirmed.cacheSizeLimitMb = settings.cacheSizeLimitMb;
    confirmed.filePreviewMode = settings.filePreviewMode;
    confirmed.imageAutoCompressEnabled = settings.imageAutoCompressEnabled;
    confirmed.videoTranscodeMode = settings.videoTranscodeMode;
    confirmed.fileEncryptionMode = settings.fileEncryptionMode;
    confirmed.externalWatermarkMode = settings.externalWatermarkMode;
    confirmed.defaultSharePermission = settings.defaultSharePermission;
    confirmed.syncFolderPath = settings.syncFolderPath;
    confirmed.echoCancellationEnabled = settings.echoCancellationEnabled;
    confirmed.noiseSuppressionEnabled = settings.noiseSuppressionEnabled;
    confirmed.autoGainControlEnabled = settings.autoGainControlEnabled;
    confirmed.cameraMirrorEnabled = settings.cameraMirrorEnabled;
    confirmed.videoResolutionMode = settings.videoResolutionMode;
    confirmed.bandwidthOptimizationEnabled = settings.bandwidthOptimizationEnabled;
    confirmed.recordingPermissionEnabled = settings.recordingPermissionEnabled;
    confirmed.incomingCallWindowPosition = settings.incomingCallWindowPosition;
    confirmed.bluetoothPreferred = settings.bluetoothPreferred;
    confirmed.callShortcut = settings.callShortcut;
    settingsModel_.replaceProfile(confirmed);
    settingsProfile_ = {{QStringLiteral("revision"), identifier(settings.revision)},
        {QStringLiteral("twoFactorEnabled"), settings.twoFactorEnabled},
        {QStringLiteral("startupEnabled"), settings.startupEnabled},
        {QStringLiteral("autoLoginEnabled"), settings.autoLoginEnabled},
        {QStringLiteral("autoLockMinutes"), settings.autoLockMinutes},
        {QStringLiteral("chatWatermarkEnabled"), settings.chatWatermarkEnabled},
        {QStringLiteral("screenshotProtectionEnabled"), settings.screenshotProtectionEnabled},
        {QStringLiteral("downloadPath"), settings.downloadPath},
        {QStringLiteral("language"), settings.language}, {QStringLiteral("theme"), settings.theme},
        {QStringLiteral("phoneVisibility"), settings.phoneVisibility},
        {QStringLiteral("emailVisibility"), settings.emailVisibility},
        {QStringLiteral("searchVisibility"), settings.searchVisibility},
        {QStringLiteral("phoneSearchEnabled"), settings.phoneSearchEnabled},
        {QStringLiteral("profileSignature"), settings.profileSignature},
        {QStringLiteral("newMessageNotificationEnabled"), settings.newMessageNotificationEnabled},
        {QStringLiteral("notificationSoundEnabled"), settings.notificationSoundEnabled},
        {QStringLiteral("notificationSoundName"), settings.notificationSoundName},
        {QStringLiteral("desktopPopupEnabled"), settings.desktopPopupEnabled},
        {QStringLiteral("unreadBadgeEnabled"), settings.unreadBadgeEnabled},
        {QStringLiteral("mentionNotificationEnabled"), settings.mentionNotificationEnabled},
        {QStringLiteral("groupNotificationLevel"), settings.groupNotificationLevel},
        {QStringLiteral("systemNotificationEnabled"), settings.systemNotificationEnabled},
        {QStringLiteral("approvalNotificationEnabled"), settings.approvalNotificationEnabled},
        {QStringLiteral("fileNotificationEnabled"), settings.fileNotificationEnabled},
        {QStringLiteral("calendarNotificationEnabled"), settings.calendarNotificationEnabled},
        {QStringLiteral("calendarReminderMinutes"), settings.calendarReminderMinutes},
        {QStringLiteral("doNotDisturbEnabled"), settings.doNotDisturbEnabled},
        {QStringLiteral("doNotDisturbStartMinutes"), settings.doNotDisturbStartMinutes},
        {QStringLiteral("doNotDisturbEndMinutes"), settings.doNotDisturbEndMinutes},
        {QStringLiteral("notificationPreviewMode"), settings.notificationPreviewMode},
        {QStringLiteral("readReceiptEnabled"), settings.readReceiptEnabled},
        {QStringLiteral("enterToSendEnabled"), settings.enterToSendEnabled},
        {QStringLiteral("messageBubbleDensity"), settings.messageBubbleDensity},
        {QStringLiteral("primaryColor"), settings.primaryColor},
        {QStringLiteral("accentColor"), settings.accentColor},
        {QStringLiteral("sidebarStyle"), settings.sidebarStyle},
        {QStringLiteral("cardRadiusMode"), settings.cardRadiusMode},
        {QStringLiteral("uiDensity"), settings.uiDensity},
        {QStringLiteral("fontSizeMode"), settings.fontSizeMode},
        {QStringLiteral("chatBackground"), settings.chatBackground},
        {QStringLiteral("messageBubbleStyle"), settings.messageBubbleStyle},
        {QStringLiteral("contentViewMode"), settings.contentViewMode},
        {QStringLiteral("windowTransparency"), settings.windowTransparency},
        {QStringLiteral("animationEnabled"), settings.animationEnabled},
        {QStringLiteral("animationIntensity"), settings.animationIntensity},
        {QStringLiteral("autoSaveReceivedFiles"), settings.autoSaveReceivedFiles},
        {QStringLiteral("recentFileRetentionDays"), settings.recentFileRetentionDays},
        {QStringLiteral("autoCacheCleanupEnabled"), settings.autoCacheCleanupEnabled},
        {QStringLiteral("cacheSizeLimitMb"), settings.cacheSizeLimitMb},
        {QStringLiteral("filePreviewMode"), settings.filePreviewMode},
        {QStringLiteral("imageAutoCompressEnabled"), settings.imageAutoCompressEnabled},
        {QStringLiteral("videoTranscodeMode"), settings.videoTranscodeMode},
        {QStringLiteral("fileEncryptionMode"), settings.fileEncryptionMode},
        {QStringLiteral("externalWatermarkMode"), settings.externalWatermarkMode},
        {QStringLiteral("defaultSharePermission"), settings.defaultSharePermission},
        {QStringLiteral("syncFolderPath"), settings.syncFolderPath},
        {QStringLiteral("echoCancellationEnabled"), settings.echoCancellationEnabled},
        {QStringLiteral("noiseSuppressionEnabled"), settings.noiseSuppressionEnabled},
        {QStringLiteral("autoGainControlEnabled"), settings.autoGainControlEnabled},
        {QStringLiteral("cameraMirrorEnabled"), settings.cameraMirrorEnabled},
        {QStringLiteral("videoResolutionMode"), settings.videoResolutionMode},
        {QStringLiteral("bandwidthOptimizationEnabled"), settings.bandwidthOptimizationEnabled},
        {QStringLiteral("recordingPermissionEnabled"), settings.recordingPermissionEnabled},
        {QStringLiteral("incomingCallWindowPosition"), settings.incomingCallWindowPosition},
        {QStringLiteral("bluetoothPreferred"), settings.bluetoothPreferred},
        {QStringLiteral("callShortcut"), settings.callShortcut}};
    accountProfile_.insert(QStringLiteral("signature"), settings.profileSignature);
    emit settingsProfileChanged();
    emit accountProfileChanged();
    if (info != nullptr)
    {
        settingsModel_.replaceSystemInfo({info->deviceCount, info->trustedDeviceCount,
            info->storageUsedBytes, info->storageQuotaBytes, info->intranetMode,
            info->endToEndEncryptionAvailable, info->certificateStatus,
            info->transportEncryption, info->cryptoStatus, info->productName,
            info->currentVersion, info->updateDate, info->organizationName,
            info->loginName, info->accountStatusText, info->lastLoginAtUtcMs,
            info->lastLoginDeviceName, info->lastLoginPlatform,
            info->lastLoginSource, info->teamMemberCount, info->storageDocumentBytes,
            info->storageImageBytes, info->storageVideoBytes, info->storageOtherBytes,
            info->syncedFileCount, info->lastFileSyncAtUtcMs});
        systemInfo_ = {{QStringLiteral("deviceCount"), info->deviceCount},
            {QStringLiteral("trustedDeviceCount"), info->trustedDeviceCount},
            {QStringLiteral("storageUsedBytes"), identifier(info->storageUsedBytes)},
            {QStringLiteral("storageQuotaBytes"), identifier(info->storageQuotaBytes)},
            {QStringLiteral("intranetMode"), info->intranetMode},
            {QStringLiteral("e2eAvailable"), info->endToEndEncryptionAvailable},
            {QStringLiteral("certificateStatus"), info->certificateStatus},
            {QStringLiteral("transportEncryption"), info->transportEncryption},
            {QStringLiteral("cryptoStatus"), info->cryptoStatus},
            {QStringLiteral("productName"), info->productName},
            {QStringLiteral("version"), info->currentVersion},
            {QStringLiteral("updateDate"), info->updateDate},
            {QStringLiteral("organizationName"), info->organizationName},
            {QStringLiteral("loginName"), info->loginName},
            {QStringLiteral("accountStatus"), info->accountStatusText},
            {QStringLiteral("lastLoginAt"), info->lastLoginAtUtcMs == 0 ? QString() :
                QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(info->lastLoginAtUtcMs))
                    .toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
            {QStringLiteral("lastLoginDevice"), info->lastLoginDeviceName},
            {QStringLiteral("lastLoginPlatform"), info->lastLoginPlatform},
            {QStringLiteral("lastLoginSource"), info->lastLoginSource},
            {QStringLiteral("teamMemberCount"), info->teamMemberCount},
            {QStringLiteral("storageDocumentBytes"), identifier(info->storageDocumentBytes)},
            {QStringLiteral("storageImageBytes"), identifier(info->storageImageBytes)},
            {QStringLiteral("storageVideoBytes"), identifier(info->storageVideoBytes)},
            {QStringLiteral("storageOtherBytes"), identifier(info->storageOtherBytes)},
            {QStringLiteral("syncedFileCount"), identifier(info->syncedFileCount)},
            {QStringLiteral("lastFileSyncAt"), info->lastFileSyncAtUtcMs == 0 ? QString() :
                QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(info->lastFileSyncAtUtcMs))
                    .toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))}};
        const auto authoritativeAccountName = info->loginName.trimmed();
        if (!authoritativeAccountName.isEmpty() && currentAccountName_ != authoritativeAccountName)
        {
            // 设置响应携带服务端规范化账号名，只更新账号属性，绝不覆盖人员姓名。
            currentAccountName_ = authoritativeAccountName;
            emit currentAccountNameChanged();
        }
        mergeAccountSystemInfo();
        rebuildAboutSystemProjection();
        emit systemInfoChanged();
        emit accountProfileChanged();
    }
    rebuildFileStorageProjection();
    rebuildCallDeviceProjection();
}

void QmlClientBackend::rebuildFileStorageProjection(bool scanLocalCache)
{
    const auto cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const auto cacheUsedBytes = scanLocalCache ? directorySizeWithoutLinks(cachePath)
        : fileStorageInfo_.value(QStringLiteral("cacheUsedBytes"), identifier(0)).toULongLong();
    const auto cacheLimitBytes = static_cast<qulonglong>(
        std::max(256, remoteSettings_.cacheSizeLimitMb)) * 1024ULL * 1024ULL;
    const auto backupDirectory = fileStorageBackupDirectory();
    const auto backupPath = backupDirectory.isEmpty() ? QString()
        : QDir(backupDirectory).filePath(QStringLiteral("file-storage-settings.json"));
    const QFileInfo backup(backupPath);
    fileStorageInfo_ = {
        {QStringLiteral("storageUsedBytes"), systemInfo_.value(QStringLiteral("storageUsedBytes"))},
        {QStringLiteral("storageQuotaBytes"), systemInfo_.value(QStringLiteral("storageQuotaBytes"))},
        {QStringLiteral("documentBytes"), systemInfo_.value(QStringLiteral("storageDocumentBytes"))},
        {QStringLiteral("imageBytes"), systemInfo_.value(QStringLiteral("storageImageBytes"))},
        {QStringLiteral("videoBytes"), systemInfo_.value(QStringLiteral("storageVideoBytes"))},
        {QStringLiteral("otherBytes"), systemInfo_.value(QStringLiteral("storageOtherBytes"))},
        {QStringLiteral("cacheUsedBytes"), identifier(cacheUsedBytes)},
        {QStringLiteral("cacheLimitBytes"), identifier(cacheLimitBytes)},
        {QStringLiteral("cachePath"), cachePath},
        {QStringLiteral("syncStatus"), connected_ ? QStringLiteral("正常") : QStringLiteral("离线")},
        {QStringLiteral("syncedFileCount"), systemInfo_.value(QStringLiteral("syncedFileCount"))},
        {QStringLiteral("lastSyncAt"), systemInfo_.value(QStringLiteral("lastFileSyncAt"))},
        {QStringLiteral("backupAt"), backup.exists()
            ? backup.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString()},
        {QStringLiteral("backupSizeBytes"), identifier(backup.exists()
            ? static_cast<qulonglong>(std::max<qint64>(0, backup.size())) : 0ULL)}};
    emit fileStorageInfoChanged();
}

void QmlClientBackend::rebuildAboutSystemProjection()
{
    const auto configuredVersion = QStringLiteral(ORGLINK_PROJECT_VERSION);
    const auto applicationVersion = QCoreApplication::applicationVersion().trimmed();
    const auto clientVersion = applicationVersion.isEmpty() ? configuredVersion : applicationVersion;
    const auto executableTimestamp = QFileInfo(QCoreApplication::applicationFilePath()).lastModified();
    const auto serverVersion = systemInfo_.value(QStringLiteral("version")).toString().trimmed();
    const auto published = QVersionNumber::fromString(serverVersion);
    const auto installed = QVersionNumber::fromString(clientVersion);
    const bool updateAvailable = !published.isNull() && !installed.isNull()
        && QVersionNumber::compare(published, installed) > 0;

    bool seatLimitValid = false;
    const auto seatLimit = deploymentValue("ORGLINK_LICENSE_SEAT_LIMIT").toInt(&seatLimitValid);
    const auto licenseType = deploymentValue("ORGLINK_LICENSE_TYPE");
    const auto website = deploymentValue("ORGLINK_SUPPORT_WEBSITE");
    const auto email = deploymentValue("ORGLINK_SUPPORT_EMAIL");
    const auto phone = deploymentValue("ORGLINK_SUPPORT_PHONE");
    const auto iosDownload = deploymentValue("ORGLINK_IOS_DOWNLOAD_URL");
    const auto androidDownload = deploymentValue("ORGLINK_ANDROID_DOWNLOAD_URL");

    QVariantMap rebuilt{
        {QStringLiteral("productName"), QStringLiteral("安域通")},
        {QStringLiteral("englishName"), QStringLiteral("OrgLink Secure IM")},
        {QStringLiteral("edition"), QStringLiteral("企业版")},
        {QStringLiteral("slogan"), QStringLiteral("安全 · 高效 · 连接每一位团队成员")},
        {QStringLiteral("version"), clientVersion},
        {QStringLiteral("serverVersion"), serverVersion.isEmpty() ? QStringLiteral("—") : serverVersion},
        {QStringLiteral("buildNumber"), deploymentValue("ORGLINK_BUILD_NUMBER").isEmpty()
            ? QStringLiteral("%1-qt%2").arg(clientVersion, QString::fromLatin1(qVersion()))
            : deploymentValue("ORGLINK_BUILD_NUMBER")},
        {QStringLiteral("updateDate"), executableTimestamp.isValid()
            ? executableTimestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("—")},
        {QStringLiteral("systemEnvironment"), QStringLiteral("%1 · %2")
            .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture())},
        {QStringLiteral("organization"), systemInfo_.value(QStringLiteral("organizationName")).toString().isEmpty()
            ? QStringLiteral("未加载组织信息") : systemInfo_.value(QStringLiteral("organizationName"))},
        {QStringLiteral("licenseType"), licenseType.isEmpty()
            ? QStringLiteral("未配置授权服务") : licenseType},
        {QStringLiteral("licensedSeats"), systemInfo_.value(QStringLiteral("teamMemberCount"), 0)},
        {QStringLiteral("seatLimit"), seatLimitValid && seatLimit > 0 ? seatLimit : 0},
        {QStringLiteral("licenseExpiresAt"), deploymentValue("ORGLINK_LICENSE_EXPIRES_AT").isEmpty()
            ? QStringLiteral("—") : deploymentValue("ORGLINK_LICENSE_EXPIRES_AT")},
        {QStringLiteral("licenseConfigured"), !licenseType.isEmpty() && seatLimitValid && seatLimit > 0},
        {QStringLiteral("updateAvailable"), updateAvailable},
        {QStringLiteral("versionStatus"), serverVersion.isEmpty() ? QStringLiteral("等待服务端版本信息")
            : (updateAvailable ? QStringLiteral("发现服务端发布的新版本") : QStringLiteral("已是登记的最新版本"))},
        {QStringLiteral("website"), website.isEmpty() ? QStringLiteral("未配置") : website},
        {QStringLiteral("supportEmail"), email.isEmpty() ? QStringLiteral("未配置") : email},
        {QStringLiteral("supportPhone"), phone.isEmpty() ? QStringLiteral("未配置") : phone},
        {QStringLiteral("iosDownloadConfigured"), !iosDownload.isEmpty()},
        {QStringLiteral("androidDownloadConfigured"), !androidDownload.isEmpty()},
        {QStringLiteral("transportEncryption"), systemInfo_.value(QStringLiteral("transportEncryption"), QStringLiteral("—"))},
        {QStringLiteral("certificateStatus"), systemInfo_.value(QStringLiteral("certificateStatus"), QStringLiteral("—"))},
        {QStringLiteral("copyright"), QStringLiteral("© %1 OrgLink. 保留所有权利")
            .arg(QDate::currentDate().year())}};

    if (aboutSystem_ == rebuilt) return;
    aboutSystem_ = std::move(rebuilt);
    emit aboutSystemChanged();
}

QString QmlClientBackend::sanitizedSystemInformation() const
{
    return QStringLiteral(
        "安域通 / OrgLink Secure IM\n"
        "客户端版本：%1\n"
        "服务端发布版本：%2\n"
        "构建号：%3\n"
        "构建更新时间：%4\n"
        "系统环境：%5\n"
        "所属组织：%6\n"
        "传输加密：%7\n"
        "证书状态：%8\n")
        .arg(aboutSystem_.value(QStringLiteral("version")).toString(),
             aboutSystem_.value(QStringLiteral("serverVersion")).toString(),
             aboutSystem_.value(QStringLiteral("buildNumber")).toString(),
             aboutSystem_.value(QStringLiteral("updateDate")).toString(),
             aboutSystem_.value(QStringLiteral("systemEnvironment")).toString(),
             aboutSystem_.value(QStringLiteral("organization")).toString(),
             aboutSystem_.value(QStringLiteral("transportEncryption")).toString(),
             aboutSystem_.value(QStringLiteral("certificateStatus")).toString());
}

void QmlClientBackend::mergeAccountSystemInfo()
{
    if (accountProfile_.isEmpty()) return;
    accountProfile_.insert(QStringLiteral("organization"),
        systemInfo_.value(QStringLiteral("organizationName")));
    accountProfile_.insert(QStringLiteral("loginName"),
        systemInfo_.value(QStringLiteral("loginName")));
    accountProfile_.insert(QStringLiteral("accountStatus"),
        systemInfo_.value(QStringLiteral("accountStatus")));
    accountProfile_.insert(QStringLiteral("lastLoginAt"),
        systemInfo_.value(QStringLiteral("lastLoginAt")));
    accountProfile_.insert(QStringLiteral("lastLoginDevice"),
        systemInfo_.value(QStringLiteral("lastLoginDevice")));
    accountProfile_.insert(QStringLiteral("lastLoginPlatform"),
        systemInfo_.value(QStringLiteral("lastLoginPlatform")));
    accountProfile_.insert(QStringLiteral("lastLoginSource"),
        systemInfo_.value(QStringLiteral("lastLoginSource")));
    accountProfile_.insert(QStringLiteral("teamMemberCount"),
        systemInfo_.value(QStringLiteral("teamMemberCount")));
    accountProfile_.insert(QStringLiteral("signature"),
        settingsProfile_.value(QStringLiteral("profileSignature")));
}

void QmlClientBackend::requestCurrentCalendarRange()
{
    if (networkClient_ == nullptr) return;
    const auto now = QDateTime::currentDateTimeUtc();
    networkClient_->requestCalendarEvents(
        static_cast<qulonglong>(now.addDays(-31).toMSecsSinceEpoch()),
        static_cast<qulonglong>(now.addDays(62).toMSecsSinceEpoch()));
}

} // namespace orglink::client
