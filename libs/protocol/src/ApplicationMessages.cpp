#include <orglink/protocol/ApplicationMessages.h>

#include <limits>
#include <string_view>
#include <utility>

namespace orglink::protocol
{
namespace
{

constexpr std::size_t MaximumLoginNameBytes = 128;
constexpr std::size_t MaximumPasswordBytes = 1024;
constexpr std::size_t MaximumIdentifierBytes = 128;
constexpr std::size_t MaximumFriendlyTextBytes = 512;
constexpr std::size_t MaximumMessageContentBytes = 64U * 1024U;
constexpr std::size_t MaximumFileContentBytes = 8U * 1024U * 1024U;
constexpr std::size_t MaximumConversationCount = 200U;
constexpr std::size_t MaximumHistoryCount = 100U;
constexpr std::size_t MaximumDirectoryRecordBytes = 8U * 1024U;
constexpr std::size_t MaximumDirectoryRecordCount = 300'000U;
constexpr std::size_t MaximumDirectoryDeltaCount = 500U;
constexpr std::size_t MaximumGroupCount = 200U;
constexpr std::size_t MaximumGroupMemberCount = 500U;
constexpr std::size_t MaximumGroupFileCount = 50U;
constexpr std::size_t MaximumGroupTagCount = 12U;
constexpr std::size_t MaximumNotificationCount = 100U;
constexpr std::size_t MaximumNotificationFieldCount = 32U;
constexpr std::size_t MaximumNotificationAttachmentCount = 20U;
constexpr std::size_t MaximumContactSummaryCount = 100U;
constexpr std::size_t MaximumContactGroupCount = 100U;
constexpr std::size_t MaximumContactTagCount = 12U;
constexpr std::size_t MaximumFileCenterItemCount = 100U;
constexpr std::size_t MaximumFileCenterVersionCount = 50U;
constexpr std::size_t MaximumFileCenterPermissionCount = 100U;
constexpr std::size_t MaximumCalendarEventCount = 500U;
constexpr std::size_t MaximumCalendarParticipantCount = 64U;

/** @brief 写入 protobuf varint；仅处理无符号整数，避免负数 ZigZag 语义混淆。 */
void writeVarint(std::vector<std::byte>& output, std::uint64_t value)
{
    while (value >= 0x80U)
    {
        output.push_back(static_cast<std::byte>((value & 0x7fU) | 0x80U));
        value >>= 7U;
    }
    output.push_back(static_cast<std::byte>(value));
}

void writeTag(std::vector<std::byte>& output, std::uint32_t field, std::uint8_t wireType)
{
    writeVarint(output, (static_cast<std::uint64_t>(field) << 3U) | wireType);
}

void writeUnsigned(std::vector<std::byte>& output, std::uint32_t field, std::uint64_t value)
{
    if (value == 0)
    {
        return;
    }
    writeTag(output, field, 0U);
    writeVarint(output, value);
}

void writeBoolean(std::vector<std::byte>& output, std::uint32_t field, bool value)
{
    writeUnsigned(output, field, value ? 1U : 0U);
}

void writeString(std::vector<std::byte>& output, std::uint32_t field, std::string_view value)
{
    if (value.empty())
    {
        return;
    }
    writeTag(output, field, 2U);
    writeVarint(output, value.size());
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    output.insert(output.end(), bytes.begin(), bytes.end());
}

/** @brief 写入嵌套 protobuf 消息；空记录仍需保留字段边界，因此不沿用字符串的空值省略策略。 */
void writeNested(std::vector<std::byte>& output, std::uint32_t field, std::span<const std::byte> value)
{
    writeTag(output, field, 2U);
    writeVarint(output, value.size());
    output.insert(output.end(), value.begin(), value.end());
}

/** @brief 有界 protobuf 读取器；所有偏移运算先验证剩余长度，阻止截断包触发越界。 */
class Reader final
{
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool empty() const noexcept { return offset_ == bytes_.size(); }

    [[nodiscard]] std::pair<std::uint32_t, std::uint8_t> readTag()
    {
        const auto tag = readVarint();
        const auto field = static_cast<std::uint32_t>(tag >> 3U);
        const auto wireType = static_cast<std::uint8_t>(tag & 0x07U);
        if (field == 0)
        {
            throw MessageCodecError("protobuf 字段编号不能为零");
        }
        return {field, wireType};
    }

    [[nodiscard]] std::uint64_t readUnsigned(std::uint8_t wireType)
    {
        requireWireType(wireType, 0U);
        return readVarint();
    }

    [[nodiscard]] std::string readString(std::uint8_t wireType, std::size_t maximumBytes)
    {
        requireWireType(wireType, 2U);
        const auto length64 = readVarint();
        if (length64 > maximumBytes || length64 > bytes_.size() - offset_)
        {
            throw MessageCodecError("protobuf 字符串长度超限或数据被截断");
        }
        const auto length = static_cast<std::size_t>(length64);
        const auto* start = reinterpret_cast<const char*>(bytes_.data() + offset_);
        std::string value(start, length);
        offset_ += length;
        return value;
    }

    /** @brief 复制有界嵌套消息，确保外层 Reader 的游标在递归解码前已安全推进。 */
    [[nodiscard]] std::vector<std::byte> readNested(std::uint8_t wireType, std::size_t maximumBytes)
    {
        requireWireType(wireType, 2U);
        const auto length64 = readVarint();
        if (length64 > maximumBytes || length64 > bytes_.size() - offset_)
        {
            throw MessageCodecError("protobuf 嵌套消息长度超限或数据被截断");
        }
        const auto length = static_cast<std::size_t>(length64);
        std::vector<std::byte> value(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
        offset_ += length;
        return value;
    }

    void skip(std::uint8_t wireType)
    {
        switch (wireType)
        {
        case 0U:
            static_cast<void>(readVarint());
            return;
        case 1U:
            skipBytes(8U);
            return;
        case 2U:
        {
            const auto length = readVarint();
            if (length > std::numeric_limits<std::size_t>::max())
            {
                throw MessageCodecError("protobuf 未知字段长度超限");
            }
            skipBytes(static_cast<std::size_t>(length));
            return;
        }
        case 5U:
            skipBytes(4U);
            return;
        default:
            throw MessageCodecError("不支持的 protobuf wire type");
        }
    }

private:
    [[nodiscard]] std::uint64_t readVarint()
    {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < 10U; ++index)
        {
            if (offset_ >= bytes_.size())
            {
                throw MessageCodecError("protobuf varint 被截断");
            }
            const auto byte = std::to_integer<std::uint8_t>(bytes_[offset_++]);
            if (index == 9U && byte > 1U)
            {
                throw MessageCodecError("protobuf varint 溢出 64 位");
            }
            value |= static_cast<std::uint64_t>(byte & 0x7fU) << (index * 7U);
            if ((byte & 0x80U) == 0)
            {
                return value;
            }
        }
        throw MessageCodecError("protobuf varint 超过十字节");
    }

    static void requireWireType(std::uint8_t actual, std::uint8_t expected)
    {
        if (actual != expected)
        {
            throw MessageCodecError("protobuf 字段 wire type 不匹配");
        }
    }

    void skipBytes(std::size_t count)
    {
        if (count > bytes_.size() - offset_)
        {
            throw MessageCodecError("protobuf 未知字段被截断");
        }
        offset_ += count;
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{0};
};

template <typename Message, typename FieldHandler>
Message decode(std::span<const std::byte> bytes, FieldHandler&& handler)
{
    Reader reader(bytes);
    Message value;
    while (!reader.empty())
    {
        const auto [field, wireType] = reader.readTag();
        if (!handler(value, reader, field, wireType))
        {
            reader.skip(wireType);
        }
    }
    return value;
}

/** @brief 对重复目录记录设置统一上限，防止恶意快照造成客户端无界内存增长。 */
template <typename Collection, typename Value>
void appendDirectoryRecord(Collection& collection, Value&& value)
{
    if (collection.size() >= MaximumDirectoryRecordCount)
    {
        throw MessageCodecError("目录记录数量超过上限");
    }
    collection.push_back(std::forward<Value>(value));
}

std::vector<std::byte> encodeDirectoryOrganization(const DirectoryOrganization& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.id);
    writeString(output, 2, value.code);
    writeString(output, 3, value.name);
    writeUnsigned(output, 4, value.parentOrganizationId);
    writeUnsigned(output, 5, value.revision);
    writeBoolean(output, 6, value.enabled);
    return output;
}

DirectoryOrganization decodeDirectoryOrganization(std::span<const std::byte> bytes)
{
    return decode<DirectoryOrganization>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.id = reader.readUnsigned(wire); return true;
        case 2: value.code = reader.readString(wire, 64); return true;
        case 3: value.name = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.parentOrganizationId = reader.readUnsigned(wire); return true;
        case 5: value.revision = reader.readUnsigned(wire); return true;
        case 6: value.enabled = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeDirectoryDepartment(const DirectoryDepartment& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.id);
    writeUnsigned(output, 2, value.organizationId);
    writeUnsigned(output, 3, value.parentDepartmentId);
    writeString(output, 4, value.code);
    writeString(output, 5, value.name);
    writeString(output, 6, value.shortName);
    writeUnsigned(output, 7, static_cast<std::uint64_t>(value.sortOrder));
    writeBoolean(output, 8, value.enabled);
    return output;
}

DirectoryDepartment decodeDirectoryDepartment(std::span<const std::byte> bytes)
{
    return decode<DirectoryDepartment>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.id = reader.readUnsigned(wire); return true;
        case 2: value.organizationId = reader.readUnsigned(wire); return true;
        case 3: value.parentDepartmentId = reader.readUnsigned(wire); return true;
        case 4: value.code = reader.readString(wire, 64); return true;
        case 5: value.name = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 6: value.shortName = reader.readString(wire, 128); return true;
        case 7: value.sortOrder = static_cast<std::int32_t>(reader.readUnsigned(wire)); return true;
        case 8: value.enabled = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeDirectoryPosition(const DirectoryPosition& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.id);
    writeString(output, 2, value.code);
    writeString(output, 3, value.name);
    writeUnsigned(output, 4, static_cast<std::uint64_t>(value.sortOrder));
    return output;
}

DirectoryPosition decodeDirectoryPosition(std::span<const std::byte> bytes)
{
    return decode<DirectoryPosition>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.id = reader.readUnsigned(wire); return true;
        case 2: value.code = reader.readString(wire, 64); return true;
        case 3: value.name = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.sortOrder = static_cast<std::int32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeDirectoryPerson(const DirectoryPerson& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.id);
    writeString(output, 2, value.employeeNumber);
    writeString(output, 3, value.displayName);
    writeString(output, 4, value.avatarResourceId);
    writeString(output, 5, value.workPhone);
    writeString(output, 6, value.extensionNumber);
    writeString(output, 7, value.workEmail);
    writeUnsigned(output, 8, value.primaryDepartmentId);
    writeUnsigned(output, 9, value.primaryPositionId);
    writeBoolean(output, 10, value.enabled);
    writeUnsigned(output, 11, value.presenceState);
    return output;
}

DirectoryPerson decodeDirectoryPerson(std::span<const std::byte> bytes)
{
    return decode<DirectoryPerson>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.id = reader.readUnsigned(wire); return true;
        case 2: value.employeeNumber = reader.readString(wire, 64); return true;
        case 3: value.displayName = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.avatarResourceId = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 5: value.workPhone = reader.readString(wire, 64); return true;
        case 6: value.extensionNumber = reader.readString(wire, 32); return true;
        case 7: value.workEmail = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 8: value.primaryDepartmentId = reader.readUnsigned(wire); return true;
        case 9: value.primaryPositionId = reader.readUnsigned(wire); return true;
        case 10: value.enabled = reader.readUnsigned(wire) != 0; return true;
        case 11: value.presenceState = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeDirectoryAssignment(const DirectoryAssignment& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.id);
    writeUnsigned(output, 2, value.personId);
    writeUnsigned(output, 3, value.departmentId);
    writeUnsigned(output, 4, value.positionId);
    writeBoolean(output, 5, value.primaryAssignment);
    writeUnsigned(output, 6, static_cast<std::uint64_t>(value.sortOrder));
    return output;
}

DirectoryAssignment decodeDirectoryAssignment(std::span<const std::byte> bytes)
{
    return decode<DirectoryAssignment>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.id = reader.readUnsigned(wire); return true;
        case 2: value.personId = reader.readUnsigned(wire); return true;
        case 3: value.departmentId = reader.readUnsigned(wire); return true;
        case 4: value.positionId = reader.readUnsigned(wire); return true;
        case 5: value.primaryAssignment = reader.readUnsigned(wire) != 0; return true;
        case 6: value.sortOrder = static_cast<std::int32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

/** @brief 校验增量类型和类型化实体的一一对应关系，防止错误 payload 被应用到其他目录表。 */
bool directoryChangePayloadMatches(const DirectoryChange& value)
{
    const auto payloadCount = static_cast<unsigned>(value.organization.has_value())
        + static_cast<unsigned>(value.department.has_value())
        + static_cast<unsigned>(value.position.has_value())
        + static_cast<unsigned>(value.person.has_value())
        + static_cast<unsigned>(value.assignment.has_value());
    switch (value.type)
    {
    case DirectoryChangeType::OrganizationCreated:
    case DirectoryChangeType::OrganizationUpdated:
    case DirectoryChangeType::OrganizationDisabled:
        return payloadCount == 1U && value.organization.has_value();
    case DirectoryChangeType::DepartmentCreated:
    case DirectoryChangeType::DepartmentUpdated:
    case DirectoryChangeType::DepartmentMoved:
    case DirectoryChangeType::DepartmentDisabled:
        return payloadCount == 1U && value.department.has_value();
    case DirectoryChangeType::PersonCreated:
    case DirectoryChangeType::PersonUpdated:
    case DirectoryChangeType::PersonDisabled:
        return payloadCount == 1U && value.person.has_value();
    case DirectoryChangeType::PersonAssignmentChanged:
        return payloadCount == 1U && value.assignment.has_value();
    case DirectoryChangeType::PositionUpserted:
        return payloadCount == 1U && value.position.has_value();
    case DirectoryChangeType::Removed:
        return payloadCount == 0U;
    case DirectoryChangeType::Unknown:
        return false;
    }
    return false;
}

std::vector<std::byte> encodeDirectoryChange(const DirectoryChange& value)
{
    if (value.revision == 0 || value.entityId == 0 || !directoryChangePayloadMatches(value))
    {
        throw MessageCodecError("目录增量事件类型或实体无效");
    }
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.revision);
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.type));
    writeUnsigned(output, 3, value.entityId);
    if (value.organization) writeNested(output, 10, encodeDirectoryOrganization(*value.organization));
    if (value.department) writeNested(output, 11, encodeDirectoryDepartment(*value.department));
    if (value.position) writeNested(output, 12, encodeDirectoryPosition(*value.position));
    if (value.person) writeNested(output, 13, encodeDirectoryPerson(*value.person));
    if (value.assignment) writeNested(output, 14, encodeDirectoryAssignment(*value.assignment));
    return output;
}

