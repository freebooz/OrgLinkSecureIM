import { Component } from '@angular/core';
import { RouterOutlet } from '@angular/router';

@Component({
  selector: 'app-root',
  imports: [RouterOutlet],
  template: '<router-outlet />',
  styleUrl: './app.scss'
})
/** @brief Web 管理端根组件，仅承载路由出口，业务状态由页面服务管理。 */
export class App {
}
