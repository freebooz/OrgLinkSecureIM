/** 登录成功后保存在标签页会话中的非敏感管理员信息和 CSRF 令牌。 */
export interface AdminSession {
  accountId: string;
  personId: string;
  organizationId: string;
  displayName: string;
  role: 'super_admin' | 'org_admin';
  csrfToken: string;
}

/** 首页服务端聚合指标。 */
export interface AdminOverview {
  organizations: number;
  departments: number;
  people: number;
  onlinePeople: number;
  sharedFiles: number;
  storageBytes: number;
  recentActivity: AuditActivity[];
}

export interface AuditActivity {
  action: string;
  targetType: string;
  targetId: string;
  resultCode: string;
  occurredAtUtcMs: number;
}

/** 组织和部门均带修订号，写入时必须回传以执行乐观锁。 */
export interface OrganizationRecord {
  id: string;
  parentId: string | null;
  code: string;
  name: string;
  revision: string;
  enabled: boolean;
}

export interface DepartmentRecord {
  id: string;
  organizationId: string;
  parentId: string | null;
  code: string;
  name: string;
  shortName: string;
  sortOrder: number;
  revision: string;
  enabled: boolean;
  personCount: number;
}

export interface OrganizationTreeResponse {
  organizations: OrganizationRecord[];
  departments: DepartmentRecord[];
  positions: PositionRecord[];
}

export interface PositionRecord {
  id: string;
  code: string;
  name: string;
  sortOrder: number;
}

/** 人员与账号分开显示，presence 只使用服务端实时状态。 */
export interface PersonRecord {
  id: string;
  employeeNumber: string;
  displayName: string;
  avatarResourceId: string;
  workPhone: string;
  extensionNumber: string;
  workEmail: string;
  departmentId: string;
  departmentName: string;
  positionId: string | null;
  positionName: string;
  revision: string;
  enabled: boolean;
  loginName: string;
  presence: 'online' | 'offline';
}

export interface FileRecord {
  uuid: string;
  name: string;
  mediaType: string;
  sizeBytes: string;
  ownerName: string;
  ownerEmployeeNumber: string;
  revision: string;
  deleted: boolean;
  updatedAtUtcMs: number;
  activeShareCount: number;
  revokedShareCount: number;
}

export interface PageResponse<T> {
  items: T[];
  total: number;
  page: number;
  pageSize: number;
}

/** API 失败响应不携带数据库细节，页面只展示友好 message。 */
export interface ApiErrorBody {
  error: string;
  message: string;
}