DirectoryChange decodeDirectoryChange(std::span<const std::byte> bytes)
{
    auto value = decode<DirectoryChange>(bytes, [](auto& item, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: item.revision = reader.readUnsigned(wire); return true;
        case 2: item.type = static_cast<DirectoryChangeType>(reader.readUnsigned(wire)); return true;
        case 3: item.entityId = reader.readUnsigned(wire); return true;
        case 10: item.organization = decodeDirectoryOrganization(
            reader.readNested(wire, MaximumDirectoryRecordBytes)); return true;
        case 11: item.department = decodeDirectoryDepartment(
            reader.readNested(wire, MaximumDirectoryRecordBytes)); return true;
        case 12: item.position = decodeDirectoryPosition(
            reader.readNested(wire, MaximumDirectoryRecordBytes)); return true;
        case 13: item.person = decodeDirectoryPerson(
            reader.readNested(wire, MaximumDirectoryRecordBytes)); return true;
        case 14: item.assignment = decodeDirectoryAssignment(
            reader.readNested(wire, MaximumDirectoryRecordBytes)); return true;
        default: return false;
        }
    });
    if (value.revision == 0 || value.entityId == 0 || !directoryChangePayloadMatches(value))
    {
        throw MessageCodecError("目录增量事件类型或实体无效");
    }
    return value;
}

/** @brief 编码群组摘要；字段只包含成员可见投影，不写入对象存储键或数据库内部行号。 */
std::vector<std::byte> encodeGroupSummaryRecord(const GroupSummary& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.groupId);
    writeUnsigned(output, 2, value.conversationId);
    writeString(output, 3, value.groupCode);
    writeString(output, 4, value.name);
    writeUnsigned(output, 5, static_cast<std::uint32_t>(value.type));
    writeUnsigned(output, 6, value.memberCount);
    writeString(output, 7, value.lastMessagePreview);
    writeUnsigned(output, 8, value.lastActivityUtcMs);
    writeUnsigned(output, 9, value.unreadCount);
    writeUnsigned(output, 10, value.activityScore);
    for (const auto& tag : value.tags)
    {
        writeString(output, 11, tag);
    }
    writeBoolean(output, 12, value.owner);
    writeBoolean(output, 13, value.administrator);
    writeBoolean(output, 14, value.pinned);
    writeBoolean(output, 15, value.favorite);
    return output;
}

/** @brief 解码单条群组摘要并限制标签数量，防止异常负载导致客户端无界分配。 */
GroupSummary decodeGroupSummaryRecord(std::span<const std::byte> bytes)
{
    return decode<GroupSummary>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.groupId = reader.readUnsigned(wire); return true;
        case 2: value.conversationId = reader.readUnsigned(wire); return true;
        case 3: value.groupCode = reader.readString(wire, 32); return true;
        case 4: value.name = reader.readString(wire, 255); return true;
        case 5: value.type = static_cast<GroupType>(reader.readUnsigned(wire)); return true;
        case 6: value.memberCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 7: value.lastMessagePreview = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 8: value.lastActivityUtcMs = reader.readUnsigned(wire); return true;
        case 9: value.unreadCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 10: value.activityScore = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 11:
            if (value.tags.size() >= MaximumGroupTagCount)
                throw MessageCodecError("群组标签数量超过上限");
            value.tags.push_back(reader.readString(wire, 64));
            return true;
        case 12: value.owner = reader.readUnsigned(wire) != 0; return true;
        case 13: value.administrator = reader.readUnsigned(wire) != 0; return true;
        case 14: value.pinned = reader.readUnsigned(wire) != 0; return true;
        case 15: value.favorite = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

/** @brief 编码群成员预览；成员角色由服务端计算，客户端输入不得回显覆盖。 */
std::vector<std::byte> encodeGroupMemberRecord(const GroupMemberInfo& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.personId);
    writeString(output, 2, value.displayName);
    writeString(output, 3, value.departmentName);
    writeString(output, 4, value.positionName);
    writeString(output, 5, value.avatarResourceId);
    writeUnsigned(output, 6, value.role);
    writeUnsigned(output, 7, value.joinedAtUtcMs);
    return output;
}

GroupMemberInfo decodeGroupMemberRecord(std::span<const std::byte> bytes)
{
    return decode<GroupMemberInfo>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.personId = reader.readUnsigned(wire); return true;
        case 2: value.displayName = reader.readString(wire, 255); return true;
        case 3: value.departmentName = reader.readString(wire, 255); return true;
        case 4: value.positionName = reader.readString(wire, 255); return true;
        case 5: value.avatarResourceId = reader.readString(wire, 512); return true;
        case 6: value.role = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 7: value.joinedAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

/** @brief 编码群共享文件摘要；实际文件正文仍通过独立下载授权接口获取。 */
std::vector<std::byte> encodeGroupFileRecord(const GroupFileInfo& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.assetUuid);
    writeString(output, 2, value.fileName);
    writeString(output, 3, value.mediaType);
    writeUnsigned(output, 4, value.sizeBytes);
    writeString(output, 5, value.ownerDisplayName);
    writeUnsigned(output, 6, value.createdAtUtcMs);
    return output;
}

GroupFileInfo decodeGroupFileRecord(std::span<const std::byte> bytes)
{
    return decode<GroupFileInfo>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.fileName = reader.readString(wire, 512); return true;
        case 3: value.mediaType = reader.readString(wire, 255); return true;
        case 4: value.sizeBytes = reader.readUnsigned(wire); return true;
        case 5: value.ownerDisplayName = reader.readString(wire, 255); return true;
        case 6: value.createdAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

/** @brief 编码通知摘要；列表负载只携带展示字段，业务扩展数据留在受权详情接口。 */
std::vector<std::byte> encodeNotificationSummaryRecord(const NotificationSummary& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.notificationId);
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.category));
    writeString(output, 3, value.title);
    writeString(output, 4, value.summary);
    writeString(output, 5, value.sourceName);
    writeUnsigned(output, 6, static_cast<std::uint32_t>(value.priority));
    writeUnsigned(output, 7, static_cast<std::uint32_t>(value.status));
    writeString(output, 8, value.actorDisplayName);
    writeUnsigned(output, 9, value.occurredAtUtcMs);
    return output;
}

/** @brief 解码通知摘要并限制所有可变文本，防止列表页被异常服务端负载无界放大。 */
NotificationSummary decodeNotificationSummaryRecord(std::span<const std::byte> bytes)
{
    return decode<NotificationSummary>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.notificationId = reader.readUnsigned(wire); return true;
        case 2: value.category = static_cast<NotificationCategory>(reader.readUnsigned(wire)); return true;
        case 3: value.title = reader.readString(wire, 255); return true;
        case 4: value.summary = reader.readString(wire, 1000); return true;
        case 5: value.sourceName = reader.readString(wire, 128); return true;
        case 6: value.priority = static_cast<NotificationPriority>(reader.readUnsigned(wire)); return true;
        case 7: value.status = static_cast<NotificationStatus>(reader.readUnsigned(wire)); return true;
        case 8: value.actorDisplayName = reader.readString(wire, 255); return true;
        case 9: value.occurredAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeNotificationFieldRecord(const NotificationDetailField& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.label);
    writeString(output, 2, value.value);
    writeBoolean(output, 3, value.emphasized);
    return output;
}

NotificationDetailField decodeNotificationFieldRecord(std::span<const std::byte> bytes)
{
    return decode<NotificationDetailField>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.label = reader.readString(wire, 128); return true;
        case 2: value.value = reader.readString(wire, 2048); return true;
        case 3: value.emphasized = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeNotificationAttachmentRecord(const NotificationAttachment& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.assetUuid);
    writeString(output, 2, value.fileName);
    writeString(output, 3, value.mediaType);
    writeUnsigned(output, 4, value.sizeBytes);
    return output;
}

NotificationAttachment decodeNotificationAttachmentRecord(std::span<const std::byte> bytes)
{
    return decode<NotificationAttachment>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.fileName = reader.readString(wire, 512); return true;
        case 3: value.mediaType = reader.readString(wire, 255); return true;
        case 4: value.sizeBytes = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

/** @brief 编码完整设置快照；字段只表示用户策略，服务端能力由独立状态投影给出。 */
std::vector<std::byte> encodeUserSettingsRecord(const UserSettingsProfile& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.revision);
    writeBoolean(output, 2, value.twoFactorEnabled);
    writeBoolean(output, 3, value.startupEnabled);
    writeBoolean(output, 4, value.autoLoginEnabled);
    writeUnsigned(output, 5, value.autoLockMinutes);
    writeBoolean(output, 6, value.chatWatermarkEnabled);
    writeBoolean(output, 7, value.screenshotProtectionEnabled);
    writeString(output, 8, value.downloadPath);
    writeString(output, 9, value.language);
    writeString(output, 10, value.theme);
    writeUnsigned(output, 11, value.phoneVisibility);
    writeUnsigned(output, 12, value.emailVisibility);
    writeUnsigned(output, 13, value.searchVisibility);
    writeBoolean(output, 14, value.phoneSearchEnabled);
    writeString(output, 15, value.profileSignature);
    writeBoolean(output, 16, value.newMessageNotificationEnabled);
    writeBoolean(output, 17, value.notificationSoundEnabled);
    writeString(output, 18, value.notificationSoundName);
    writeBoolean(output, 19, value.desktopPopupEnabled);
    writeBoolean(output, 20, value.unreadBadgeEnabled);
    writeBoolean(output, 21, value.mentionNotificationEnabled);
    writeUnsigned(output, 22, value.groupNotificationLevel);
    writeBoolean(output, 23, value.systemNotificationEnabled);
    writeBoolean(output, 24, value.approvalNotificationEnabled);
    writeBoolean(output, 25, value.fileNotificationEnabled);
    writeBoolean(output, 26, value.calendarNotificationEnabled);
    writeUnsigned(output, 27, value.calendarReminderMinutes);
    writeBoolean(output, 28, value.doNotDisturbEnabled);
    writeUnsigned(output, 29, value.doNotDisturbStartMinutes);
    writeUnsigned(output, 30, value.doNotDisturbEndMinutes);
    writeUnsigned(output, 31, value.notificationPreviewMode);
    writeBoolean(output, 32, value.readReceiptEnabled);
    writeBoolean(output, 33, value.enterToSendEnabled);
    writeUnsigned(output, 34, value.messageBubbleDensity);
    writeString(output, 35, value.primaryColor);
    writeString(output, 36, value.accentColor);
    writeUnsigned(output, 37, value.sidebarStyle);
    writeUnsigned(output, 38, value.cardRadiusMode);
    writeUnsigned(output, 39, value.uiDensity);
    writeUnsigned(output, 40, value.fontSizeMode);
    writeString(output, 41, value.chatBackground);
    writeUnsigned(output, 42, value.messageBubbleStyle);
    writeUnsigned(output, 43, value.contentViewMode);
    writeUnsigned(output, 44, value.windowTransparency);
    writeBoolean(output, 45, value.animationEnabled);
    writeUnsigned(output, 46, value.animationIntensity);
    writeBoolean(output, 47, value.autoSaveReceivedFiles);
    writeUnsigned(output, 48, value.recentFileRetentionDays);
    writeBoolean(output, 49, value.autoCacheCleanupEnabled);
    writeUnsigned(output, 50, value.cacheSizeLimitMb);
    writeUnsigned(output, 51, value.filePreviewMode);
    writeBoolean(output, 52, value.imageAutoCompressEnabled);
    writeUnsigned(output, 53, value.videoTranscodeMode);
    writeUnsigned(output, 54, value.fileEncryptionMode);
    writeUnsigned(output, 55, value.externalWatermarkMode);
    writeUnsigned(output, 56, value.defaultSharePermission);
    writeString(output, 57, value.syncFolderPath);
    writeBoolean(output, 58, value.echoCancellationEnabled);
    writeBoolean(output, 59, value.noiseSuppressionEnabled);
    writeBoolean(output, 60, value.autoGainControlEnabled);
    writeBoolean(output, 61, value.cameraMirrorEnabled);
    writeUnsigned(output, 62, value.videoResolutionMode);
    writeBoolean(output, 63, value.bandwidthOptimizationEnabled);
    writeBoolean(output, 64, value.recordingPermissionEnabled);
    writeUnsigned(output, 65, value.incomingCallWindowPosition);
    writeBoolean(output, 66, value.bluetoothPreferred);
    writeString(output, 67, value.callShortcut);
    return output;
}

