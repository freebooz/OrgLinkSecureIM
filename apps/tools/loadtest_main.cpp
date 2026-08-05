#include <algorithm>
#include <charconv>
#include <iostream>
#include <string_view>

/** @brief 解析正整数；非法输入返回零并由调用方拒绝，避免压力参数意外放大。 */
std::size_t parsePositive(std::string_view value)
{
    std::size_t result = 0;
    const auto [pointer, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    return error == std::errc{} && pointer == value.data() + value.size() ? result : 0;
}

/**
 * @brief 负载测试规划入口。
 *
 * 当前版本只校验连接规模和建议分批数，不创建网络连接；真实压测引擎完成前不得将结果视为容量测试数据。
 */
int main(int argc, char** argv)
{
    const std::size_t connections = argc > 1 ? parsePositive(argv[1]) : 100;
    if (connections == 0 || connections > 50000)
    {
        std::cerr << "connections must be in [1, 50000]\n";
        return 2;
    }
    constexpr std::size_t batchSize = 500;
    const auto batches = (connections + batchSize - 1) / batchSize;
    std::cout << "planned_connections=" << connections << '\n'
              << "batch_size=" << batchSize << '\n'
              << "batches=" << batches << '\n';
    return 0;
}

