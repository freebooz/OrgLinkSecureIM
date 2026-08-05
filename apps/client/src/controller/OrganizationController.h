#pragma once

#include <orglink/application/OrganizationService.h>

#include <QObject>

namespace orglink::client
{

class DepartmentPersonnelModel;
class OrganizationTreeModel;

/** @brief 组织目录 Controller，协调 Service 查询和两个 Qt Model 更新，不访问具体仓储。 */
class OrganizationController final : public QObject
{
    Q_OBJECT

public:
    OrganizationController(application::OrganizationService& service,
                           OrganizationTreeModel* organizationModel,
                           DepartmentPersonnelModel* personnelModel,
                           QObject* parent = nullptr);

public slots:
    /** @brief 加载目录快照并默认选择首个可用部门；失败时保留 Model 的已有数据。 */
    void initialize();

    /** @brief 加载指定部门及下级部门人员；无效 ID 显示空结果而不崩溃。 */
    void selectDepartment(qulonglong departmentId);

    /** @brief 执行受限人员搜索；空关键字恢复当前部门结果。 */
    void searchDirectory(const QString& keyword);

signals:
    void directoryError(const QString& friendlyMessage);
    /** @brief 通知 View 更新部门标题和人数；标题来自已校验目录快照，不接受界面拼接 ID。 */
    void departmentContextChanged(const QString& breadcrumb, int personCount);

private:
    application::OrganizationService& service_;
    OrganizationTreeModel* organizationModel_{nullptr};
    DepartmentPersonnelModel* personnelModel_{nullptr};
    domain::OrganizationSnapshot snapshot_;
    std::optional<domain::DepartmentId> selectedDepartmentId_;
};

} // namespace orglink::client
