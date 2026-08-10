import { HttpErrorResponse } from '@angular/common/http';
import { Component, computed, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { AdminApiService } from '../../core/admin-api.service';
import {
  ApiErrorBody,
  DepartmentRecord,
  OrganizationTreeResponse,
  PageResponse,
  PersonRecord
} from '../../core/admin.models';
import { AppIconComponent } from '../../shared/app-icon.component';

interface DepartmentView extends DepartmentRecord { depth: number; }
type DialogKind = 'department' | 'person' | 'password' | null;

/** @brief 组织机构与人员账号统一管理页，组织树选择会直接约束右侧人员查询范围。 */
@Component({
  selector: 'app-organization-page',
  imports: [FormsModule, AppIconComponent],
  templateUrl: './organization.page.html',
  styleUrl: './organization.page.scss'
})
export class OrganizationPage {
  private readonly api = inject(AdminApiService);
  readonly loading = signal(false);
  readonly errorMessage = signal('');
  readonly toastMessage = signal('');
  readonly tree = signal<OrganizationTreeResponse>({ organizations: [], departments: [], positions: [] });
  readonly peoplePage = signal<PageResponse<PersonRecord>>({ items: [], total: 0, page: 1, pageSize: 20 });
  readonly selectedDepartmentId = signal('0');
  readonly searchText = signal('');
  readonly dialog = signal<DialogKind>(null);
  readonly editingPerson = signal<PersonRecord | null>(null);
  readonly editingDepartment = signal<DepartmentRecord | null>(null);
  readonly passwordPerson = signal<PersonRecord | null>(null);
  temporaryPassword = '';
  departmentDraft = this.emptyDepartment();
  personDraft = this.emptyPerson();

  readonly selectedDepartment = computed(() =>
    this.tree().departments.find((item) => item.id === this.selectedDepartmentId()) ?? null);
  readonly departmentViews = computed(() => this.flattenDepartments(this.tree().departments));
  readonly pageCount = computed(() => Math.max(1, Math.ceil(this.peoplePage().total / this.peoplePage().pageSize)));

  constructor() {
    this.loadTree();
  }

  /** 重新读取组织树，并保留仍然有效的部门选择。 */
  loadTree(): void {
    this.loading.set(true);
    this.api.organizationTree().subscribe({
      next: (tree) => {
        this.tree.set(tree);
        if (this.selectedDepartmentId() !== '0'
            && !tree.departments.some((item) => item.id === this.selectedDepartmentId())) {
          this.selectedDepartmentId.set('0');
        }
        this.loadPeople(1);
      },
      error: (error: HttpErrorResponse) => this.handleError(error, '组织架构加载失败')
    });
  }

  /** 切换部门后从第一页查询，避免复用上一部门已经越界的页码。 */
  selectDepartment(id: string): void {
    this.selectedDepartmentId.set(id);
    this.loadPeople(1);
  }

  /** 查询人员列表；在线状态完全采用服务端 presence 结果。 */
  loadPeople(page = this.peoplePage().page): void {
    this.loading.set(true);
    this.api.persons(this.selectedDepartmentId(), this.searchText(), page, this.peoplePage().pageSize).subscribe({
      next: (peoplePage) => { this.peoplePage.set(peoplePage); this.loading.set(false); },
      error: (error: HttpErrorResponse) => this.handleError(error, '人员列表加载失败')
    });
  }

  openCreateDepartment(): void {
    this.editingDepartment.set(null);
    this.departmentDraft = this.emptyDepartment();
    this.departmentDraft.parentId = this.selectedDepartmentId() === '0' ? null : this.selectedDepartmentId();
    this.dialog.set('department');
  }

  openEditDepartment(): void {
    const department = this.selectedDepartment();
    if (!department) return;
    this.editingDepartment.set(department);
    this.departmentDraft = { ...department };
    this.dialog.set('department');
  }

  openCreatePerson(): void {
    this.editingPerson.set(null);
    this.personDraft = this.emptyPerson();
    this.personDraft.departmentId = this.selectedDepartmentId() === '0' ? '' : this.selectedDepartmentId();
    this.temporaryPassword = '';
    this.dialog.set('person');
  }

  openEditPerson(person: PersonRecord): void {
    this.editingPerson.set(person);
    this.personDraft = { ...person };
    this.dialog.set('person');
  }

  openPassword(person: PersonRecord): void {
    this.passwordPerson.set(person);
    this.temporaryPassword = '';
    this.dialog.set('password');
  }

  closeDialog(): void {
    this.dialog.set(null);
    this.passwordPerson.set(null);
  }

  /** 创建或按修订号更新部门；冲突时保留窗口并提示管理员刷新。 */
  saveDepartment(): void {
    if (!this.departmentDraft.code.trim() || !this.departmentDraft.name.trim()) {
      this.errorMessage.set('请填写部门编码和名称');
      return;
    }
    const request = this.editingDepartment()
      ? this.api.updateDepartment(this.departmentDraft as DepartmentRecord)
      : this.api.createDepartment(this.departmentDraft);
    this.loading.set(true);
    request.subscribe({
      next: () => { this.closeDialog(); this.showToast('部门信息已保存'); this.loadTree(); },
      error: (error: HttpErrorResponse) => this.handleError(error, '部门保存失败')
    });
  }

  /** 创建或更新人员；账号名仅在创建时提交，避免把账号名与姓名混为同一字段。 */
  savePerson(): void {
    if (!this.personDraft.displayName.trim() || !this.personDraft.departmentId) {
      this.errorMessage.set('请填写姓名并选择部门');
      return;
    }
    const existing = this.editingPerson();
    const request = existing
      ? this.api.updatePerson(this.personDraft as PersonRecord)
      : this.api.createPerson({ ...this.personDraft, temporaryPassword: this.temporaryPassword });
    this.loading.set(true);
    request.subscribe({
      next: () => { this.closeDialog(); this.showToast(existing ? '人员资料已更新' : '人员与账号已创建'); this.loadTree(); },
      error: (error: HttpErrorResponse) => this.handleError(error, '人员保存失败')
    });
  }

  /** 重置口令会撤销目标账号已有客户端和管理端会话。 */
  savePassword(): void {
    const person = this.passwordPerson();
    if (!person || this.temporaryPassword.length < 12) {
      this.errorMessage.set('临时密码至少需要 12 位');
      return;
    }
    this.loading.set(true);
    this.api.resetPassword(person.id, this.temporaryPassword).subscribe({
      next: () => { this.closeDialog(); this.showToast('临时密码已更新，目标账号会话已撤销'); this.loading.set(false); },
      error: (error: HttpErrorResponse) => this.handleError(error, '密码重置失败')
    });
  }

  initials(name: string): string { return name.slice(0, 1) || '人'; }

  private flattenDepartments(departments: DepartmentRecord[]): DepartmentView[] {
    const children = new Map<string, DepartmentRecord[]>();
    for (const department of departments) {
      const parent = department.parentId || 'root';
      children.set(parent, [...(children.get(parent) ?? []), department]);
    }
    for (const entries of children.values()) entries.sort((a, b) => a.sortOrder - b.sortOrder || a.name.localeCompare(b.name, 'zh-CN'));
    const result: DepartmentView[] = [];
    const walk = (parent: string, depth: number) => {
      for (const item of children.get(parent) ?? []) {
        result.push({ ...item, depth });
        walk(item.id, depth + 1);
      }
    };
    walk('root', 0);
    return result;
  }

  private emptyDepartment(): DepartmentRecord {
    return { id: '', organizationId: '', code: '', name: '', shortName: '', parentId: null,
      sortOrder: 100, revision: '1', enabled: true, personCount: 0 };
  }

  private emptyPerson(): PersonRecord {
    return { id: '', employeeNumber: '', loginName: '', displayName: '', avatarResourceId: '', workPhone: '',
      extensionNumber: '', workEmail: '', departmentId: '', departmentName: '', positionId: '0', positionName: '',
      revision: '1', enabled: true, presence: 'offline' };
  }

  private handleError(error: HttpErrorResponse, fallback: string): void {
    this.loading.set(false);
    this.errorMessage.set((error.error as ApiErrorBody | undefined)?.message || fallback);
  }

  private showToast(message: string): void {
    this.toastMessage.set(message);
    window.setTimeout(() => this.toastMessage.set(''), 2600);
  }
}
