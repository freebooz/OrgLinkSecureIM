#pragma once

#include <string>

namespace orglink::persistence
{

/**
 * @brief 以 UTF-8 读取进程环境变量。
 *
 * Windows 使用宽字符环境块再显式转换 UTF-8，避免 `_dupenv_s` 按系统代码页转换中文账号、路径或显示名称；
 * Unix 环境按部署约定直接解释为 UTF-8。空值返回 fallback 的副本。
 */
[[nodiscard]] std::string environmentUtf8(const char* name, std::string fallback = {});

} // namespace orglink::persistence
