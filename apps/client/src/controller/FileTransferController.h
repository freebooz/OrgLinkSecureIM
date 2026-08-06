#pragma once

#include <QHash>
#include <QObject>

namespace orglink::client
{

class MainWindow;
class NetworkClient;
class SettingsModel;

/**
 * @brief 客户端文件下载、原子落盘、本地缓存和安全打开用例 Controller。
 *
 * 所有模块的下载意图统一进入本类，避免消息、群组、通知和文件中心分别保存同一响应。
 * 服务端仍负责成员/共享权限与对象完整性校验；本类只在用户下载目录写入经过验证的正文，
 * 并阻止可执行文件和脚本被双击启动。
 */
class FileTransferController final : public QObject
{
    Q_OBJECT

public:
    FileTransferController(NetworkClient* networkClient, SettingsModel* settingsModel,
                           MainWindow* mainWindow, QObject* parent = nullptr);

    /**
     * @brief 清理前一账号待处理状态并切换本地映射命名空间。
     * @param personId 已认证人员编号，只用于隔离本机路径映射，不进入网络权限参数。
     */
    void initializeForUser(qulonglong personId);

    /**
     * @brief 将服务端文件名裁剪为单一安全文件名。
     * @return 不包含路径、控制字符、Windows 保留名或尾随点号的名称。
     */
    [[nodiscard]] static QString sanitizedFileName(const QString& fileName);

public slots:
    /** @brief 双击打开意图；已有本地副本时立即打开，否则先请求鉴权下载再预览。 */
    void requestOpen(const QString& assetUuid);
    /** @brief 显式下载意图；原子保存后只提示路径，不自动弹出预览。 */
    void requestDownload(const QString& assetUuid);

signals:
    /** @brief 向公共外壳发送脱敏状态，不包含对象键、正文或服务器内部地址。 */
    void notificationRequested(const QString& friendlyMessage);
    /** @brief 文件已安全落盘；文件中心可据此刷新当前详情缩略图。 */
    void fileAvailable(const QString& assetUuid, const QString& localPath,
                       const QString& mediaType);

private slots:
    /** @brief 处理 Gateway 已鉴权且已校验摘要的正文；写入失败不会保留部分文件。 */
    void handleFileDownloaded(const QString& assetUuid, const QString& fileName,
                              const QString& mediaType, const QByteArray& content);

private:
    /** @brief 下载完成后的用户意图；Preview 会继续打开，SaveOnly 仅通知。 */
    enum class PendingAction { Preview, SaveOnly };

    /** @brief 合并重复点击并向网络门面发出一次下载请求。 */
    void request(const QString& assetUuid, PendingAction action);
    /** @brief 返回设置指定的绝对下载目录；相对默认值不会解析到安装目录。 */
    [[nodiscard]] QString downloadDirectory() const;
    /** @brief 读取当前账号与资产对应的现存本地文件；失效映射会立即清理。 */
    [[nodiscard]] QString existingLocalPath(const QString& assetUuid) const;
    /** @brief 生成不覆盖用户已有文件的目标路径。 */
    [[nodiscard]] QString uniqueTargetPath(const QString& directory,
                                           const QString& safeFileName) const;
    /** @brief 保存资产到当前账号映射并同步 QSettings。 */
    void rememberLocalPath(const QString& assetUuid, const QString& localPath);
    /** @brief 打开应用内预览或系统文档应用；危险类型只显示安全提示。 */
    void openLocalFile(const QString& localPath, const QString& displayName,
                       const QString& mediaType);
    /** @brief 生成不暴露资产标识的本地 QSettings 键。 */
    [[nodiscard]] QString settingsKey(const QString& assetUuid) const;

    /** @brief 网络门面、设置快照和主窗口均由组合根持有，生命周期长于本 Controller。 */
    NetworkClient* networkClient_{nullptr};
    SettingsModel* settingsModel_{nullptr};
    MainWindow* mainWindow_{nullptr};
    /** @brief 当前认证人员编号；0 表示尚未绑定账号。 */
    qulonglong currentPersonId_{0};
    /** @brief 资产到未完成操作的进程内映射；重复双击不会重复下载。 */
    QHash<QString, PendingAction> pendingActions_;
};

} // namespace orglink::client
