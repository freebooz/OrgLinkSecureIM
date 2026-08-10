import { Routes } from '@angular/router';
import { authGuard } from './core/auth.guard';

/** @brief 管理端路由均按功能懒加载，未认证请求统一回到登录页。 */
export const routes: Routes = [
  {
    path: 'login',
    loadComponent: () => import('./pages/login/login.page').then((module) => module.LoginPage)
  },
  {
    path: '',
    canActivate: [authGuard],
    loadComponent: () => import('./layout/admin-shell.component').then((module) => module.AdminShellComponent),
    children: [
      {
        path: 'dashboard',
        loadComponent: () => import('./pages/dashboard/dashboard.page').then((module) => module.DashboardPage)
      },
      {
        path: 'organization',
        loadComponent: () => import('./pages/organization/organization.page').then((module) => module.OrganizationPage)
      },
      {
        path: 'files',
        loadComponent: () => import('./pages/files/files.page').then((module) => module.FilesPage)
      },
      { path: '', pathMatch: 'full', redirectTo: 'dashboard' }
    ]
  },
  { path: '**', redirectTo: '' }
];
