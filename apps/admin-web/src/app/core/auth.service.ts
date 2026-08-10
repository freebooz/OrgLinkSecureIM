import { HttpClient } from '@angular/common/http';
import { computed, inject, Injectable, signal } from '@angular/core';
import { Router } from '@angular/router';
import { catchError, finalize, tap, throwError } from 'rxjs';
import { AdminSession } from './admin.models';

const SESSION_STORAGE_KEY = 'orglink.admin.session';

/**
 * @brief 管理端会话状态。
 *
 * HttpOnly 会话 Cookie 由浏览器持有；sessionStorage 仅保存 CSRF 和展示信息，关闭标签页后自动清除。
 */
@Injectable({ providedIn: 'root' })
export class AuthService {
  private readonly http = inject(HttpClient);
  private readonly router = inject(Router);
  private readonly sessionState = signal<AdminSession | null>(this.restoreSession());
  readonly session = this.sessionState.asReadonly();
  readonly authenticated = computed(() => this.sessionState() !== null);
  readonly busy = signal(false);

  /** 校验口令并建立 Cookie 会话；失败不会把口令写入任何前端持久化介质。 */
  login(loginName: string, password: string) {
    this.busy.set(true);
    return this.http.post<AdminSession>('/api/admin/auth/login', { loginName, password }).pipe(
      tap((session) => {
        this.sessionState.set(session);
        sessionStorage.setItem(SESSION_STORAGE_KEY, JSON.stringify(session));
      }),
      finalize(() => this.busy.set(false))
    );
  }

  /** 服务端撤销会话后清理标签页状态；网络失败也会清理本地状态，避免产生假登录界面。 */
  logout(): void {
    this.http.post('/api/admin/auth/logout', {}).pipe(
      catchError((error) => throwError(() => error)),
      finalize(() => this.clearSession())
    ).subscribe({ error: () => undefined });
  }

  /** 收到 401 时由拦截器调用，统一清理状态并跳转。 */
  expire(): void {
    this.clearSession();
  }

  private clearSession(): void {
    sessionStorage.removeItem(SESSION_STORAGE_KEY);
    this.sessionState.set(null);
    void this.router.navigateByUrl('/login');
  }

  private restoreSession(): AdminSession | null {
    try {
      const raw = sessionStorage.getItem(SESSION_STORAGE_KEY);
      return raw ? JSON.parse(raw) as AdminSession : null;
    } catch {
      sessionStorage.removeItem(SESSION_STORAGE_KEY);
      return null;
    }
  }
}