/** @brief 解码用户设置并对路径和枚举文本做长度限制，避免配置响应造成无界内存占用。 */
UserSettingsProfile decodeUserSettingsRecord(std::span<const std::byte> bytes)
{
    return decode<UserSettingsProfile>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.revision = reader.readUnsigned(wire); return true;
        case 2: value.twoFactorEnabled = reader.readUnsigned(wire) != 0; return true;
        case 3: value.startupEnabled = reader.readUnsigned(wire) != 0; return true;
        case 4: value.autoLoginEnabled = reader.readUnsigned(wire) != 0; return true;
        case 5: value.autoLockMinutes = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 6: value.chatWatermarkEnabled = reader.readUnsigned(wire) != 0; return true;
        case 7: value.screenshotProtectionEnabled = reader.readUnsigned(wire) != 0; return true;
        case 8: value.downloadPath = reader.readString(wire, 1024); return true;
        case 9: value.language = reader.readString(wire, 32); return true;
        case 10: value.theme = reader.readString(wire, 32); return true;
        case 11: value.phoneVisibility = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 12: value.emailVisibility = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 13: value.searchVisibility = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 14: value.phoneSearchEnabled = reader.readUnsigned(wire) != 0; return true;
        case 15: value.profileSignature = reader.readString(wire, 160); return true;
        case 16: value.newMessageNotificationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 17: value.notificationSoundEnabled = reader.readUnsigned(wire) != 0; return true;
        case 18: value.notificationSoundName = reader.readString(wire, 64); return true;
        case 19: value.desktopPopupEnabled = reader.readUnsigned(wire) != 0; return true;
        case 20: value.unreadBadgeEnabled = reader.readUnsigned(wire) != 0; return true;
        case 21: value.mentionNotificationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 22: value.groupNotificationLevel = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 23: value.systemNotificationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 24: value.approvalNotificationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 25: value.fileNotificationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 26: value.calendarNotificationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 27: value.calendarReminderMinutes = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 28: value.doNotDisturbEnabled = reader.readUnsigned(wire) != 0; return true;
        case 29: value.doNotDisturbStartMinutes = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 30: value.doNotDisturbEndMinutes = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 31: value.notificationPreviewMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 32: value.readReceiptEnabled = reader.readUnsigned(wire) != 0; return true;
        case 33: value.enterToSendEnabled = reader.readUnsigned(wire) != 0; return true;
        case 34: value.messageBubbleDensity = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 35: value.primaryColor = reader.readString(wire, 16); return true;
        case 36: value.accentColor = reader.readString(wire, 16); return true;
        case 37: value.sidebarStyle = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 38: value.cardRadiusMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 39: value.uiDensity = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 40: value.fontSizeMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 41: value.chatBackground = reader.readString(wire, 64); return true;
        case 42: value.messageBubbleStyle = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 43: value.contentViewMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 44: value.windowTransparency = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 45: value.animationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 46: value.animationIntensity = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 47: value.autoSaveReceivedFiles = reader.readUnsigned(wire) != 0; return true;
        case 48: value.recentFileRetentionDays = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 49: value.autoCacheCleanupEnabled = reader.readUnsigned(wire) != 0; return true;
        case 50: value.cacheSizeLimitMb = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 51: value.filePreviewMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 52: value.imageAutoCompressEnabled = reader.readUnsigned(wire) != 0; return true;
        case 53: value.videoTranscodeMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 54: value.fileEncryptionMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 55: value.externalWatermarkMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 56: value.defaultSharePermission = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 57: value.syncFolderPath = reader.readString(wire, 1024); return true;
        case 58: value.echoCancellationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 59: value.noiseSuppressionEnabled = reader.readUnsigned(wire) != 0; return true;
        case 60: value.autoGainControlEnabled = reader.readUnsigned(wire) != 0; return true;
        case 61: value.cameraMirrorEnabled = reader.readUnsigned(wire) != 0; return true;
        case 62: value.videoResolutionMode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 63: value.bandwidthOptimizationEnabled = reader.readUnsigned(wire) != 0; return true;
        case 64: value.recordingPermissionEnabled = reader.readUnsigned(wire) != 0; return true;
        case 65: value.incomingCallWindowPosition = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 66: value.bluetoothPreferred = reader.readUnsigned(wire) != 0; return true;
        case 67: value.callShortcut = reader.readString(wire, 64); return true;
        default: return false;
        }
    });
}

/** @brief 编码设置页系统状态；仅发送聚合值和公开版本信息。 */
std::vector<std::byte> encodeSettingsSystemInfoRecord(const SettingsSystemInfo& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.deviceCount);
    writeUnsigned(output, 2, value.trustedDeviceCount);
    writeUnsigned(output, 3, value.storageUsedBytes);
    writeUnsigned(output, 4, value.storageQuotaBytes);
    writeBoolean(output, 5, value.intranetMode);
    writeBoolean(output, 6, value.endToEndEncryptionAvailable);
    writeString(output, 7, value.certificateStatus);
    writeString(output, 8, value.transportEncryption);
    writeString(output, 9, value.cryptoStatus);
    writeString(output, 10, value.productName);
    writeString(output, 11, value.currentVersion);
    writeString(output, 12, value.updateDate);
    writeString(output, 13, value.organizationName);
    writeString(output, 14, value.loginName);
    writeString(output, 15, value.accountStatusText);
    writeUnsigned(output, 16, value.lastLoginAtUtcMs);
    writeString(output, 17, value.lastLoginDeviceName);
    writeString(output, 18, value.lastLoginPlatform);
    writeString(output, 19, value.lastLoginSource);
    writeUnsigned(output, 20, value.teamMemberCount);
    writeUnsigned(output, 21, value.storageDocumentBytes);
    writeUnsigned(output, 22, value.storageImageBytes);
    writeUnsigned(output, 23, value.storageVideoBytes);
    writeUnsigned(output, 24, value.storageOtherBytes);
    writeUnsigned(output, 25, value.syncedFileCount);
    writeUnsigned(output, 26, value.lastFileSyncAtUtcMs);
    return output;
}

SettingsSystemInfo decodeSettingsSystemInfoRecord(std::span<const std::byte> bytes)
{
    return decode<SettingsSystemInfo>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.deviceCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 2: value.trustedDeviceCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.storageUsedBytes = reader.readUnsigned(wire); return true;
        case 4: value.storageQuotaBytes = reader.readUnsigned(wire); return true;
        case 5: value.intranetMode = reader.readUnsigned(wire) != 0; return true;
        case 6: value.endToEndEncryptionAvailable = reader.readUnsigned(wire) != 0; return true;
        case 7: value.certificateStatus = reader.readString(wire, 64); return true;
        case 8: value.transportEncryption = reader.readString(wire, 64); return true;
        case 9: value.cryptoStatus = reader.readString(wire, 64); return true;
        case 10: value.productName = reader.readString(wire, 128); return true;
        case 11: value.currentVersion = reader.readString(wire, 64); return true;
        case 12: value.updateDate = reader.readString(wire, 32); return true;
        case 13: value.organizationName = reader.readString(wire, 255); return true;
        case 14: value.loginName = reader.readString(wire, 128); return true;
        case 15: value.accountStatusText = reader.readString(wire, 64); return true;
        case 16: value.lastLoginAtUtcMs = reader.readUnsigned(wire); return true;
        case 17: value.lastLoginDeviceName = reader.readString(wire, 255); return true;
        case 18: value.lastLoginPlatform = reader.readString(wire, 64); return true;
        case 19: value.lastLoginSource = reader.readString(wire, 128); return true;
        case 20: value.teamMemberCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 21: value.storageDocumentBytes = reader.readUnsigned(wire); return true;
        case 22: value.storageImageBytes = reader.readUnsigned(wire); return true;
        case 23: value.storageVideoBytes = reader.readUnsigned(wire); return true;
        case 24: value.storageOtherBytes = reader.readUnsigned(wire); return true;
        case 25: value.syncedFileCount = reader.readUnsigned(wire); return true;
        case 26: value.lastFileSyncAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

/** @brief 编码联系人列表摘要；不包含手机号、邮箱等详情字段。 */
std::vector<std::byte> encodeContactSummaryRecord(const ContactSummary& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.personId);
    writeString(output, 2, value.displayName);
    writeString(output, 3, value.avatarResourceId);
    writeUnsigned(output, 4, value.presenceState);
    writeBoolean(output, 5, value.favorite);
    writeUnsigned(output, 6, value.lastInteractionAtUtcMs);
    writeUnsigned(output, 7, value.interactionCount);
    return output;
}

ContactSummary decodeContactSummaryRecord(std::span<const std::byte> bytes)
{
    return decode<ContactSummary>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.personId = reader.readUnsigned(wire); return true;
        case 2: value.displayName = reader.readString(wire, 255); return true;
        case 3: value.avatarResourceId = reader.readString(wire, 255); return true;
        case 4: value.presenceState = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 5: value.favorite = reader.readUnsigned(wire) != 0; return true;
        case 6: value.lastInteractionAtUtcMs = reader.readUnsigned(wire); return true;
        case 7: value.interactionCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeContactGroupRecord(const ContactGroupPreview& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.groupId);
    writeString(output, 2, value.name);
    writeUnsigned(output, 3, value.groupType);
    return output;
}

ContactGroupPreview decodeContactGroupRecord(std::span<const std::byte> bytes)
{
    return decode<ContactGroupPreview>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.groupId = reader.readUnsigned(wire); return true;
        case 2: value.name = reader.readString(wire, 255); return true;
        case 3: value.groupType = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

/** @brief 编码联系人详情及私有偏好；数组数量在入口再次受协议上限约束。 */
std::vector<std::byte> encodeContactDetailRecord(const ContactDetail& value)
{
    if (value.tags.size() > MaximumContactTagCount || value.groups.size() > MaximumContactGroupCount)
    {
        throw MessageCodecError("联系人详情数组超过协议上限");
    }
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.personId);
    writeString(output, 2, value.displayName);
    writeString(output, 3, value.avatarResourceId);
    writeString(output, 4, value.employeeNumber);
    writeString(output, 5, value.workPhone);
    writeString(output, 6, value.extensionNumber);
    writeString(output, 7, value.workEmail);
    writeString(output, 8, value.departmentName);
    writeString(output, 9, value.positionName);
    writeString(output, 10, value.officeLocation);
    writeUnsigned(output, 11, value.managerPersonId);
    writeString(output, 12, value.managerName);
    writeUnsigned(output, 13, value.presenceState);
    writeBoolean(output, 14, value.favorite);
    writeUnsigned(output, 15, value.revision);
    writeString(output, 16, value.note);
    for (const auto& tag : value.tags) writeString(output, 17, tag);
    for (const auto& group : value.groups) writeNested(output, 18, encodeContactGroupRecord(group));
    return output;
}

