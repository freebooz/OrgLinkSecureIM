import { DatePipe } from '@angular/common';
import { HttpErrorResponse } from '@angular/common/http';
import { Component, computed, inject, signal } from '@angular/core';
import { FormsModule } from '@angular/forms';
import { AdminApiService } from '../../core/admin-api.service';
import { ApiErrorBody, FileRecord, PageResponse } from '../../core/admin.models';
import { AppIconComponent } from '../../shared/app-icon.component';

type FileAction = 'revoke' | 'delete';

/** @brief 共享文件治理页；仅操作元数据和共享关系，不向浏览器暴露 MinIO 对象键或凭据。 */
@Component({
  selector: 'app-files-page',
  imports: [FormsModule, DatePipe, AppIconComponent],
  templateUrl: './files.page.html',
  styleUrl: './files.page.scss'
})
export class FilesPage {
  private readonly api = inject(AdminApiService);
  readonly loading = signal(false);
  readonly errorMessage = signal('');
  readonly toastMessage = signal('');
  readonly searchText = signal('');
  readonly page = signal<PageResponse<FileRecord>>({ items: [], total: 0, page: 1, pageSize: 20 });
  readonly selected = signal<FileRecord | null>(null);
  readonly pendingAction = signal<FileAction | null>(null);
  readonly pageCount = computed(() => Math.max(1, Math.ceil(this.page().total / this.page().pageSize)));
  readonly activeShares = computed(() => this.page().items.reduce((sum, item) => sum + item.activeShareCount, 0));
  readonly activeFiles = computed(() => this.page().items.filter((item) => !item.deleted).length);

  constructor() {
    this.load();
  }

  /** 查询当前组织的文件文档，搜索和分页均由服务端执行。 */
  load(page = this.page().page): void {
    this.loading.set(true);
    this.api.files(this.searchText(), page, this.page().pageSize).subscribe({
      next: (result) => {
        this.page.set(result);
        const selected = this.selected();
        this.selected.set(selected ? result.items.find((item) => item.uuid === selected.uuid) ?? null : null);
        this.loading.set(false);
      },
      error: (error: HttpErrorResponse) => this.handleError(error, '共享文件加载失败')
    });
  }

  confirm(file: FileRecord, action: FileAction): void {
    this.selected.set(file);
    this.pendingAction.set(action);
  }

  cancelAction(): void {
    this.pendingAction.set(null);
  }

  /** 撤销会保留历史共享记录并写入 revoked_at_utc，便于后续审计。 */
  executeAction(): void {
    const file = this.selected();
    const action = this.pendingAction();
    if (!file || !action) return;
    this.loading.set(true);
    const request = action === 'revoke' ? this.api.revokeFileShares(file.uuid) : this.api.deleteFile(file.uuid);
    request.subscribe({
      next: () => {
        this.pendingAction.set(null);
        this.showToast(action === 'revoke' ? '文件共享关系已撤销' : '文件已移入回收状态，对象正文已安全保留');
        this.load();
      },
      error: (error: HttpErrorResponse) => this.handleError(error, action === 'revoke' ? '撤销共享失败' : '删除文件失败')
    });
  }

  formatBytes(bytesText: string): string {
    let value = Number(bytesText);
    if (!Number.isFinite(value) || value < 0) return '-';
    if (value < 1024) return `${value} B`;
    const units = ['KB', 'MB', 'GB', 'TB'];
    let unit = 0;
    value /= 1024;
    while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit += 1; }
    return `${value.toFixed(value >= 10 ? 1 : 2)} ${units[unit]}`;
  }

  fileKind(mediaType: string): string {
    if (mediaType.startsWith('image/')) return '图片';
    if (mediaType.startsWith('video/')) return '视频';
    if (mediaType.startsWith('audio/')) return '音频';
    if (mediaType.includes('pdf')) return 'PDF';
    if (mediaType.includes('spreadsheet') || mediaType.includes('excel')) return '表格';
    if (mediaType.includes('word') || mediaType.includes('document')) return '文档';
    return '其他';
  }

  private handleError(error: HttpErrorResponse, fallback: string): void {
    this.loading.set(false);
    this.errorMessage.set((error.error as ApiErrorBody | undefined)?.message || fallback);
  }

  private showToast(message: string): void {
    this.toastMessage.set(message);
    window.setTimeout(() => this.toastMessage.set(''), 2800);
  }
}
