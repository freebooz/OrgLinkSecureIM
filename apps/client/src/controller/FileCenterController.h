#pragma once

#include "model/FileCenterModel.h"
#include "network/NetworkClient.h"

#include <QObject>

namespace orglink::client
{

class FileCenterView;

/**
 * @brief 文件中心 Controller，协调分页 Model、三栏 View 与 NetworkClient。
 * @details 上传固定使用 conversationId=0；所有者、共享权限和配额均由服务端认证会话决定。
 */
class FileCenterController final : public QObject
{
    Q_OBJECT

public:
    FileCenterController(NetworkClient* networkClient, FileCenterModel* model,
                         FileCenterView* view, QObject* parent = nullptr);

public slots:
    /** @brief 登录成功后清空前一用户分页并加载当前认证用户文件中心。 */
    void initializeForUser(qulonglong personId);

signals:
    void notificationRequested(const QString& friendlyMessage);

private:
    [[nodiscard]] static FileCenterListItem mapItem(const RemoteFileCenterItem& item);
    [[nodiscard]] static FileCenterDetailItem mapDetail(const RemoteFileCenterDetail& detail);
    void refreshCurrentPage();

    NetworkClient* networkClient_{nullptr};
    FileCenterModel* model_{nullptr};
    FileCenterView* view_{nullptr};
    int currentScope_{0};
    int currentCategory_{0};
    QString searchText_;
    int offset_{0};
    int limit_{20};
};

} // namespace orglink::client