ContactDetail decodeContactDetailRecord(std::span<const std::byte> bytes)
{
    return decode<ContactDetail>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.personId = reader.readUnsigned(wire); return true;
        case 2: value.displayName = reader.readString(wire, 255); return true;
        case 3: value.avatarResourceId = reader.readString(wire, 255); return true;
        case 4: value.employeeNumber = reader.readString(wire, 64); return true;
        case 5: value.workPhone = reader.readString(wire, 64); return true;
        case 6: value.extensionNumber = reader.readString(wire, 32); return true;
        case 7: value.workEmail = reader.readString(wire, 255); return true;
        case 8: value.departmentName = reader.readString(wire, 255); return true;
        case 9: value.positionName = reader.readString(wire, 255); return true;
        case 10: value.officeLocation = reader.readString(wire, 255); return true;
        case 11: value.managerPersonId = reader.readUnsigned(wire); return true;
        case 12: value.managerName = reader.readString(wire, 255); return true;
        case 13: value.presenceState = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 14: value.favorite = reader.readUnsigned(wire) != 0; return true;
        case 15: value.revision = reader.readUnsigned(wire); return true;
        case 16: value.note = reader.readString(wire, 512); return true;
        case 17:
            if (value.tags.size() >= MaximumContactTagCount) throw MessageCodecError("联系人标签超过协议上限");
            value.tags.push_back(reader.readString(wire, 64)); return true;
        case 18:
            if (value.groups.size() >= MaximumContactGroupCount) throw MessageCodecError("联系人群组超过协议上限");
            value.groups.push_back(decodeContactGroupRecord(reader.readNested(wire, MaximumDirectoryRecordBytes)));
            return true;
        default: return false;
        }
    });
}

/** @brief 编码文件中心列表条目；对象存储键和内部表主键从不进入该投影。 */
std::vector<std::byte> encodeFileCenterItemRecord(const FileCenterItem& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.itemUuid);
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.kind));
    writeString(output, 3, value.name);
    writeString(output, 4, value.assetUuid);
    writeString(output, 5, value.mediaType);
    writeUnsigned(output, 6, static_cast<std::uint32_t>(value.category));
    writeUnsigned(output, 7, value.sizeBytes);
    writeUnsigned(output, 8, value.ownerPersonId);
    writeString(output, 9, value.ownerDisplayName);
    writeString(output, 10, value.location);
    writeUnsigned(output, 11, value.modifiedAtUtcMs);
    writeBoolean(output, 12, value.favorite);
    writeBoolean(output, 13, value.deleted);
    writeUnsigned(output, 14, value.sharedCount);
    writeUnsigned(output, 15, value.revision);
    writeUnsigned(output, 16, value.securityStatus);
    return output;
}

FileCenterItem decodeFileCenterItemRecord(std::span<const std::byte> bytes)
{
    return decode<FileCenterItem>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.itemUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.kind = static_cast<FileCenterItemKind>(reader.readUnsigned(wire)); return true;
        case 3: value.name = reader.readString(wire, 512); return true;
        case 4: value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 5: value.mediaType = reader.readString(wire, 255); return true;
        case 6: value.category = static_cast<FileMediaCategory>(reader.readUnsigned(wire)); return true;
        case 7: value.sizeBytes = reader.readUnsigned(wire); return true;
        case 8: value.ownerPersonId = reader.readUnsigned(wire); return true;
        case 9: value.ownerDisplayName = reader.readString(wire, 255); return true;
        case 10: value.location = reader.readString(wire, 1024); return true;
        case 11: value.modifiedAtUtcMs = reader.readUnsigned(wire); return true;
        case 12: value.favorite = reader.readUnsigned(wire) != 0; return true;
        case 13: value.deleted = reader.readUnsigned(wire) != 0; return true;
        case 14: value.sharedCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 15: value.revision = reader.readUnsigned(wire); return true;
        case 16: value.securityStatus = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeFileCenterVersionRecord(const FileCenterVersion& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.versionNumber);
    writeString(output, 2, value.assetUuid);
    writeUnsigned(output, 3, value.sizeBytes);
    writeString(output, 4, value.createdByDisplayName);
    writeUnsigned(output, 5, value.createdAtUtcMs);
    writeBoolean(output, 6, value.current);
    return output;
}

FileCenterVersion decodeFileCenterVersionRecord(std::span<const std::byte> bytes)
{
    return decode<FileCenterVersion>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.versionNumber = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 2: value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 3: value.sizeBytes = reader.readUnsigned(wire); return true;
        case 4: value.createdByDisplayName = reader.readString(wire, 255); return true;
        case 5: value.createdAtUtcMs = reader.readUnsigned(wire); return true;
        case 6: value.current = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeFileCenterPermissionRecord(const FileCenterPermission& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.personId);
    writeString(output, 2, value.displayName);
    writeUnsigned(output, 3, value.permission);
    return output;
}

FileCenterPermission decodeFileCenterPermissionRecord(std::span<const std::byte> bytes)
{
    return decode<FileCenterPermission>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.personId = reader.readUnsigned(wire); return true;
        case 2: value.displayName = reader.readString(wire, 255); return true;
        case 3: value.permission = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

/** @brief 编码文件详情并对版本、权限数组施加防御性上限。 */
std::vector<std::byte> encodeFileCenterDetailRecord(const FileCenterDetail& value)
{
    if (value.versions.size() > MaximumFileCenterVersionCount
        || value.permissions.size() > MaximumFileCenterPermissionCount)
    {
        throw MessageCodecError("文件详情数组超过协议上限");
    }
    std::vector<std::byte> output;
    writeNested(output, 1, encodeFileCenterItemRecord(value.item));
    writeUnsigned(output, 2, value.createdAtUtcMs);
    writeString(output, 3, value.sha256Hex);
    for (const auto& version : value.versions)
        writeNested(output, 4, encodeFileCenterVersionRecord(version));
    for (const auto& permission : value.permissions)
        writeNested(output, 5, encodeFileCenterPermissionRecord(permission));
    return output;
}

FileCenterDetail decodeFileCenterDetailRecord(std::span<const std::byte> bytes)
{
    return decode<FileCenterDetail>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.item = decodeFileCenterItemRecord(reader.readNested(wire, 16U * 1024U)); return true;
        case 2: value.createdAtUtcMs = reader.readUnsigned(wire); return true;
        case 3: value.sha256Hex = reader.readString(wire, 128); return true;
        case 4:
            if (value.versions.size() >= MaximumFileCenterVersionCount)
                throw MessageCodecError("文件版本数量超过协议上限");
            value.versions.push_back(decodeFileCenterVersionRecord(reader.readNested(wire, 8U * 1024U)));
            return true;
        case 5:
            if (value.permissions.size() >= MaximumFileCenterPermissionCount)
                throw MessageCodecError("文件权限数量超过协议上限");
            value.permissions.push_back(decodeFileCenterPermissionRecord(reader.readNested(wire, 8U * 1024U)));
            return true;
        default: return false;
        }
    });
}

/** @brief 编码单个日程参与人；只输出服务端裁剪后的展示资料和响应状态。 */
std::vector<std::byte> encodeCalendarParticipantRecord(const CalendarParticipant& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.personId);
    writeString(output, 2, value.displayName);
    writeString(output, 3, value.avatarResourceId);
    writeUnsigned(output, 4, static_cast<std::uint32_t>(value.status));
    return output;
}

CalendarParticipant decodeCalendarParticipantRecord(std::span<const std::byte> bytes)
{
    return decode<CalendarParticipant>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.personId = reader.readUnsigned(wire); return true;
        case 2: value.displayName = reader.readString(wire, 255); return true;
        case 3: value.avatarResourceId = reader.readString(wire, 512); return true;
        case 4:
            value.status = static_cast<CalendarParticipationStatus>(reader.readUnsigned(wire));
            return true;
        default: return false;
        }
    });
}

/** @brief 编码完整日程；参与人数量有界，避免列表嵌套集合导致负载失控。 */
std::vector<std::byte> encodeCalendarEventRecord(const CalendarEvent& value)
{
    if (value.participants.size() > MaximumCalendarParticipantCount)
        throw MessageCodecError("日程参与人超过协议上限");
    std::vector<std::byte> output;
    writeString(output, 1, value.eventUuid);
    writeString(output, 2, value.title);
    writeString(output, 3, value.description);
    writeString(output, 4, value.location);
    writeString(output, 5, value.calendarName);
    writeUnsigned(output, 6, static_cast<std::uint32_t>(value.kind));
    writeString(output, 7, value.color);
    writeUnsigned(output, 8, value.organizerPersonId);
    writeString(output, 9, value.organizerDisplayName);
    writeUnsigned(output, 10, value.startsAtUtcMs);
    writeUnsigned(output, 11, value.endsAtUtcMs);
    writeBoolean(output, 12, value.allDay);
    writeBoolean(output, 13, value.cancelled);
    writeString(output, 14, value.meetingNumber);
    writeUnsigned(output, 15, value.reminderMinutes);
    writeUnsigned(output, 16, value.revision);
    writeBoolean(output, 17, value.editable);
    for (const auto& participant : value.participants)
        writeNested(output, 18, encodeCalendarParticipantRecord(participant));
    return output;
}

CalendarEvent decodeCalendarEventRecord(std::span<const std::byte> bytes)
{
    return decode<CalendarEvent>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.eventUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.title = reader.readString(wire, 255); return true;
        case 3: value.description = reader.readString(wire, 2048); return true;
        case 4: value.location = reader.readString(wire, 512); return true;
        case 5: value.calendarName = reader.readString(wire, 128); return true;
        case 6: value.kind = static_cast<CalendarKind>(reader.readUnsigned(wire)); return true;
        case 7: value.color = reader.readString(wire, 16); return true;
        case 8: value.organizerPersonId = reader.readUnsigned(wire); return true;
        case 9: value.organizerDisplayName = reader.readString(wire, 255); return true;
        case 10: value.startsAtUtcMs = reader.readUnsigned(wire); return true;
        case 11: value.endsAtUtcMs = reader.readUnsigned(wire); return true;
        case 12: value.allDay = reader.readUnsigned(wire) != 0; return true;
        case 13: value.cancelled = reader.readUnsigned(wire) != 0; return true;
        case 14: value.meetingNumber = reader.readString(wire, 64); return true;
        case 15: value.reminderMinutes = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 16: value.revision = reader.readUnsigned(wire); return true;
        case 17: value.editable = reader.readUnsigned(wire) != 0; return true;
        case 18:
            if (value.participants.size() >= MaximumCalendarParticipantCount)
                throw MessageCodecError("日程参与人超过协议上限");
            value.participants.push_back(decodeCalendarParticipantRecord(
                reader.readNested(wire, MaximumDirectoryRecordBytes)));
            return true;
        default: return false;
        }
    });
}

} // namespace

std::vector<std::byte> encodeMessage(const LoginRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.loginName);
    writeString(output, 2, value.password);
    writeString(output, 3, value.deviceUuid);
    writeString(output, 4, value.deviceName);
    writeString(output, 5, value.platform);
    return output;
}

std::vector<std::byte> encodeMessage(const LoginResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.accountId);
    writeUnsigned(output, 5, value.personId);
    writeUnsigned(output, 6, value.deviceId);
    writeUnsigned(output, 7, value.sessionId);
    writeString(output, 8, value.displayName);
    return output;
}

std::vector<std::byte> encodeMessage(const HeartbeatPing& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.continuousReceivedSequence);
    return output;
}

std::vector<std::byte> encodeMessage(const HeartbeatPong& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.serverTimeUtcMs);
    return output;
}

std::vector<std::byte> encodeMessage(const DirectConversationRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.peerPersonId);
    return output;
}

std::vector<std::byte> encodeMessage(const DirectConversationResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.conversationId);
    writeUnsigned(output, 5, value.peerPersonId);
    return output;
}

std::vector<std::byte> encodeMessage(const SendMessageRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.conversationId);
    writeString(output, 2, value.clientMessageId);
    writeUnsigned(output, 3, value.kind);
    writeString(output, 4, value.content);
    return output;
}

std::vector<std::byte> encodeMessage(const SendMessageResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeString(output, 4, value.clientMessageId);
    writeString(output, 5, value.serverMessageId);
    writeUnsigned(output, 6, value.conversationId);
    writeUnsigned(output, 7, value.conversationSequence);
    writeUnsigned(output, 8, value.acceptedAtUtcMs);
    return output;
}

