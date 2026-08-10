import { HttpErrorResponse } from '@angular/common/http';
import { Component, inject, signal } from '@angular/core';
import { FormControl, FormGroup, ReactiveFormsModule, Validators } from '@angular/forms';
import { Router } from '@angular/router';
import { ApiErrorBody } from '../../core/admin.models';
import { AuthService } from '../../core/auth.service';
import { AppIconComponent } from '../../shared/app-icon.component';

/** @brief Web 管理端登录页；只接受已授予 administrator_roles 的账号。 */
@Component({
  selector: 'app-login-page',
  imports: [ReactiveFormsModule, AppIconComponent],
  templateUrl: './login.page.html',
  styleUrl: './login.page.scss'
})
export class LoginPage {
  private readonly router = inject(Router);
  readonly auth = inject(AuthService);
  readonly errorMessage = signal('');
  readonly passwordVisible = signal(false);
  readonly form = new FormGroup({
    loginName: new FormControl('', { nonNullable: true, validators: [Validators.required, Validators.maxLength(128)] }),
    password: new FormControl('', { nonNullable: true, validators: [Validators.required, Validators.maxLength(1024)] })
  });

  /** 提交登录，成功后进入工作台；服务端统一返回模糊认证错误以防账号枚举。 */
  submit(): void {
    if (this.form.invalid || this.auth.busy()) {
      this.form.markAllAsTouched();
      return;
    }
    this.errorMessage.set('');
    const { loginName, password } = this.form.getRawValue();
    this.auth.login(loginName, password).subscribe({
      next: () => void this.router.navigateByUrl('/dashboard'),
      error: (error: HttpErrorResponse) => {
        const body = error.error as ApiErrorBody | undefined;
        this.errorMessage.set(body?.message || '暂时无法连接管理服务，请稍后重试');
        this.form.controls.password.setValue('');
      }
    });
  }
}
