#include "controller/FileTransferController.h"

#include "model/SettingsModel.h"
#include "network/NetworkClient.h"
#include "view/file/FilePreviewDialog.h"
#include "view/main/MainWindow.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace orglink::client
{

FileTransferController::FileTransferController(
    NetworkClient* networkClient, SettingsModel* settingsModel,
    MainWindow* mainWindow, QObject* parent)
    : QObject(parent), networkClient_(networkClient), settingsModel_(settingsModel),
      mainWindow_(mainWindow)
{
    Q_ASSERT(settingsModel_ != nullptr);
    Q_ASSERT(mainWindow_ != nullptr);
    if (networkClient_ != nullptr)
    {
        connect(networkClient_, &NetworkClient::fileDownloaded,
                this, &FileTransferController::handleFileDownloaded);
    }
}

void FileTransferController::initializeForUser(qulonglong personId)
{
    // 登录身份变化时丢弃未完成意图，防止前一账号的迟到响应被当前账号自动打开。
    currentPersonId_ = personId;
    pendingActions_.clear();
}

QString FileTransferController::sanitizedFileName(const QString& fileName)
{
    auto normalized = fileName;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QFileInfo(normalized).fileName().trimmed();
    normalized.remove(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")));
    while (normalized.endsWith(QLatin1Char('.')) || normalized.endsWith(QLatin1Char(' ')))
        normalized.chop(1);
    if (normalized.isEmpty()) normalized = QStringLiteral("共享文件");

    const auto baseName = QFileInfo(normalized).completeBaseName().toUpper();
    static const QSet<QString> reserved{
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
        QStringLiteral("NUL"), QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9")};
    if (reserved.contains(baseName)) normalized.prepend(QLatin1Char('_'));

    // 限制本地文件名长度，给下载目录和自动生成的冲突序号预留 Windows 路径空间。
    if (normalized.size() > 180)
    {
        const QFileInfo info(normalized);
        const auto suffix = info.suffix();
        const auto suffixLength = suffix.isEmpty() ? 0 : suffix.size() + 1;
        normalized = info.completeBaseName().left(180 - suffixLength)
            + (suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
    }
    return normalized;
}

void FileTransferController::requestOpen(const QString& assetUuid)
{
    request(assetUuid, PendingAction::Preview);
}

void FileTransferController::requestDownload(const QString& assetUuid)
{
    request(assetUuid, PendingAction::SaveOnly);
}

void FileTransferController::request(const QString& assetUuid, PendingAction action)
{
    const auto normalizedAsset = assetUuid.trimmed();
    if (normalizedAsset.isEmpty())
    {
        emit notificationRequested(QStringLiteral("文件标识无效，无法下载。"));
        return;
    }
    const auto existingPath = existingLocalPath(normalizedAsset);
    if (!existingPath.isEmpty())
    {
        emit fileAvailable(normalizedAsset, existingPath, {});
        if (action == PendingAction::Preview)
            openLocalFile(existingPath, QFileInfo(existingPath).fileName(), {});
        else
            emit notificationRequested(QStringLiteral("文件已下载：%1").arg(existingPath));
        return;
    }
    if (networkClient_ == nullptr)
    {
        emit notificationRequested(QStringLiteral("当前未连接服务端，且本地没有该文件。"));
        return;
    }

    const auto pending = pendingActions_.find(normalizedAsset);
    if (pending != pendingActions_.end())
    {
        // 后续双击比单纯下载意图更强，允许升级但不重复发送网络请求。
        if (action == PendingAction::Preview) pending.value() = PendingAction::Preview;
        emit notificationRequested(QStringLiteral("文件正在下载，请稍候。"));
        return;
    }
    pendingActions_.insert(normalizedAsset, action);
    networkClient_->downloadFile(normalizedAsset);
    emit notificationRequested(QStringLiteral("正在安全下载文件…"));
}

void FileTransferController::handleFileDownloaded(
    const QString& assetUuid, const QString& fileName,
    const QString& mediaType, const QByteArray& content)
{
    const auto action = pendingActions_.take(assetUuid);
    const auto directory = downloadDirectory();
    if (directory.isEmpty() || !QDir().mkpath(directory))
    {
        emit notificationRequested(QStringLiteral("无法创建下载目录，请在设置中选择可写路径。"));
        return;
    }
    const auto safeName = sanitizedFileName(fileName);
    const auto targetPath = uniqueTargetPath(directory, safeName);
    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(content) != content.size() || !output.commit())
    {
        output.cancelWriting();
        emit notificationRequested(QStringLiteral("保存文件失败，请检查下载目录权限或磁盘空间。"));
        return;
    }

    rememberLocalPath(assetUuid, targetPath);
    emit fileAvailable(assetUuid, targetPath, mediaType);
    if (action == PendingAction::Preview)
        openLocalFile(targetPath, safeName, mediaType);
    else
        emit notificationRequested(QStringLiteral("文件已下载：%1").arg(targetPath));
}

QString FileTransferController::downloadDirectory() const
{
    const auto configured = settingsModel_->profile().downloadPath.trimmed();
    if (!configured.isEmpty() && QDir::isAbsolutePath(configured))
        return QDir::cleanPath(configured);

    // 服务端默认值“Downloads”是跨平台偏好，不得相对解析到 EXE 安装目录。
    auto standard = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (standard.isEmpty())
        standard = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("Downloads"));
    return QDir::cleanPath(standard);
}

QString FileTransferController::existingLocalPath(const QString& assetUuid) const
{
    QSettings settings;
    const auto key = settingsKey(assetUuid);
    const auto path = settings.value(key).toString();
    if (!path.isEmpty() && QFileInfo(path).isFile() && QFileInfo(path).isReadable()) return path;
    if (!path.isEmpty()) settings.remove(key);
    return {};
}

QString FileTransferController::uniqueTargetPath(
    const QString& directory, const QString& safeFileName) const
{
    QDir targetDirectory(directory);
    auto candidate = targetDirectory.filePath(safeFileName);
    if (!QFileInfo::exists(candidate)) return candidate;

    const QFileInfo fileInfo(safeFileName);
    const auto baseName = fileInfo.completeBaseName();
    const auto suffix = fileInfo.suffix();
    for (int index = 2; index <= 9999; ++index)
    {
        const auto numbered = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(baseName).arg(index)
            : QStringLiteral("%1 (%2).%3").arg(baseName).arg(index).arg(suffix);
        candidate = targetDirectory.filePath(numbered);
        if (!QFileInfo::exists(candidate)) return candidate;
    }
    // 极端冲突时使用随机性足够的时间无关散列后缀，避免覆盖任一现有用户文件。
    const auto suffixHash = QString::fromLatin1(QCryptographicHash::hash(
        safeFileName.toUtf8() + QByteArray::number(qHash(assetUuid)), QCryptographicHash::Sha256).toHex().left(10));
    return targetDirectory.filePath(QStringLiteral("%1-%2").arg(baseName, suffixHash)
        + (suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix));
}

void FileTransferController::rememberLocalPath(
    const QString& assetUuid, const QString& localPath)
{
    QSettings settings;
    settings.setValue(settingsKey(assetUuid), QFileInfo(localPath).absoluteFilePath());
}

void FileTransferController::openLocalFile(
    const QString& localPath, const QString& displayName, const QString& mediaType)
{
    const auto kind = FilePreviewDialog::previewKind(localPath, mediaType);
    if (kind == FilePreviewKind::Blocked)
    {
        emit notificationRequested(QStringLiteral("安全策略禁止打开可执行文件或脚本；文件已保存供安全审查。"));
        return;
    }
    if (kind == FilePreviewKind::Image || kind == FilePreviewKind::Audio
        || kind == FilePreviewKind::Video)
    {
        auto* dialog = new FilePreviewDialog(localPath, displayName, mediaType, mainWindow_);
        if (!dialog->isReady())
        {
            const auto message = dialog->diagnostic();
            dialog->deleteLater();
            emit notificationRequested(message);
            return;
        }
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(localPath)))
    {
        emit notificationRequested(QStringLiteral("无法调用系统默认应用，请检查文件关联设置。"));
        return;
    }
    emit notificationRequested(QStringLiteral("已使用系统默认应用打开文件。"));
}

QString FileTransferController::settingsKey(const QString& assetUuid) const
{
    const auto digest = QCryptographicHash::hash(assetUuid.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("fileTransfers/%1/%2").arg(currentPersonId_).arg(QString::fromLatin1(digest));
}

} // namespace orglink::client