std::vector<std::byte> encodeMessage(const DirectMessagePush& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.serverMessageId);
    writeString(output, 2, value.clientMessageId);
    writeUnsigned(output, 3, value.conversationId);
    writeUnsigned(output, 4, value.conversationSequence);
    writeUnsigned(output, 5, value.senderPersonId);
    writeUnsigned(output, 6, value.recipientPersonId);
    writeUnsigned(output, 7, value.kind);
    writeString(output, 8, value.content);
    writeUnsigned(output, 9, value.createdAtUtcMs);
    return output;
}

/** @brief 编码会话列表中的单条摘要；内部对象键和账号字段不属于该投影。 */
std::vector<std::byte> encodeConversationSummaryRecord(const ConversationSummary& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.conversationId);
    writeUnsigned(output, 2, value.peerPersonId);
    writeString(output, 3, value.displayName);
    writeString(output, 4, value.lastMessagePreview);
    writeUnsigned(output, 5, value.lastActivityUtcMs);
    writeUnsigned(output, 6, value.unreadCount);
    writeBoolean(output, 7, value.pinned);
    writeBoolean(output, 8, value.muted);
    writeUnsigned(output, 9, value.lastMessageSequence);
    writeUnsigned(output, 10, value.lastReadSequence);
    return output;
}

/** @brief 解码单条会话摘要并限制所有展示文本，防止异常服务端扩大客户端内存。 */
ConversationSummary decodeConversationSummaryRecord(std::span<const std::byte> bytes)
{
    return decode<ConversationSummary>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.conversationId = reader.readUnsigned(wire); return true;
        case 2: value.peerPersonId = reader.readUnsigned(wire); return true;
        case 3: value.displayName = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.lastMessagePreview = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 5: value.lastActivityUtcMs = reader.readUnsigned(wire); return true;
        case 6: value.unreadCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 7: value.pinned = reader.readUnsigned(wire) != 0; return true;
        case 8: value.muted = reader.readUnsigned(wire) != 0; return true;
        case 9: value.lastMessageSequence = reader.readUnsigned(wire); return true;
        case 10: value.lastReadSequence = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

std::vector<std::byte> encodeMessage(const MessageHistoryRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.conversationId);
    writeUnsigned(output, 2, value.beforeSequence);
    writeUnsigned(output, 3, value.limit);
    return output;
}

std::vector<std::byte> encodeMessage(const MessageHistoryResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.conversationId);
    for (const auto& message : value.messages)
    {
        writeNested(output, 5, encodeMessage(message));
    }
    writeBoolean(output, 6, value.hasMore);
    return output;
}

std::vector<std::byte> encodeMessage(const ConversationListRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.limit);
    return output;
}

std::vector<std::byte> encodeMessage(const ConversationListResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    for (const auto& item : value.conversations)
    {
        writeNested(output, 4, encodeConversationSummaryRecord(item));
    }
    return output;
}

std::vector<std::byte> encodeMessage(const ConversationPreferenceRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.conversationId);
    writeBoolean(output, 2, value.pinned);
    writeBoolean(output, 3, value.muted);
    return output;
}

std::vector<std::byte> encodeMessage(const ConversationPreferenceResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.conversationId);
    writeBoolean(output, 5, value.pinned);
    writeBoolean(output, 6, value.muted);
    return output;
}

std::vector<std::byte> encodeMessage(const GroupListRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.filter);
    writeString(output, 2, value.searchText);
    writeUnsigned(output, 3, value.limit);
    return output;
}

std::vector<std::byte> encodeMessage(const GroupListResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    for (const auto& group : value.groups)
    {
        writeNested(output, 4, encodeGroupSummaryRecord(group));
    }
    writeUnsigned(output, 5, value.totalCount);
    writeUnsigned(output, 6, value.managedCount);
    writeUnsigned(output, 7, value.activeTodayCount);
    writeUnsigned(output, 8, value.unreadCount);
    return output;
}

std::vector<std::byte> encodeMessage(const GroupDetailRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.groupId);
    return output;
}

std::vector<std::byte> encodeMessage(const GroupDetailResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeGroupSummaryRecord(value.group));
    writeString(output, 5, value.ownerDisplayName);
    writeString(output, 6, value.announcement);
    writeUnsigned(output, 7, value.createdAtUtcMs);
    for (const auto& member : value.members)
    {
        writeNested(output, 8, encodeGroupMemberRecord(member));
    }
    for (const auto& file : value.files)
    {
        writeNested(output, 9, encodeGroupFileRecord(file));
    }
    return output;
}

std::vector<std::byte> encodeMessage(const GroupCreateRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.name);
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.type));
    writeString(output, 3, value.announcement);
    for (const auto& tag : value.tags)
    {
        writeString(output, 4, tag);
    }
    for (const auto personId : value.memberPersonIds)
    {
        writeUnsigned(output, 5, personId);
    }
    return output;
}

std::vector<std::byte> encodeMessage(const GroupCreateResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeGroupSummaryRecord(value.group));
    return output;
}

std::vector<std::byte> encodeMessage(const GroupJoinRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.groupCode);
    return output;
}

std::vector<std::byte> encodeMessage(const GroupJoinResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeGroupSummaryRecord(value.group));
    return output;
}

std::vector<std::byte> encodeMessage(const GroupMemberUpdateRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.groupId);
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.action));
    for (const auto personId : value.personIds)
    {
        writeUnsigned(output, 3, personId);
    }
    return output;
}

std::vector<std::byte> encodeMessage(const GroupMemberUpdateResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.groupId);
    writeUnsigned(output, 5, value.updatedCount);
    for (const auto& member : value.members)
    {
        writeNested(output, 6, encodeGroupMemberRecord(member));
    }
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationListRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, static_cast<std::uint32_t>(value.category));
    writeBoolean(output, 2, value.unreadOnly);
    writeString(output, 3, value.searchText);
    writeUnsigned(output, 4, value.offset);
    writeUnsigned(output, 5, value.limit);
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationListResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    for (const auto& notification : value.notifications)
        writeNested(output, 4, encodeNotificationSummaryRecord(notification));
    writeUnsigned(output, 5, value.totalCount);
    writeUnsigned(output, 6, value.unreadCount);
    writeUnsigned(output, 7, value.approvalCount);
    writeUnsigned(output, 8, value.systemCount);
    writeUnsigned(output, 9, value.securityCount);
    writeUnsigned(output, 10, value.mentionCount);
    writeUnsigned(output, 11, value.fileCount);
    writeUnsigned(output, 12, value.taskCount);
    writeUnsigned(output, 13, value.otherCount);
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationDetailRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.notificationId);
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationDetailResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeNotificationSummaryRecord(value.notification));
    writeString(output, 5, value.businessReference);
    writeString(output, 6, value.explanation);
    for (const auto& field : value.fields)
        writeNested(output, 7, encodeNotificationFieldRecord(field));
    for (const auto& attachment : value.attachments)
        writeNested(output, 8, encodeNotificationAttachmentRecord(attachment));
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationStatusRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.notificationId);
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.action));
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationStatusResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.notificationId);
    writeUnsigned(output, 5, static_cast<std::uint32_t>(value.status));
    writeUnsigned(output, 6, value.unreadCount);
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationMarkAllReadRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, static_cast<std::uint32_t>(value.category));
    return output;
}

std::vector<std::byte> encodeMessage(const NotificationMarkAllReadResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.updatedCount);
    writeUnsigned(output, 5, value.unreadCount);
    return output;
}

std::vector<std::byte> encodeMessage(const SettingsGetRequest&)
{
    return {};
}

std::vector<std::byte> encodeMessage(const SettingsGetResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeUserSettingsRecord(value.settings));
    writeNested(output, 5, encodeSettingsSystemInfoRecord(value.systemInfo));
    return output;
}

std::vector<std::byte> encodeMessage(const SettingsUpdateRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.expectedRevision);
    writeNested(output, 2, encodeUserSettingsRecord(value.settings));
    return output;
}

std::vector<std::byte> encodeMessage(const SettingsUpdateResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeUserSettingsRecord(value.settings));
    return output;
}

std::vector<std::byte> encodeMessage(const SettingsResetRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.expectedRevision);
    return output;
}

std::vector<std::byte> encodeMessage(const SettingsResetResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeUserSettingsRecord(value.settings));
    return output;
}

std::vector<std::byte> encodeMessage(const ContactCenterRequest&)
{
    return {};
}

std::vector<std::byte> encodeMessage(const ContactCenterResponse& value)
{
    if (value.recentContacts.size() > MaximumContactSummaryCount
        || value.favoriteContacts.size() > MaximumContactSummaryCount)
    {
        throw MessageCodecError("联系人摘要超过协议上限");
    }
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    for (const auto& item : value.recentContacts) writeNested(output, 4, encodeContactSummaryRecord(item));
    for (const auto& item : value.favoriteContacts) writeNested(output, 5, encodeContactSummaryRecord(item));
    return output;
}

std::vector<std::byte> encodeMessage(const ContactDetailRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.contactPersonId);
    return output;
}

std::vector<std::byte> encodeMessage(const ContactDetailResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeContactDetailRecord(value.detail));
    return output;
}

std::vector<std::byte> encodeMessage(const ContactPreferenceUpdateRequest& value)
{
    if (value.tags.size() > MaximumContactTagCount) throw MessageCodecError("联系人标签超过协议上限");
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.contactPersonId);
    writeUnsigned(output, 2, value.expectedRevision);
    writeBoolean(output, 3, value.favorite);
    writeString(output, 4, value.note);
    for (const auto& tag : value.tags) writeString(output, 5, tag);
    return output;
}

std::vector<std::byte> encodeMessage(const ContactPreferenceUpdateResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeContactDetailRecord(value.detail));
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterListRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, static_cast<std::uint32_t>(value.scope));
    writeUnsigned(output, 2, static_cast<std::uint32_t>(value.category));
    writeString(output, 3, value.searchText);
    writeUnsigned(output, 4, value.offset);
    writeUnsigned(output, 5, value.limit);
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterListResponse& value)
{
    if (value.items.size() > MaximumFileCenterItemCount)
        throw MessageCodecError("文件中心列表超过协议上限");
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    for (const auto& item : value.items) writeNested(output, 4, encodeFileCenterItemRecord(item));
    writeUnsigned(output, 5, value.totalCount);
    writeUnsigned(output, 6, value.usedBytes);
    writeUnsigned(output, 7, value.quotaBytes);
    writeUnsigned(output, 8, value.documentBytes);
    writeUnsigned(output, 9, value.imageBytes);
    writeUnsigned(output, 10, value.videoBytes);
    writeUnsigned(output, 11, value.otherBytes);
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterDetailRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.itemUuid);
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterDetailResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeFileCenterDetailRecord(value.detail));
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterFolderCreateRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.parentFolderUuid);
    writeString(output, 2, value.name);
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterFolderCreateResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeFileCenterItemRecord(value.folder));
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterUpdateRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.documentUuid);
    writeUnsigned(output, 2, value.expectedRevision);
    writeUnsigned(output, 3, static_cast<std::uint32_t>(value.action));
    writeBoolean(output, 4, value.desiredFavorite);
    writeString(output, 5, value.value);
    writeUnsigned(output, 6, value.targetPersonId);
    writeUnsigned(output, 7, value.permission);
    return output;
}

std::vector<std::byte> encodeMessage(const FileCenterUpdateResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeFileCenterDetailRecord(value.detail));
    return output;
}

std::vector<std::byte> encodeMessage(const CalendarListRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.rangeStartUtcMs);
    writeUnsigned(output, 2, value.rangeEndUtcMs);
    writeBoolean(output, 3, value.includeCancelled);
    writeBoolean(output, 4, value.remindersOnly);
    return output;
}

std::vector<std::byte> encodeMessage(const CalendarListResponse& value)
{
    if (value.events.size() > MaximumCalendarEventCount)
        throw MessageCodecError("日程列表超过协议上限");
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    for (const auto& event : value.events)
        writeNested(output, 4, encodeCalendarEventRecord(event));
    return output;
}

std::vector<std::byte> encodeMessage(const CalendarCreateRequest& value)
{
    if (value.participantLoginNames.size() > MaximumCalendarParticipantCount)
        throw MessageCodecError("日程参与人超过协议上限");
    std::vector<std::byte> output;
    writeString(output, 1, value.title);
    writeString(output, 2, value.description);
    writeString(output, 3, value.location);
    writeString(output, 4, value.calendarName);
    writeUnsigned(output, 5, static_cast<std::uint32_t>(value.kind));
    writeString(output, 6, value.color);
    writeUnsigned(output, 7, value.startsAtUtcMs);
    writeUnsigned(output, 8, value.endsAtUtcMs);
    writeBoolean(output, 9, value.allDay);
    writeBoolean(output, 10, value.conferenceEnabled);
    writeUnsigned(output, 11, value.reminderMinutes);
    for (const auto& login : value.participantLoginNames) writeString(output, 12, login);
    return output;
}

