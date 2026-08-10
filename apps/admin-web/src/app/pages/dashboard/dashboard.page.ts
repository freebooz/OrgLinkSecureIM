import { DatePipe, DecimalPipe } from '@angular/common';
import { HttpErrorResponse } from '@angular/common/http';
import { Component, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { AdminApiService } from '../../core/admin-api.service';
import { AdminOverview, ApiErrorBody } from '../../core/admin.models';
import { AppIconComponent } from '../../shared/app-icon.component';

/** @brief 管理工作台聚合组织规模、实时在线人数、共享文件与最近审计动作。 */
@Component({
  selector: 'app-dashboard-page',
  imports: [RouterLink, AppIconComponent, DecimalPipe, DatePipe],
  templateUrl: './dashboard.page.html',
  styleUrl: './dashboard.page.scss'
})
export class DashboardPage {
  private readonly api = inject(AdminApiService);
  readonly loading = signal(true);
  readonly errorMessage = signal('');
  readonly overview = signal<AdminOverview | null>(null);

  constructor() {
    this.refresh();
  }

  /** 从服务端重新读取聚合数据，不在浏览器用列表推算在线或文件计数。 */
  refresh(): void {
    this.loading.set(true);
    this.errorMessage.set('');
    this.api.overview().subscribe({
      next: (overview) => { this.overview.set(overview); this.loading.set(false); },
      error: (error: HttpErrorResponse) => {
        this.loading.set(false);
        this.errorMessage.set((error.error as ApiErrorBody | undefined)?.message || '工作台数据加载失败');
      }
    });
  }

  /** 用二进制单位展示服务端返回的字节数。 */
  formatBytes(bytes: number): string {
    if (bytes < 1024) return `${bytes} B`;
    const units = ['KB', 'MB', 'GB', 'TB'];
    let value = bytes / 1024;
    let index = 0;
    while (value >= 1024 && index < units.length - 1) { value /= 1024; index += 1; }
    return `${value.toFixed(value >= 10 ? 1 : 2)} ${units[index]}`;
  }
}
