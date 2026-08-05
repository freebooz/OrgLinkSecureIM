#include "controller/OrganizationController.h"

#include "model/DepartmentPersonnelModel.h"
#include "model/OrganizationTreeModel.h"

#include <QString>

#include <algorithm>

namespace orglink::client
{

OrganizationController::OrganizationController(
    application::OrganizationService& service,
    OrganizationTreeModel* organizationModel,
    DepartmentPersonnelModel* personnelModel,
    QObject* parent)
    : QObject(parent), service_(service), organizationModel_(organizationModel), personnelModel_(personnelModel)
{
    Q_ASSERT(organizationModel_ != nullptr);
    Q_ASSERT(personnelModel_ != nullptr);
}

void OrganizationController::initialize()
{
    try
    {
        // 先完整取得快照，再更新 Model；仓储抛错时不会破坏用户当前看到的缓存。
        const auto nextSnapshot = service_.snapshot();
        snapshot_ = nextSnapshot;
        organizationModel_->setSnapshot(snapshot_);
        if (!snapshot_.departments.empty())
        {
            selectDepartment(snapshot_.departments.front().id.value());
        }
    }
    catch (const std::exception&)
    {
        emit directoryError(QStringLiteral("组织目录同步失败，已保留本地已有数据。"));
    }
}

void OrganizationController::selectDepartment(qulonglong departmentId)
{
    selectedDepartmentId_ = domain::DepartmentId{departmentId};
    const auto people = service_.peopleForDepartment(*selectedDepartmentId_, true);
    personnelModel_->setPeople(people, snapshot_);
    const auto selected = std::ranges::find_if(snapshot_.departments, [departmentId](const auto& department) {
        return department.id.value() == departmentId;
    });
    QString breadcrumb = selected == snapshot_.departments.end()
        ? QStringLiteral("部门成员") : QString::fromStdString(selected->name);
    if (selected != snapshot_.departments.end() && selected->parentDepartmentId)
    {
        const auto parent = std::ranges::find_if(snapshot_.departments, [&](const auto& department) {
            return department.id == *selected->parentDepartmentId;
        });
        if (parent != snapshot_.departments.end())
            breadcrumb = QStringLiteral("%1 / %2").arg(QString::fromStdString(parent->name), breadcrumb);
    }
    emit departmentContextChanged(breadcrumb, static_cast<int>(people.size()));
}

void OrganizationController::searchDirectory(const QString& keyword)
{
    if (keyword.trimmed().isEmpty())
    {
        if (selectedDepartmentId_)
        {
            selectDepartment(selectedDepartmentId_->value());
        }
        return;
    }
    personnelModel_->setPeople(service_.searchPeople(keyword.toStdString()), snapshot_);
}

} // namespace orglink::client