std::vector<std::byte> encodeMessage(const CalendarUpdateRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.eventUuid);
    writeUnsigned(output, 2, value.expectedRevision);
    writeNested(output, 3, encodeMessage(value.event));
    return output;
}

std::vector<std::byte> encodeMessage(const CalendarDeleteRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.eventUuid);
    writeUnsigned(output, 2, value.expectedRevision);
    return output;
}

std::vector<std::byte> encodeMessage(const CalendarMutationResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeNested(output, 4, encodeCalendarEventRecord(value.event));
    return output;
}

std::vector<std::byte> encodeMessage(const FileUploadRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.conversationId);
    writeString(output, 2, value.clientMessageId);
    writeString(output, 3, value.fileName);
    writeString(output, 4, value.mediaType);
    writeString(output, 5, value.sha256Hex);
    writeString(output, 6, value.content);
    return output;
}

std::vector<std::byte> encodeMessage(const FileUploadResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeString(output, 4, value.clientMessageId);
    writeString(output, 5, value.assetUuid);
    writeString(output, 6, value.serverMessageId);
    writeUnsigned(output, 7, value.conversationId);
    writeUnsigned(output, 8, value.conversationSequence);
    writeUnsigned(output, 9, value.acceptedAtUtcMs);
    return output;
}

std::vector<std::byte> encodeMessage(const FileDownloadRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.assetUuid);
    return output;
}

std::vector<std::byte> encodeMessage(const FileDownloadResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeString(output, 4, value.assetUuid);
    writeString(output, 5, value.fileName);
    writeString(output, 6, value.mediaType);
    writeString(output, 7, value.sha256Hex);
    writeUnsigned(output, 8, value.sizeBytes);
    writeString(output, 9, value.content);
    return output;
}

std::vector<std::byte> encodeMessage(const ConferenceJoinRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.conversationId);
    writeBoolean(output, 2, value.videoEnabled);
    return output;
}

std::vector<std::byte> encodeMessage(const ConferenceJoinResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeString(output, 4, value.conferenceUuid);
    writeString(output, 5, value.roomName);
    writeString(output, 6, value.serverUrl);
    writeString(output, 7, value.webUrl);
    writeString(output, 8, value.participantToken);
    writeUnsigned(output, 9, value.expiresAtUtcMs);
    writeBoolean(output, 10, value.videoEnabled);
    return output;
}

std::vector<std::byte> encodeMessage(const ConferenceLeaveRequest& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.conferenceUuid);
    return output;
}

std::vector<std::byte> encodeMessage(const ConferenceLeaveResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeString(output, 4, value.conferenceUuid);
    return output;
}

std::vector<std::byte> encodeMessage(const DeliveryReceipt& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.serverMessageId);
    writeUnsigned(output, 2, value.conversationId);
    writeUnsigned(output, 3, value.continuousDeliveredSequence);
    writeUnsigned(output, 4, value.recipientPersonId);
    return output;
}

std::vector<std::byte> encodeMessage(const ReadReceipt& value)
{
    std::vector<std::byte> output;
    writeString(output, 1, value.serverMessageId);
    writeUnsigned(output, 2, value.conversationId);
    writeUnsigned(output, 3, value.continuousReadSequence);
    writeUnsigned(output, 4, value.readerPersonId);
    return output;
}

std::vector<std::byte> encodeMessage(const DirectorySnapshotRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.knownRevision);
    return output;
}

std::vector<std::byte> encodeMessage(const DirectorySnapshotResponse& value)
{
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.revision);
    for (const auto& record : value.organizations)
    {
        const auto nested = encodeDirectoryOrganization(record);
        writeNested(output, 10, nested);
    }
    for (const auto& record : value.departments)
    {
        const auto nested = encodeDirectoryDepartment(record);
        writeNested(output, 11, nested);
    }
    for (const auto& record : value.positions)
    {
        const auto nested = encodeDirectoryPosition(record);
        writeNested(output, 12, nested);
    }
    for (const auto& record : value.people)
    {
        const auto nested = encodeDirectoryPerson(record);
        writeNested(output, 13, nested);
    }
    for (const auto& record : value.assignments)
    {
        const auto nested = encodeDirectoryAssignment(record);
        writeNested(output, 14, nested);
    }
    return output;
}

std::vector<std::byte> encodeMessage(const DirectoryDeltaRequest& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.fromRevisionExclusive);
    return output;
}

std::vector<std::byte> encodeMessage(const DirectoryDeltaResponse& value)
{
    if (value.changes.size() > MaximumDirectoryDeltaCount)
    {
        throw MessageCodecError("目录增量事件数量超过单批上限");
    }
    std::vector<std::byte> output;
    writeBoolean(output, 1, value.success);
    writeUnsigned(output, 2, value.errorCode);
    writeString(output, 3, value.errorMessage);
    writeUnsigned(output, 4, value.fromRevision);
    writeUnsigned(output, 5, value.currentRevision);
    writeBoolean(output, 6, value.fullSnapshotRequired);
    for (const auto& change : value.changes)
    {
        writeNested(output, 10, encodeDirectoryChange(change));
    }
    return output;
}

std::vector<std::byte> encodeMessage(const ErrorResponse& value)
{
    std::vector<std::byte> output;
    writeUnsigned(output, 1, value.code);
    writeString(output, 2, value.message);
    writeUnsigned(output, 3, value.failedRequestId);
    return output;
}

LoginRequest decodeLoginRequest(std::span<const std::byte> bytes)
{
    return decode<LoginRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.loginName = reader.readString(wire, MaximumLoginNameBytes); return true;
        case 2: value.password = reader.readString(wire, MaximumPasswordBytes); return true;
        case 3: value.deviceUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 4: value.deviceName = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 5: value.platform = reader.readString(wire, 64); return true;
        default: return false;
        }
    });
}

LoginResponse decodeLoginResponse(std::span<const std::byte> bytes)
{
    return decode<LoginResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.accountId = reader.readUnsigned(wire); return true;
        case 5: value.personId = reader.readUnsigned(wire); return true;
        case 6: value.deviceId = reader.readUnsigned(wire); return true;
        case 7: value.sessionId = reader.readUnsigned(wire); return true;
        case 8: value.displayName = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        default: return false;
        }
    });
}

HeartbeatPing decodeHeartbeatPing(std::span<const std::byte> bytes)
{
    return decode<HeartbeatPing>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.continuousReceivedSequence = reader.readUnsigned(wire); return true; }
        return false;
    });
}

HeartbeatPong decodeHeartbeatPong(std::span<const std::byte> bytes)
{
    return decode<HeartbeatPong>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.serverTimeUtcMs = reader.readUnsigned(wire); return true; }
        return false;
    });
}

DirectConversationRequest decodeDirectConversationRequest(std::span<const std::byte> bytes)
{
    return decode<DirectConversationRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.peerPersonId = reader.readUnsigned(wire); return true; }
        return false;
    });
}

DirectConversationResponse decodeDirectConversationResponse(std::span<const std::byte> bytes)
{
    return decode<DirectConversationResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.conversationId = reader.readUnsigned(wire); return true;
        case 5: value.peerPersonId = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

SendMessageRequest decodeSendMessageRequest(std::span<const std::byte> bytes)
{
    return decode<SendMessageRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.conversationId = reader.readUnsigned(wire); return true;
        case 2: value.clientMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 3: value.kind = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 4: value.content = reader.readString(wire, MaximumMessageContentBytes); return true;
        default: return false;
        }
    });
}

SendMessageResponse decodeSendMessageResponse(std::span<const std::byte> bytes)
{
    return decode<SendMessageResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.clientMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 5: value.serverMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 6: value.conversationId = reader.readUnsigned(wire); return true;
        case 7: value.conversationSequence = reader.readUnsigned(wire); return true;
        case 8: value.acceptedAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

DirectMessagePush decodeDirectMessagePush(std::span<const std::byte> bytes)
{
    return decode<DirectMessagePush>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.serverMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.clientMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 3: value.conversationId = reader.readUnsigned(wire); return true;
        case 4: value.conversationSequence = reader.readUnsigned(wire); return true;
        case 5: value.senderPersonId = reader.readUnsigned(wire); return true;
        case 6: value.recipientPersonId = reader.readUnsigned(wire); return true;
        case 7: value.kind = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 8: value.content = reader.readString(wire, MaximumMessageContentBytes); return true;
        case 9: value.createdAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

MessageHistoryRequest decodeMessageHistoryRequest(std::span<const std::byte> bytes)
{
    return decode<MessageHistoryRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.conversationId = reader.readUnsigned(wire); return true;
        case 2: value.beforeSequence = reader.readUnsigned(wire); return true;
        case 3: value.limit = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

MessageHistoryResponse decodeMessageHistoryResponse(std::span<const std::byte> bytes)
{
    return decode<MessageHistoryResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.conversationId = reader.readUnsigned(wire); return true;
        case 5:
            if (value.messages.size() >= MaximumHistoryCount)
                throw MessageCodecError("历史消息数量超过上限");
            value.messages.push_back(decodeDirectMessagePush(
                reader.readNested(wire, MaximumMessageContentBytes + 2048U)));
            return true;
        case 6: value.hasMore = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

ConversationListRequest decodeConversationListRequest(std::span<const std::byte> bytes)
{
    return decode<ConversationListRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.limit = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true; }
        return false;
    });
}

ConversationListResponse decodeConversationListResponse(std::span<const std::byte> bytes)
{
    return decode<ConversationListResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4:
            if (value.conversations.size() >= MaximumConversationCount)
                throw MessageCodecError("会话摘要数量超过上限");
            value.conversations.push_back(decodeConversationSummaryRecord(
                reader.readNested(wire, MaximumDirectoryRecordBytes)));
            return true;
        default: return false;
        }
    });
}

ConversationPreferenceRequest decodeConversationPreferenceRequest(std::span<const std::byte> bytes)
{
    return decode<ConversationPreferenceRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.conversationId = reader.readUnsigned(wire); return true;
        case 2: value.pinned = reader.readUnsigned(wire) != 0; return true;
        case 3: value.muted = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

ConversationPreferenceResponse decodeConversationPreferenceResponse(std::span<const std::byte> bytes)
{
    return decode<ConversationPreferenceResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.conversationId = reader.readUnsigned(wire); return true;
        case 5: value.pinned = reader.readUnsigned(wire) != 0; return true;
        case 6: value.muted = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

GroupListRequest decodeGroupListRequest(std::span<const std::byte> bytes)
{
    return decode<GroupListRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.filter = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 2: value.searchText = reader.readString(wire, 255); return true;
        case 3: value.limit = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

GroupListResponse decodeGroupListResponse(std::span<const std::byte> bytes)
{
    return decode<GroupListResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4:
            if (value.groups.size() >= MaximumGroupCount)
                throw MessageCodecError("群组列表数量超过上限");
            value.groups.push_back(decodeGroupSummaryRecord(reader.readNested(wire, 16U * 1024U)));
            return true;
        case 5: value.totalCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 6: value.managedCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 7: value.activeTodayCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 8: value.unreadCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

GroupDetailRequest decodeGroupDetailRequest(std::span<const std::byte> bytes)
{
    return decode<GroupDetailRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.groupId = reader.readUnsigned(wire); return true; }
        return false;
    });
}

GroupDetailResponse decodeGroupDetailResponse(std::span<const std::byte> bytes)
{
    return decode<GroupDetailResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.group = decodeGroupSummaryRecord(reader.readNested(wire, 16U * 1024U)); return true;
        case 5: value.ownerDisplayName = reader.readString(wire, 255); return true;
        case 6: value.announcement = reader.readString(wire, 4096); return true;
        case 7: value.createdAtUtcMs = reader.readUnsigned(wire); return true;
        case 8:
            if (value.members.size() >= MaximumGroupMemberCount)
                throw MessageCodecError("群成员数量超过上限");
            value.members.push_back(decodeGroupMemberRecord(reader.readNested(wire, 16U * 1024U)));
            return true;
        case 9:
            if (value.files.size() >= MaximumGroupFileCount)
                throw MessageCodecError("群共享文件数量超过上限");
            value.files.push_back(decodeGroupFileRecord(reader.readNested(wire, 16U * 1024U)));
            return true;
        default: return false;
        }
    });
}

