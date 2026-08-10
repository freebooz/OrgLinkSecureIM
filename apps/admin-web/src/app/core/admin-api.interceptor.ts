import { HttpErrorResponse, HttpInterceptorFn } from '@angular/common/http';
import { inject } from '@angular/core';
import { catchError, throwError } from 'rxjs';
import { AuthService } from './auth.service';

/** 为同源管理请求附加 Cookie 与 CSRF；401 会触发全局会话失效。 */
export const adminApiInterceptor: HttpInterceptorFn = (request, next) => {
  const auth = inject(AuthService);
  const mutation = !['GET', 'HEAD', 'OPTIONS'].includes(request.method.toUpperCase());
  const csrfToken = auth.session()?.csrfToken;
  const securedRequest = request.clone({
    withCredentials: true,
    setHeaders: mutation && csrfToken ? { 'X-CSRF-Token': csrfToken } : {}
  });
  return next(securedRequest).pipe(
    catchError((error: HttpErrorResponse) => {
      if (error.status === 401 && !request.url.endsWith('/auth/login')) {
        auth.expire();
      }
      return throwError(() => error);
    })
  );
};
