#include "storage/LocalDirectoryRepository.h"

#include <orglink/application/SnapshotOrganizationRepository.h>

#include <QDateTime>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>
#endif

#include <stdexcept>
#include <algorithm>
#include <type_traits>
#include <utility>

namespace orglink::client
{
namespace
{

/** @brief 统一生成脱敏异常；数据库路径、驱动正文和 SQL 永远不进入上层 UI。 */
[[noreturn]] void fail(const char* message)
{
    throw std::runtime_error(message);
}

/** @brief 使用当前 Windows 用户保护目录敏感字段；非 Windows 返回空值，禁止明文降级。 */
QByteArray protectSensitive(const std::string& value)
{
    const auto inputBytes = QByteArray::fromStdString(value);
    if (inputBytes.isEmpty())
    {
        return {};
    }
#if defined(Q_OS_WIN)
    DATA_BLOB input{static_cast<DWORD>(inputBytes.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(inputBytes.constData()))};
    const QByteArray entropyBytes("OrgLinkSecureIM.LocalDirectory.v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()),
                      reinterpret_cast<BYTE*>(const_cast<char*>(entropyBytes.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"OrgLink directory field", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return {};
    }
    QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
#else
    static_cast<void>(inputBytes);
    return {};
#endif
}

/** @brief 解开 DPAPI 字段；密文损坏或平台未启用保护能力时返回空字段而不暴露内部错误。 */
std::string unprotectSensitive(const QByteArray& encrypted)
{
#if defined(Q_OS_WIN)
    if (encrypted.isEmpty())
    {
        return {};
    }
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))};
    const QByteArray entropyBytes("OrgLinkSecureIM.LocalDirectory.v1");
    DATA_BLOB entropy{static_cast<DWORD>(entropyBytes.size()),
                      reinterpret_cast<BYTE*>(const_cast<char*>(entropyBytes.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
    {
        return {};
    }
    std::string value(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return value;
#else
    static_cast<void>(encrypted);
    return {};
#endif
}

/** @brief 执行固定 SQL；调用者只传编译期语句，业务值必须继续使用 bindValue。 */
void executeFixed(QSqlDatabase database, const QString& sql, const char* diagnostic)
{
    QSqlQuery query(database);
    if (!query.exec(sql))
    {
        fail(diagnostic);
    }
}

/** @brief 按强类型主键插入或替换实体；重复事件只覆盖同一对象，不改变集合中其他记录。 */
template <typename Entity, typename IdAccessor>
void upsertEntity(std::vector<Entity>& entities, Entity entity, IdAccessor idAccessor)
{
    const auto existing = std::find_if(entities.begin(), entities.end(), [&](const auto& item) {
        return idAccessor(item) == idAccessor(entity);
    });
    if (existing == entities.end()) entities.push_back(std::move(entity));
    else *existing = std::move(entity);
}

} // namespace

LocalDirectoryRepository::LocalDirectoryRepository(QString rootOverride)
    : connectionName_(QStringLiteral("orglink-local-directory-%1")
          .arg(reinterpret_cast<quintptr>(this), 0, 16)),
      rootOverride_(std::move(rootOverride))
{
}

LocalDirectoryRepository::~LocalDirectoryRepository()
{
    close();
}

bool LocalDirectoryRepository::openForUser(qulonglong personId, QString& diagnostic)
{
    if (personId == 0)
    {
        diagnostic = QStringLiteral("本地目录用户无效。");
        return false;
    }
    close();
    const auto root = rootOverride_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : rootOverride_;
    if (root.isEmpty() || !QDir().mkpath(root))
    {
        diagnostic = QStringLiteral("无法创建本地目录缓存位置。");
        return false;
    }
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(QDir(root).filePath(QStringLiteral("directory-%1.sqlite").arg(personId)));
    database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!database_.open())
    {
        diagnostic = QStringLiteral("无法打开本地组织目录缓存。");
        close();
        return false;
    }
    personId_ = personId;
    QSqlQuery pragma(database_);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"))
        || !pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"))
        || !pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON")))
    {
        diagnostic = QStringLiteral("无法初始化本地目录缓存参数。");
        close();
        return false;
    }
    return ensureSchema(diagnostic);
}

bool LocalDirectoryRepository::ensureSchema(QString& diagnostic)
{
    try
    {
        executeFixed(database_, QStringLiteral("CREATE TABLE IF NOT EXISTS organizations("
            "id INTEGER PRIMARY KEY, code TEXT NOT NULL, name TEXT NOT NULL, parent_id INTEGER, "
            "revision INTEGER NOT NULL, enabled INTEGER NOT NULL)"), "无法创建组织缓存表");
        executeFixed(database_, QStringLiteral("CREATE TABLE IF NOT EXISTS departments("
            "id INTEGER PRIMARY KEY, organization_id INTEGER NOT NULL, parent_id INTEGER, code TEXT NOT NULL, "
            "name TEXT NOT NULL, short_name TEXT NOT NULL, sort_order INTEGER NOT NULL, enabled INTEGER NOT NULL)"),
            "无法创建部门缓存表");
        executeFixed(database_, QStringLiteral("CREATE TABLE IF NOT EXISTS positions("
            "id INTEGER PRIMARY KEY, code TEXT NOT NULL, name TEXT NOT NULL, sort_order INTEGER NOT NULL)"),
            "无法创建岗位缓存表");
        executeFixed(database_, QStringLiteral("CREATE TABLE IF NOT EXISTS persons("
            "id INTEGER PRIMARY KEY, employee_number TEXT NOT NULL, display_name TEXT NOT NULL, avatar_resource_id TEXT NOT NULL, "
            "work_phone_cipher BLOB, extension_cipher BLOB, work_email_cipher BLOB, primary_department_id INTEGER, "
            "primary_position_id INTEGER, enabled INTEGER NOT NULL)"), "无法创建人员缓存表");
        executeFixed(database_, QStringLiteral("CREATE TABLE IF NOT EXISTS person_assignments("
            "id INTEGER PRIMARY KEY, person_id INTEGER NOT NULL, department_id INTEGER NOT NULL, position_id INTEGER, "
            "primary_assignment INTEGER NOT NULL, sort_order INTEGER NOT NULL)"), "无法创建任职缓存表");
        executeFixed(database_, QStringLiteral("CREATE TABLE IF NOT EXISTS organization_sync_state("
            "singleton INTEGER PRIMARY KEY CHECK(singleton=1), local_revision INTEGER NOT NULL, "
            "last_sync_at_utc_ms INTEGER NOT NULL, last_sync_result TEXT NOT NULL)"), "无法创建目录同步状态表");
        diagnostic.clear();
        return true;
    }
    catch (const std::exception&)
    {
        diagnostic = QStringLiteral("无法升级本地组织目录缓存结构。");
        return false;
    }
}

void LocalDirectoryRepository::replaceSnapshot(domain::OrganizationSnapshot snapshot)
{
    if (!database_.isOpen())
    {
        fail("本地目录缓存尚未打开");
    }
    // 复用纯 C++ 校验器先检查引用闭合和修订单调性；只有完整快照才进入 SQLite 事务。
    application::SnapshotOrganizationRepository validator;
    validator.replaceSnapshot(snapshot);
    QSqlQuery revisionQuery(database_);
    if (!revisionQuery.exec(QStringLiteral(
            "SELECT local_revision FROM organization_sync_state WHERE singleton=1")))
    {
        fail("无法读取本地目录修订号");
    }
    if (revisionQuery.next() && revisionQuery.value(0).toULongLong() > snapshot.revision)
    {
        fail("服务端目录修订号低于本地缓存");
    }
    if (!database_.transaction())
    {
        fail("无法开始目录缓存事务");
    }
    try
    {
        executeFixed(database_, QStringLiteral("DELETE FROM person_assignments"), "无法清理旧任职缓存");
        executeFixed(database_, QStringLiteral("DELETE FROM persons"), "无法清理旧人员缓存");
        executeFixed(database_, QStringLiteral("DELETE FROM positions"), "无法清理旧岗位缓存");
        executeFixed(database_, QStringLiteral("DELETE FROM departments"), "无法清理旧部门缓存");
        executeFixed(database_, QStringLiteral("DELETE FROM organizations"), "无法清理旧组织缓存");

        QSqlQuery query(database_);
        query.prepare(QStringLiteral("INSERT INTO organizations VALUES(?,?,?,?,?,?)"));
        for (const auto& item : snapshot.organizations)
        {
            query.bindValue(0, QVariant::fromValue(item.id.value())); query.bindValue(1, QString::fromStdString(item.code));
            query.bindValue(2, QString::fromStdString(item.name));
            query.bindValue(3, item.parentId ? QVariant::fromValue(item.parentId->value()) : QVariant{});
            query.bindValue(4, QVariant::fromValue(item.revision)); query.bindValue(5, item.enabled);
            if (!query.exec()) fail("无法写入组织缓存");
        }
        query.prepare(QStringLiteral("INSERT INTO departments VALUES(?,?,?,?,?,?,?,?)"));
        for (const auto& item : snapshot.departments)
        {
            query.bindValue(0, QVariant::fromValue(item.id.value())); query.bindValue(1, QVariant::fromValue(item.organizationId.value()));
            query.bindValue(2, item.parentDepartmentId ? QVariant::fromValue(item.parentDepartmentId->value()) : QVariant{});
            query.bindValue(3, QString::fromStdString(item.code)); query.bindValue(4, QString::fromStdString(item.name));
            query.bindValue(5, QString::fromStdString(item.shortName)); query.bindValue(6, item.sortOrder); query.bindValue(7, item.enabled);
            if (!query.exec()) fail("无法写入部门缓存");
        }
        query.prepare(QStringLiteral("INSERT INTO positions VALUES(?,?,?,?)"));
        for (const auto& item : snapshot.positions)
        {
            query.bindValue(0, QVariant::fromValue(item.id.value())); query.bindValue(1, QString::fromStdString(item.code));
            query.bindValue(2, QString::fromStdString(item.name)); query.bindValue(3, item.sortOrder);
            if (!query.exec()) fail("无法写入岗位缓存");
        }
        query.prepare(QStringLiteral("INSERT INTO persons VALUES(?,?,?,?,?,?,?,?,?,?)"));
        for (const auto& item : snapshot.people)
        {
            const auto phoneCipher = protectSensitive(item.workPhone);
            const auto extensionCipher = protectSensitive(item.extensionNumber);
            const auto emailCipher = protectSensitive(item.workEmail);
#if defined(Q_OS_WIN)
            // DPAPI 异常时必须回滚整份快照，不能悄悄把有值的敏感字段替换为空缓存。
            if ((!item.workPhone.empty() && phoneCipher.isEmpty())
                || (!item.extensionNumber.empty() && extensionCipher.isEmpty())
                || (!item.workEmail.empty() && emailCipher.isEmpty()))
            {
                fail("无法保护目录敏感字段");
            }
#endif
            query.bindValue(0, QVariant::fromValue(item.id.value())); query.bindValue(1, QString::fromStdString(item.employeeNumber));
            query.bindValue(2, QString::fromStdString(item.displayName)); query.bindValue(3, QString::fromStdString(item.avatarResourceId));
            query.bindValue(4, phoneCipher); query.bindValue(5, extensionCipher); query.bindValue(6, emailCipher);
            query.bindValue(7, item.primaryDepartmentId ? QVariant::fromValue(item.primaryDepartmentId->value()) : QVariant{});
            query.bindValue(8, item.primaryPositionId ? QVariant::fromValue(item.primaryPositionId->value()) : QVariant{});
            query.bindValue(9, item.enabled);
            if (!query.exec()) fail("无法写入人员缓存");
        }
        query.prepare(QStringLiteral("INSERT INTO person_assignments VALUES(?,?,?,?,?,?)"));
        for (const auto& item : snapshot.assignments)
        {
            query.bindValue(0, QVariant::fromValue(item.id.value())); query.bindValue(1, QVariant::fromValue(item.personId.value()));
            query.bindValue(2, QVariant::fromValue(item.departmentId.value()));
            query.bindValue(3, item.positionId ? QVariant::fromValue(item.positionId->value()) : QVariant{});
            query.bindValue(4, item.primaryAssignment); query.bindValue(5, item.sortOrder);
            if (!query.exec()) fail("无法写入任职缓存");
        }
        query.prepare(QStringLiteral("INSERT INTO organization_sync_state VALUES(1,?,?,?) "
                                     "ON CONFLICT(singleton) DO UPDATE SET local_revision=excluded.local_revision, "
                                     "last_sync_at_utc_ms=excluded.last_sync_at_utc_ms, last_sync_result=excluded.last_sync_result"));
        query.bindValue(0, QVariant::fromValue(snapshot.revision));
        query.bindValue(1, QDateTime::currentMSecsSinceEpoch());
        query.bindValue(2, QStringLiteral("success"));
        if (!query.exec() || !database_.commit())
        {
            fail("无法提交目录缓存事务");
        }
    }
    catch (...)
    {
        database_.rollback();
        throw;
    }
}

void LocalDirectoryRepository::applyDelta(domain::OrganizationDelta delta)
{
    if (delta.fullSnapshotRequired || delta.fromRevision == 0
        || delta.currentRevision < delta.fromRevision)
    {
        fail("目录增量要求全量同步或修订范围无效");
    }
    auto snapshot = loadSnapshot();
    if (snapshot.revision != delta.fromRevision)
    {
        fail("目录增量起始修订与本地缓存不一致");
    }

    auto expectedRevision = delta.fromRevision;
    for (auto& change : delta.changes)
    {
        if (change.revision != ++expectedRevision || change.entityId == 0)
        {
            fail("目录增量修订不连续");
        }
        switch (change.kind)
        {
        case domain::DirectoryChangeKind::OrganizationCreated:
        case domain::DirectoryChangeKind::OrganizationUpdated:
        case domain::DirectoryChangeKind::OrganizationDisabled:
        {
            const auto* entity = std::get_if<domain::Organization>(&change.payload);
            if (entity == nullptr || entity->id.value() != change.entityId
                || (change.kind == domain::DirectoryChangeKind::OrganizationDisabled && entity->enabled))
                fail("组织增量载荷与事件类型不匹配");
            upsertEntity(snapshot.organizations, *entity, [](const auto& item) { return item.id.value(); });
            break;
        }
        case domain::DirectoryChangeKind::DepartmentCreated:
        case domain::DirectoryChangeKind::DepartmentUpdated:
        case domain::DirectoryChangeKind::DepartmentMoved:
        case domain::DirectoryChangeKind::DepartmentDisabled:
        {
            const auto* entity = std::get_if<domain::Department>(&change.payload);
            if (entity == nullptr || entity->id.value() != change.entityId
                || (change.kind == domain::DirectoryChangeKind::DepartmentDisabled && entity->enabled))
                fail("部门增量载荷与事件类型不匹配");
            upsertEntity(snapshot.departments, *entity, [](const auto& item) { return item.id.value(); });
            break;
        }
        case domain::DirectoryChangeKind::PositionUpserted:
        {
            const auto* entity = std::get_if<domain::Position>(&change.payload);
            if (entity == nullptr || entity->id.value() != change.entityId)
                fail("岗位增量载荷与事件类型不匹配");
            upsertEntity(snapshot.positions, *entity, [](const auto& item) { return item.id.value(); });
            break;
        }
        case domain::DirectoryChangeKind::PersonCreated:
        case domain::DirectoryChangeKind::PersonUpdated:
        case domain::DirectoryChangeKind::PersonDisabled:
        {
            const auto* entity = std::get_if<domain::Person>(&change.payload);
            if (entity == nullptr || entity->id.value() != change.entityId
                || (change.kind == domain::DirectoryChangeKind::PersonDisabled && entity->enabled))
                fail("人员增量载荷与事件类型不匹配");
            upsertEntity(snapshot.people, *entity, [](const auto& item) { return item.id.value(); });
            break;
        }
        case domain::DirectoryChangeKind::PersonAssignmentChanged:
        {
            const auto* entity = std::get_if<domain::PersonAssignment>(&change.payload);
            if (entity == nullptr || entity->id.value() != change.entityId)
                fail("任职增量载荷与事件类型不匹配");
            upsertEntity(snapshot.assignments, *entity, [](const auto& item) { return item.id.value(); });
            break;
        }
        case domain::DirectoryChangeKind::Removed:
            // 硬删除可能影响多条外键关系；在没有完整墓碑关系前禁止猜测局部删除顺序。
            fail("目录硬删除必须回退全量同步");
        }
    }
    if (expectedRevision != delta.currentRevision)
    {
        fail("目录增量未覆盖服务端当前修订");
    }
    snapshot.revision = delta.currentRevision;
    for (auto& organization : snapshot.organizations)
    {
        organization.revision = delta.currentRevision;
    }
    // replaceSnapshot 会先做引用闭合校验，再用单事务替换；失败时 SQLite 自动保留旧修订。
    replaceSnapshot(std::move(snapshot));
}

qulonglong LocalDirectoryRepository::synchronizedRevision() const
{
    if (!database_.isOpen())
    {
        fail("本地目录缓存尚未打开");
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT local_revision FROM organization_sync_state WHERE singleton=1")))
    {
        fail("无法读取本地目录修订号");
    }
    return query.next() ? query.value(0).toULongLong() : 0;
}

domain::OrganizationSnapshot LocalDirectoryRepository::loadSnapshot() const
{
    if (!database_.isOpen())
    {
        fail("本地目录缓存尚未打开");
    }
    domain::OrganizationSnapshot snapshot;
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT local_revision FROM organization_sync_state WHERE singleton=1")) || !query.next())
    {
        fail("本地组织目录尚未完成同步");
    }
    snapshot.revision = query.value(0).toULongLong();
    if (!query.exec(QStringLiteral("SELECT id,code,name,parent_id,revision,enabled FROM organizations ORDER BY id"))) fail("无法读取组织缓存");
    while (query.next()) snapshot.organizations.push_back({domain::OrganizationId{query.value(0).toULongLong()},
        query.value(1).toString().toStdString(), query.value(2).toString().toStdString(), query.value(3).isNull() ? std::nullopt
            : std::optional{domain::OrganizationId{query.value(3).toULongLong()}}, query.value(4).toULongLong(), query.value(5).toBool()});
    if (!query.exec(QStringLiteral("SELECT id,organization_id,parent_id,code,name,short_name,sort_order,enabled FROM departments ORDER BY sort_order,id"))) fail("无法读取部门缓存");
    while (query.next()) snapshot.departments.push_back({domain::DepartmentId{query.value(0).toULongLong()}, domain::OrganizationId{query.value(1).toULongLong()},
        query.value(2).isNull() ? std::nullopt : std::optional{domain::DepartmentId{query.value(2).toULongLong()}}, query.value(3).toString().toStdString(),
        query.value(4).toString().toStdString(), query.value(5).toString().toStdString(), query.value(6).toInt(), query.value(7).toBool()});
    if (!query.exec(QStringLiteral("SELECT id,code,name,sort_order FROM positions ORDER BY sort_order,id"))) fail("无法读取岗位缓存");
    while (query.next()) snapshot.positions.push_back({domain::PositionId{query.value(0).toULongLong()}, query.value(1).toString().toStdString(),
        query.value(2).toString().toStdString(), query.value(3).toInt()});
    if (!query.exec(QStringLiteral("SELECT id,employee_number,display_name,avatar_resource_id,work_phone_cipher,extension_cipher,work_email_cipher,primary_department_id,primary_position_id,enabled FROM persons ORDER BY display_name,id"))) fail("无法读取人员缓存");
    while (query.next()) snapshot.people.push_back({domain::PersonId{query.value(0).toULongLong()}, query.value(1).toString().toStdString(),
        query.value(2).toString().toStdString(), query.value(3).toString().toStdString(), unprotectSensitive(query.value(4).toByteArray()),
        unprotectSensitive(query.value(5).toByteArray()), unprotectSensitive(query.value(6).toByteArray()),
        query.value(7).isNull() ? std::nullopt : std::optional{domain::DepartmentId{query.value(7).toULongLong()}},
        query.value(8).isNull() ? std::nullopt : std::optional{domain::PositionId{query.value(8).toULongLong()}}, query.value(9).toBool()});
    if (!query.exec(QStringLiteral("SELECT id,person_id,department_id,position_id,primary_assignment,sort_order FROM person_assignments ORDER BY sort_order,id"))) fail("无法读取任职缓存");
    while (query.next()) snapshot.assignments.push_back({domain::PersonAssignmentId{query.value(0).toULongLong()}, domain::PersonId{query.value(1).toULongLong()},
        domain::DepartmentId{query.value(2).toULongLong()}, query.value(3).isNull() ? std::nullopt
            : std::optional{domain::PositionId{query.value(3).toULongLong()}}, query.value(4).toBool(), query.value(5).toInt()});

    // 磁盘缓存同样经过引用验证，损坏或越权残留不会进入 Qt Model。
    application::SnapshotOrganizationRepository validator;
    validator.replaceSnapshot(snapshot);
    return snapshot;
}

void LocalDirectoryRepository::close()
{
    personId_ = 0;
    if (database_.isValid())
    {
        database_.close();
        database_ = QSqlDatabase{};
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

} // namespace orglink::client