GroupCreateRequest decodeGroupCreateRequest(std::span<const std::byte> bytes)
{
    return decode<GroupCreateRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.name = reader.readString(wire, 255); return true;
        case 2: value.type = static_cast<GroupType>(reader.readUnsigned(wire)); return true;
        case 3: value.announcement = reader.readString(wire, 4096); return true;
        case 4:
            if (value.tags.size() >= MaximumGroupTagCount)
                throw MessageCodecError("群组标签数量超过上限");
            value.tags.push_back(reader.readString(wire, 64));
            return true;
        case 5:
            if (value.memberPersonIds.size() >= MaximumGroupMemberCount)
                throw MessageCodecError("初始群成员数量超过上限");
            value.memberPersonIds.push_back(reader.readUnsigned(wire));
            return true;
        default: return false;
        }
    });
}

GroupCreateResponse decodeGroupCreateResponse(std::span<const std::byte> bytes)
{
    return decode<GroupCreateResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.group = decodeGroupSummaryRecord(reader.readNested(wire, 16U * 1024U)); return true;
        default: return false;
        }
    });
}

GroupJoinRequest decodeGroupJoinRequest(std::span<const std::byte> bytes)
{
    return decode<GroupJoinRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.groupCode = reader.readString(wire, 32); return true; }
        return false;
    });
}

GroupJoinResponse decodeGroupJoinResponse(std::span<const std::byte> bytes)
{
    return decode<GroupJoinResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.group = decodeGroupSummaryRecord(reader.readNested(wire, 16U * 1024U)); return true;
        default: return false;
        }
    });
}

GroupMemberUpdateRequest decodeGroupMemberUpdateRequest(std::span<const std::byte> bytes)
{
    return decode<GroupMemberUpdateRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.groupId = reader.readUnsigned(wire); return true;
        case 2: value.action = static_cast<GroupMemberAction>(reader.readUnsigned(wire)); return true;
        case 3:
            if (value.personIds.size() >= MaximumGroupMemberCount)
                throw MessageCodecError("成员变更数量超过上限");
            value.personIds.push_back(reader.readUnsigned(wire));
            return true;
        default: return false;
        }
    });
}

GroupMemberUpdateResponse decodeGroupMemberUpdateResponse(std::span<const std::byte> bytes)
{
    return decode<GroupMemberUpdateResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.groupId = reader.readUnsigned(wire); return true;
        case 5: value.updatedCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 6:
            if (value.members.size() >= MaximumGroupMemberCount)
                throw MessageCodecError("群成员数量超过上限");
            value.members.push_back(decodeGroupMemberRecord(reader.readNested(wire, 16U * 1024U)));
            return true;
        default: return false;
        }
    });
}

NotificationListRequest decodeNotificationListRequest(std::span<const std::byte> bytes)
{
    return decode<NotificationListRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.category = static_cast<NotificationCategory>(reader.readUnsigned(wire)); return true;
        case 2: value.unreadOnly = reader.readUnsigned(wire) != 0; return true;
        case 3: value.searchText = reader.readString(wire, 255); return true;
        case 4: value.offset = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 5: value.limit = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

NotificationListResponse decodeNotificationListResponse(std::span<const std::byte> bytes)
{
    return decode<NotificationListResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4:
            if (value.notifications.size() >= MaximumNotificationCount)
                throw MessageCodecError("通知列表数量超过上限");
            value.notifications.push_back(decodeNotificationSummaryRecord(reader.readNested(wire, 16U * 1024U)));
            return true;
        case 5: value.totalCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 6: value.unreadCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 7: value.approvalCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 8: value.systemCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 9: value.securityCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 10: value.mentionCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 11: value.fileCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 12: value.taskCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 13: value.otherCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

NotificationDetailRequest decodeNotificationDetailRequest(std::span<const std::byte> bytes)
{
    return decode<NotificationDetailRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.notificationId = reader.readUnsigned(wire); return true; }
        return false;
    });
}

NotificationDetailResponse decodeNotificationDetailResponse(std::span<const std::byte> bytes)
{
    return decode<NotificationDetailResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.notification = decodeNotificationSummaryRecord(reader.readNested(wire, 16U * 1024U)); return true;
        case 5: value.businessReference = reader.readString(wire, 255); return true;
        case 6: value.explanation = reader.readString(wire, 4096); return true;
        case 7:
            if (value.fields.size() >= MaximumNotificationFieldCount)
                throw MessageCodecError("通知详情字段数量超过上限");
            value.fields.push_back(decodeNotificationFieldRecord(reader.readNested(wire, 8U * 1024U)));
            return true;
        case 8:
            if (value.attachments.size() >= MaximumNotificationAttachmentCount)
                throw MessageCodecError("通知附件数量超过上限");
            value.attachments.push_back(decodeNotificationAttachmentRecord(reader.readNested(wire, 8U * 1024U)));
            return true;
        default: return false;
        }
    });
}

NotificationStatusRequest decodeNotificationStatusRequest(std::span<const std::byte> bytes)
{
    return decode<NotificationStatusRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.notificationId = reader.readUnsigned(wire); return true; }
        if (field == 2) { value.action = static_cast<NotificationAction>(reader.readUnsigned(wire)); return true; }
        return false;
    });
}

NotificationStatusResponse decodeNotificationStatusResponse(std::span<const std::byte> bytes)
{
    return decode<NotificationStatusResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.notificationId = reader.readUnsigned(wire); return true;
        case 5: value.status = static_cast<NotificationStatus>(reader.readUnsigned(wire)); return true;
        case 6: value.unreadCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

NotificationMarkAllReadRequest decodeNotificationMarkAllReadRequest(std::span<const std::byte> bytes)
{
    return decode<NotificationMarkAllReadRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.category = static_cast<NotificationCategory>(reader.readUnsigned(wire)); return true; }
        return false;
    });
}

NotificationMarkAllReadResponse decodeNotificationMarkAllReadResponse(std::span<const std::byte> bytes)
{
    return decode<NotificationMarkAllReadResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.updatedCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 5: value.unreadCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
}

SettingsGetRequest decodeSettingsGetRequest(std::span<const std::byte> bytes)
{
    return decode<SettingsGetRequest>(bytes, [](auto&, Reader&, auto, auto) { return false; });
}

SettingsGetResponse decodeSettingsGetResponse(std::span<const std::byte> bytes)
{
    return decode<SettingsGetResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.settings = decodeUserSettingsRecord(reader.readNested(wire, 8U * 1024U)); return true;
        case 5: value.systemInfo = decodeSettingsSystemInfoRecord(reader.readNested(wire, 8U * 1024U)); return true;
        default: return false;
        }
    });
}

SettingsUpdateRequest decodeSettingsUpdateRequest(std::span<const std::byte> bytes)
{
    return decode<SettingsUpdateRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.expectedRevision = reader.readUnsigned(wire); return true; }
        if (field == 2) { value.settings = decodeUserSettingsRecord(reader.readNested(wire, 8U * 1024U)); return true; }
        return false;
    });
}

SettingsUpdateResponse decodeSettingsUpdateResponse(std::span<const std::byte> bytes)
{
    return decode<SettingsUpdateResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.settings = decodeUserSettingsRecord(reader.readNested(wire, 8U * 1024U)); return true;
        default: return false;
        }
    });
}

SettingsResetRequest decodeSettingsResetRequest(std::span<const std::byte> bytes)
{
    return decode<SettingsResetRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.expectedRevision = reader.readUnsigned(wire); return true; }
        return false;
    });
}

SettingsResetResponse decodeSettingsResetResponse(std::span<const std::byte> bytes)
{
    return decode<SettingsResetResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.settings = decodeUserSettingsRecord(reader.readNested(wire, 8U * 1024U)); return true;
        default: return false;
        }
    });
}

ContactCenterRequest decodeContactCenterRequest(std::span<const std::byte> bytes)
{
    return decode<ContactCenterRequest>(bytes, [](auto&, Reader&, auto, auto) { return false; });
}

ContactCenterResponse decodeContactCenterResponse(std::span<const std::byte> bytes)
{
    return decode<ContactCenterResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4:
            if (value.recentContacts.size() >= MaximumContactSummaryCount) throw MessageCodecError("最近联系人超过协议上限");
            value.recentContacts.push_back(decodeContactSummaryRecord(reader.readNested(wire, MaximumDirectoryRecordBytes)));
            return true;
        case 5:
            if (value.favoriteContacts.size() >= MaximumContactSummaryCount) throw MessageCodecError("收藏联系人超过协议上限");
            value.favoriteContacts.push_back(decodeContactSummaryRecord(reader.readNested(wire, MaximumDirectoryRecordBytes)));
            return true;
        default: return false;
        }
    });
}

ContactDetailRequest decodeContactDetailRequest(std::span<const std::byte> bytes)
{
    return decode<ContactDetailRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.contactPersonId = reader.readUnsigned(wire); return true; }
        return false;
    });
}

ContactDetailResponse decodeContactDetailResponse(std::span<const std::byte> bytes)
{
    return decode<ContactDetailResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.detail = decodeContactDetailRecord(reader.readNested(wire, 64U * 1024U)); return true;
        default: return false;
        }
    });
}

ContactPreferenceUpdateRequest decodeContactPreferenceUpdateRequest(std::span<const std::byte> bytes)
{
    return decode<ContactPreferenceUpdateRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.contactPersonId = reader.readUnsigned(wire); return true;
        case 2: value.expectedRevision = reader.readUnsigned(wire); return true;
        case 3: value.favorite = reader.readUnsigned(wire) != 0; return true;
        case 4: value.note = reader.readString(wire, 512); return true;
        case 5:
            if (value.tags.size() >= MaximumContactTagCount) throw MessageCodecError("联系人标签超过协议上限");
            value.tags.push_back(reader.readString(wire, 64)); return true;
        default: return false;
        }
    });
}

ContactPreferenceUpdateResponse decodeContactPreferenceUpdateResponse(std::span<const std::byte> bytes)
{
    return decode<ContactPreferenceUpdateResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.detail = decodeContactDetailRecord(reader.readNested(wire, 64U * 1024U)); return true;
        default: return false;
        }
    });
}

FileCenterListRequest decodeFileCenterListRequest(std::span<const std::byte> bytes)
{
    auto value = decode<FileCenterListRequest>(bytes, [](auto& item, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: item.scope = static_cast<FileCenterScope>(reader.readUnsigned(wire)); return true;
        case 2: item.category = static_cast<FileMediaCategory>(reader.readUnsigned(wire)); return true;
        case 3: item.searchText = reader.readString(wire, 255); return true;
        case 4: item.offset = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 5: item.limit = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
    if (static_cast<std::uint32_t>(value.scope) > static_cast<std::uint32_t>(FileCenterScope::RecycleBin)
        || static_cast<std::uint32_t>(value.category) > static_cast<std::uint32_t>(FileMediaCategory::Other))
    {
        throw MessageCodecError("文件中心筛选值无效");
    }
    return value;
}

FileCenterListResponse decodeFileCenterListResponse(std::span<const std::byte> bytes)
{
    return decode<FileCenterListResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4:
            if (value.items.size() >= MaximumFileCenterItemCount)
                throw MessageCodecError("文件中心列表超过协议上限");
            value.items.push_back(decodeFileCenterItemRecord(reader.readNested(wire, 16U * 1024U)));
            return true;
        case 5: value.totalCount = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 6: value.usedBytes = reader.readUnsigned(wire); return true;
        case 7: value.quotaBytes = reader.readUnsigned(wire); return true;
        case 8: value.documentBytes = reader.readUnsigned(wire); return true;
        case 9: value.imageBytes = reader.readUnsigned(wire); return true;
        case 10: value.videoBytes = reader.readUnsigned(wire); return true;
        case 11: value.otherBytes = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

FileCenterDetailRequest decodeFileCenterDetailRequest(std::span<const std::byte> bytes)
{
    return decode<FileCenterDetailRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.itemUuid = reader.readString(wire, MaximumIdentifierBytes); return true; }
        return false;
    });
}

FileCenterDetailResponse decodeFileCenterDetailResponse(std::span<const std::byte> bytes)
{
    return decode<FileCenterDetailResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.detail = decodeFileCenterDetailRecord(reader.readNested(wire, 128U * 1024U)); return true;
        default: return false;
        }
    });
}

FileCenterFolderCreateRequest decodeFileCenterFolderCreateRequest(std::span<const std::byte> bytes)
{
    return decode<FileCenterFolderCreateRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.parentFolderUuid = reader.readString(wire, MaximumIdentifierBytes); return true; }
        if (field == 2) { value.name = reader.readString(wire, 255); return true; }
        return false;
    });
}

FileCenterFolderCreateResponse decodeFileCenterFolderCreateResponse(std::span<const std::byte> bytes)
{
    return decode<FileCenterFolderCreateResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.folder = decodeFileCenterItemRecord(reader.readNested(wire, 16U * 1024U)); return true;
        default: return false;
        }
    });
}

