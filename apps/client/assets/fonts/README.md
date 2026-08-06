# 客户端内置字体

本目录保存客户端随程序发布的字体资产，避免目标机器缺少指定中文字体时产生布局漂移。

| 用途 | 字体与版本 | 项目内文件 | 上游发行包 SHA-256 |
|---|---|---|---|
| 登录页、导航、表单、列表、标题及聊天元数据 | Sarasa UI SC 1.0.40 | `SarasaUiSC-Regular.ttf`、`SarasaUiSC-SemiBold.ttf`、`SarasaUiSC-Bold.ttf` | `BB9891C8BE805CD0DAE942A07472B3031DB2510741B7CDE42E9591F74A186F6A` |
| 聊天气泡正文与文件消息标题 | Source Han Sans SC 2.005R | `SourceHanSansSC-Regular.otf` | `EF7364F7AC2564BE1AE9C1D74276DE2653FE38B73449070398C4FC0B7E032FF1` |

上游来源：

- Sarasa Gothic：<https://github.com/be5invis/Sarasa-Gothic/releases/tag/v1.0.40>
- Source Han Sans：<https://github.com/adobe-fonts/source-han-sans/releases/tag/2.005R>

两套字体均按 SIL Open Font License 1.1 分发。对应许可证保存在
`LICENSE-Sarasa-Gothic.txt` 和 `LICENSE-Source-Han-Sans.txt`，构建后会复制到
客户端目录的 `licenses/fonts`。更新字体时必须先精确删除本目录内对应旧版本文件，
再从官方发行包全量导入并同步更新资源清单、校验值和许可证。
