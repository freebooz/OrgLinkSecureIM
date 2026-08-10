import { HttpClient, HttpParams } from '@angular/common/http';
import { inject, Injectable } from '@angular/core';
import {
  AdminOverview,
  DepartmentRecord,
  FileRecord,
  OrganizationTreeResponse,
  PageResponse,
  PersonRecord
} from './admin.models';

/** @brief 强类型管理 API 适配器，组件不得自行拼接 REST 地址或处理 Cookie。 */
@Injectable({ providedIn: 'root' })
export class AdminApiService {
  private readonly http = inject(HttpClient);

  overview() {
    return this.http.get<AdminOverview>('/api/admin/overview');
  }

  organizationTree() {
    return this.http.get<OrganizationTreeResponse>('/api/admin/organizations/tree');
  }

  persons(departmentId: string, search: string, page = 1, pageSize = 20) {
    const params = new HttpParams()
      .set('departmentId', departmentId || '0')
      .set('search', search)
      .set('page', page)
      .set('pageSize', pageSize);
    return this.http.get<PageResponse<PersonRecord>>('/api/admin/persons', { params });
  }

  files(search: string, page = 1, pageSize = 20) {
    const params = new HttpParams().set('search', search).set('page', page).set('pageSize', pageSize);
    return this.http.get<PageResponse<FileRecord>>('/api/admin/files', { params });
  }

  createDepartment(input: Partial<DepartmentRecord>) {
    return this.http.post<DepartmentRecord>('/api/admin/departments', input);
  }

  updateDepartment(input: DepartmentRecord) {
    return this.http.patch<DepartmentRecord>(`/api/admin/departments/${input.id}`, input);
  }

  createPerson(input: Record<string, unknown>) {
    return this.http.post<PersonRecord>('/api/admin/persons', input);
  }

  updatePerson(input: PersonRecord) {
    return this.http.patch<PersonRecord>(`/api/admin/persons/${input.id}`, input);
  }

  resetPassword(personId: string, temporaryPassword: string) {
    return this.http.post(`/api/admin/persons/${personId}/reset-password`, { temporaryPassword });
  }

  revokeFileShares(uuid: string) {
    return this.http.post(`/api/admin/files/${uuid}/revoke-shares`, {});
  }

  deleteFile(uuid: string) {
    return this.http.delete(`/api/admin/files/${uuid}`);
  }
}
