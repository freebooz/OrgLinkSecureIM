import { Component, computed, inject, signal } from '@angular/core';
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { AuthService } from '../core/auth.service';
import { AppIconComponent } from '../shared/app-icon.component';

/** @brief Web 管理端公共顶部栏、导航栏和移动端抽屉；所有业务页面共享同一组件。 */
@Component({
  selector: 'app-admin-shell',
  imports: [RouterOutlet, RouterLink, RouterLinkActive, AppIconComponent],
  templateUrl: './admin-shell.component.html',
  styleUrl: './admin-shell.component.scss'
})
export class AdminShellComponent {
  readonly auth = inject(AuthService);
  readonly menuOpen = signal(false);
  readonly initials = computed(() => this.auth.session()?.displayName.slice(0, 1) ?? '管');

  /** 移动端选择模块后收起抽屉，桌面端状态不受影响。 */
  closeMenu(): void {
    this.menuOpen.set(false);
  }
}