FileCenterUpdateRequest decodeFileCenterUpdateRequest(std::span<const std::byte> bytes)
{
    auto value = decode<FileCenterUpdateRequest>(bytes, [](auto& item, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: item.documentUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: item.expectedRevision = reader.readUnsigned(wire); return true;
        case 3: item.action = static_cast<FileCenterAction>(reader.readUnsigned(wire)); return true;
        case 4: item.desiredFavorite = reader.readUnsigned(wire) != 0; return true;
        case 5: item.value = reader.readString(wire, 512); return true;
        case 6: item.targetPersonId = reader.readUnsigned(wire); return true;
        case 7: item.permission = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        default: return false;
        }
    });
    const auto action = static_cast<std::uint32_t>(value.action);
    if (action < static_cast<std::uint32_t>(FileCenterAction::SetFavorite)
        || action > static_cast<std::uint32_t>(FileCenterAction::RevokePerson))
    {
        throw MessageCodecError("文件中心动作无效");
    }
    return value;
}

FileCenterUpdateResponse decodeFileCenterUpdateResponse(std::span<const std::byte> bytes)
{
    return decode<FileCenterUpdateResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.detail = decodeFileCenterDetailRecord(reader.readNested(wire, 128U * 1024U)); return true;
        default: return false;
        }
    });
}

CalendarListRequest decodeCalendarListRequest(std::span<const std::byte> bytes)
{
    auto value = decode<CalendarListRequest>(bytes, [](auto& item, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: item.rangeStartUtcMs = reader.readUnsigned(wire); return true;
        case 2: item.rangeEndUtcMs = reader.readUnsigned(wire); return true;
        case 3: item.includeCancelled = reader.readUnsigned(wire) != 0; return true;
        case 4: item.remindersOnly = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
    constexpr std::uint64_t MaximumCalendarRangeMs = 366ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
    if (value.rangeStartUtcMs == 0 || value.rangeEndUtcMs <= value.rangeStartUtcMs
        || value.rangeEndUtcMs - value.rangeStartUtcMs > MaximumCalendarRangeMs)
        throw MessageCodecError("日程查询时间范围无效");
    return value;
}

CalendarListResponse decodeCalendarListResponse(std::span<const std::byte> bytes)
{
    return decode<CalendarListResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4:
            if (value.events.size() >= MaximumCalendarEventCount)
                throw MessageCodecError("日程列表超过协议上限");
            value.events.push_back(decodeCalendarEventRecord(reader.readNested(wire, 64U * 1024U)));
            return true;
        default: return false;
        }
    });
}

CalendarCreateRequest decodeCalendarCreateRequest(std::span<const std::byte> bytes)
{
    auto value = decode<CalendarCreateRequest>(bytes, [](auto& item, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: item.title = reader.readString(wire, 255); return true;
        case 2: item.description = reader.readString(wire, 2048); return true;
        case 3: item.location = reader.readString(wire, 512); return true;
        case 4: item.calendarName = reader.readString(wire, 128); return true;
        case 5: item.kind = static_cast<CalendarKind>(reader.readUnsigned(wire)); return true;
        case 6: item.color = reader.readString(wire, 16); return true;
        case 7: item.startsAtUtcMs = reader.readUnsigned(wire); return true;
        case 8: item.endsAtUtcMs = reader.readUnsigned(wire); return true;
        case 9: item.allDay = reader.readUnsigned(wire) != 0; return true;
        case 10: item.conferenceEnabled = reader.readUnsigned(wire) != 0; return true;
        case 11: item.reminderMinutes = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 12:
            if (item.participantLoginNames.size() >= MaximumCalendarParticipantCount)
                throw MessageCodecError("日程参与人超过协议上限");
            item.participantLoginNames.push_back(reader.readString(wire, MaximumLoginNameBytes));
            return true;
        default: return false;
        }
    });
    const auto kind = static_cast<std::uint32_t>(value.kind);
    if (kind < static_cast<std::uint32_t>(CalendarKind::Personal)
        || kind > static_cast<std::uint32_t>(CalendarKind::Shared))
        throw MessageCodecError("日历类型无效");
    return value;
}

CalendarUpdateRequest decodeCalendarUpdateRequest(std::span<const std::byte> bytes)
{
    return decode<CalendarUpdateRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.eventUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.expectedRevision = reader.readUnsigned(wire); return true;
        case 3: value.event = decodeCalendarCreateRequest(reader.readNested(wire, 64U * 1024U)); return true;
        default: return false;
        }
    });
}

CalendarDeleteRequest decodeCalendarDeleteRequest(std::span<const std::byte> bytes)
{
    return decode<CalendarDeleteRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.eventUuid = reader.readString(wire, MaximumIdentifierBytes); return true; }
        if (field == 2) { value.expectedRevision = reader.readUnsigned(wire); return true; }
        return false;
    });
}

CalendarMutationResponse decodeCalendarMutationResponse(std::span<const std::byte> bytes)
{
    return decode<CalendarMutationResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.event = decodeCalendarEventRecord(reader.readNested(wire, 64U * 1024U)); return true;
        default: return false;
        }
    });
}

FileUploadRequest decodeFileUploadRequest(std::span<const std::byte> bytes)
{
    return decode<FileUploadRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.conversationId = reader.readUnsigned(wire); return true;
        case 2: value.clientMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 3: value.fileName = reader.readString(wire, 512); return true;
        case 4: value.mediaType = reader.readString(wire, 255); return true;
        case 5: value.sha256Hex = reader.readString(wire, 64); return true;
        case 6: value.content = reader.readString(wire, MaximumFileContentBytes); return true;
        default: return false;
        }
    });
}

FileUploadResponse decodeFileUploadResponse(std::span<const std::byte> bytes)
{
    return decode<FileUploadResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.clientMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 5: value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 6: value.serverMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 7: value.conversationId = reader.readUnsigned(wire); return true;
        case 8: value.conversationSequence = reader.readUnsigned(wire); return true;
        case 9: value.acceptedAtUtcMs = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

FileDownloadRequest decodeFileDownloadRequest(std::span<const std::byte> bytes)
{
    return decode<FileDownloadRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true; }
        return false;
    });
}

FileDownloadResponse decodeFileDownloadResponse(std::span<const std::byte> bytes)
{
    return decode<FileDownloadResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.assetUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 5: value.fileName = reader.readString(wire, 512); return true;
        case 6: value.mediaType = reader.readString(wire, 255); return true;
        case 7: value.sha256Hex = reader.readString(wire, 64); return true;
        case 8: value.sizeBytes = reader.readUnsigned(wire); return true;
        case 9: value.content = reader.readString(wire, MaximumFileContentBytes); return true;
        default: return false;
        }
    });
}

ConferenceJoinRequest decodeConferenceJoinRequest(std::span<const std::byte> bytes)
{
    return decode<ConferenceJoinRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.conversationId = reader.readUnsigned(wire); return true; }
        if (field == 2) { value.videoEnabled = reader.readUnsigned(wire) != 0; return true; }
        return false;
    });
}

ConferenceJoinResponse decodeConferenceJoinResponse(std::span<const std::byte> bytes)
{
    return decode<ConferenceJoinResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.conferenceUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 5: value.roomName = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 6: value.serverUrl = reader.readString(wire, 2048); return true;
        case 7: value.webUrl = reader.readString(wire, 2048); return true;
        case 8: value.participantToken = reader.readString(wire, 16U * 1024U); return true;
        case 9: value.expiresAtUtcMs = reader.readUnsigned(wire); return true;
        case 10: value.videoEnabled = reader.readUnsigned(wire) != 0; return true;
        default: return false;
        }
    });
}

ConferenceLeaveRequest decodeConferenceLeaveRequest(std::span<const std::byte> bytes)
{
    return decode<ConferenceLeaveRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.conferenceUuid = reader.readString(wire, MaximumIdentifierBytes); return true; }
        return false;
    });
}

ConferenceLeaveResponse decodeConferenceLeaveResponse(std::span<const std::byte> bytes)
{
    return decode<ConferenceLeaveResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.conferenceUuid = reader.readString(wire, MaximumIdentifierBytes); return true;
        default: return false;
        }
    });
}

DeliveryReceipt decodeDeliveryReceipt(std::span<const std::byte> bytes)
{
    return decode<DeliveryReceipt>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.serverMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.conversationId = reader.readUnsigned(wire); return true;
        case 3: value.continuousDeliveredSequence = reader.readUnsigned(wire); return true;
        case 4: value.recipientPersonId = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

ReadReceipt decodeReadReceipt(std::span<const std::byte> bytes)
{
    return decode<ReadReceipt>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.serverMessageId = reader.readString(wire, MaximumIdentifierBytes); return true;
        case 2: value.conversationId = reader.readUnsigned(wire); return true;
        case 3: value.continuousReadSequence = reader.readUnsigned(wire); return true;
        case 4: value.readerPersonId = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

DirectorySnapshotRequest decodeDirectorySnapshotRequest(std::span<const std::byte> bytes)
{
    return decode<DirectorySnapshotRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.knownRevision = reader.readUnsigned(wire); return true; }
        return false;
    });
}

DirectorySnapshotResponse decodeDirectorySnapshotResponse(std::span<const std::byte> bytes)
{
    return decode<DirectorySnapshotResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.success = reader.readUnsigned(wire) != 0; return true;
        case 2: value.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: value.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: value.revision = reader.readUnsigned(wire); return true;
        case 10:
        {
            const auto nested = reader.readNested(wire, MaximumDirectoryRecordBytes);
            appendDirectoryRecord(value.organizations, decodeDirectoryOrganization(nested));
            return true;
        }
        case 11:
        {
            const auto nested = reader.readNested(wire, MaximumDirectoryRecordBytes);
            appendDirectoryRecord(value.departments, decodeDirectoryDepartment(nested));
            return true;
        }
        case 12:
        {
            const auto nested = reader.readNested(wire, MaximumDirectoryRecordBytes);
            appendDirectoryRecord(value.positions, decodeDirectoryPosition(nested));
            return true;
        }
        case 13:
        {
            const auto nested = reader.readNested(wire, MaximumDirectoryRecordBytes);
            appendDirectoryRecord(value.people, decodeDirectoryPerson(nested));
            return true;
        }
        case 14:
        {
            const auto nested = reader.readNested(wire, MaximumDirectoryRecordBytes);
            appendDirectoryRecord(value.assignments, decodeDirectoryAssignment(nested));
            return true;
        }
        default: return false;
        }
    });
}

DirectoryDeltaRequest decodeDirectoryDeltaRequest(std::span<const std::byte> bytes)
{
    return decode<DirectoryDeltaRequest>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        if (field == 1) { value.fromRevisionExclusive = reader.readUnsigned(wire); return true; }
        return false;
    });
}

DirectoryDeltaResponse decodeDirectoryDeltaResponse(std::span<const std::byte> bytes)
{
    auto value = decode<DirectoryDeltaResponse>(bytes, [](auto& item, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: item.success = reader.readUnsigned(wire) != 0; return true;
        case 2: item.errorCode = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 3: item.errorMessage = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 4: item.fromRevision = reader.readUnsigned(wire); return true;
        case 5: item.currentRevision = reader.readUnsigned(wire); return true;
        case 6: item.fullSnapshotRequired = reader.readUnsigned(wire) != 0; return true;
        case 10:
            if (item.changes.size() >= MaximumDirectoryDeltaCount)
            {
                throw MessageCodecError("目录增量事件数量超过单批上限");
            }
            item.changes.push_back(decodeDirectoryChange(
                reader.readNested(wire, MaximumDirectoryRecordBytes)));
            return true;
        default: return false;
        }
    });
    if (value.success && (value.currentRevision == 0
        || (!value.fullSnapshotRequired && value.fromRevision > value.currentRevision)))
    {
        throw MessageCodecError("目录增量修订范围无效");
    }
    if (value.fullSnapshotRequired && !value.changes.empty())
    {
        throw MessageCodecError("要求全量同步的响应不得携带局部事件");
    }
    return value;
}

ErrorResponse decodeErrorResponse(std::span<const std::byte> bytes)
{
    return decode<ErrorResponse>(bytes, [](auto& value, Reader& reader, auto field, auto wire) {
        switch (field)
        {
        case 1: value.code = static_cast<std::uint32_t>(reader.readUnsigned(wire)); return true;
        case 2: value.message = reader.readString(wire, MaximumFriendlyTextBytes); return true;
        case 3: value.failedRequestId = reader.readUnsigned(wire); return true;
        default: return false;
        }
    });
}

} // namespace orglink::protocol
