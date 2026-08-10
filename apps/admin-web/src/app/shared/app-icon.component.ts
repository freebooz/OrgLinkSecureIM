import { Component, Input } from '@angular/core';

const ICON_PATHS: Record<string, string> = {
  dashboard: 'M3 3h7v7H3z M14 3h7v4h-7z M14 11h7v10h-7z M3 14h7v7H3z',
  organization: 'M4 20v-8h6v8 M14 20V4h6v16 M7 12V7h7 M17 8h.01 M17 12h.01 M17 16h.01',
  people: 'M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2 M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8 M22 21v-2a4 4 0 0 0-3-3.87 M16 3.13a4 4 0 0 1 0 7.75',
  files: 'M3 6h6l2 2h10v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z M3 10h18',
  audit: 'M12 22a10 10 0 1 0-10-10 M12 6v6l4 2',
  search: 'M21 21l-4.35-4.35 M19 11a8 8 0 1 1-16 0 8 8 0 0 1 16 0',
  plus: 'M12 5v14 M5 12h14',
  edit: 'M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L8 18l-4 1 1-4z',
  refresh: 'M20 11a8.1 8.1 0 1 0 2 5 M20 4v7h-7',
  bell: 'M18 8a6 6 0 0 0-12 0c0 7-3 7-3 9h18c0-2-3-2-3-9 M10 21h4',
  logout: 'M10 17l5-5-5-5 M15 12H3 M21 19V5a2 2 0 0 0-2-2h-6',
  chevron: 'M9 18l6-6-6-6',
  lock: 'M5 11h14v10H5z M8 11V7a4 4 0 0 1 8 0v4',
  shield: 'M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z M9 12l2 2 4-5',
  trash: 'M3 6h18 M8 6V4h8v2 M19 6l-1 15H6L5 6 M10 11v6 M14 11v6',
  share: 'M18 8a3 3 0 1 0 0-6 3 3 0 0 0 0 6 M6 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6 M18 24a3 3 0 1 0 0-6 3 3 0 0 0 0 6 M8.6 13.5l6.8-4 M8.6 16.5l6.8 4',
  menu: 'M4 6h16 M4 12h16 M4 18h16'
};

/** @brief 项目内置线性图标，统一 24 像素画布、线宽和颜色继承规则。 */
@Component({
  selector: 'app-icon',
  template: `<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path [attr.d]="path" /></svg>`,
  styles: [`:host{display:inline-flex;width:1.25rem;height:1.25rem;flex:none}svg{width:100%;height:100%;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}`]
})
export class AppIconComponent {
  @Input({ required: true }) name = 'dashboard';
  get path(): string { return ICON_PATHS[this.name] ?? ICON_PATHS['dashboard']; }
}
