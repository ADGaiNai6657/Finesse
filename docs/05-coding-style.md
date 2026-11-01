# 代码写法与 FastCDC 风格对应

## 风格依据

`[已批准 D-02]` 用户指定 [ADGaiNai6657/FastCDC](https://github.com/ADGaiNai6657/FastCDC.git)。本次通过 GitHub 读取远端 `feature/chunk-store-dedup`，固定参考提交为 `be5816e8e2e8548738db3e7dd54aa323056df402`，检查日期 2026-11-01。

已检查以下源文件：

- [GearHash.h](https://github.com/ADGaiNai6657/FastCDC/blob/be5816e8e2e8548738db3e7dd54aa323056df402/src/GearHash/GearHash.h)
- [GearHash.cpp](https://github.com/ADGaiNai6657/FastCDC/blob/be5816e8e2e8548738db3e7dd54aa323056df402/src/GearHash/GearHash.cpp)
- [main.cpp](https://github.com/ADGaiNai6657/FastCDC/blob/be5816e8e2e8548738db3e7dd54aa323056df402/src/main.cpp)
- [CMakeLists.txt](https://github.com/ADGaiNai6657/FastCDC/blob/be5816e8e2e8548738db3e7dd54aa323056df402/CMakeLists.txt)

已核实该远端分支与本机 HEAD 一致，包含 GearHash、BreakingApart、Hash、ChunkStore 及实验驱动。初版文档误以 main 为基准，本版已按用户指示改用功能分支。复用模块清单见 [08-fastcdc-reuse.md](08-fastcdc-reuse.md)。

## 继承的写法

| 项目 | FastCDC 已观察到的写法 | Finesse 采用方式 |
| --- | --- | --- |
| 语言 | C++20 | 保持 C++20 |
| 模块布局 | `src/GearHash/GearHash.h` 和 `.cpp` | `src/Finesse/Finesse.h` 和 `.cpp` 等同名组织 |
| 函数 | `gearRoll`、`updateGearHash` | lowerCamelCase，如 `extractFinesse` |
| 自定义类型 | `Object` | PascalCase，如 `ChunkView` |
| 常量 | `GEAR_TABLE`、`static constexpr` | 大写下划线、编译期常量 |
| 返回类型 | `auto ... -> uint64_t` | 普通函数使用尾置返回类型；`main` 可保留 `int main()` |
| 缩进与大括号 | 4 空格、左括号同行 | 采用同一布局 |
| 输入 | `const std::uint8_t *data, size_t length` | 保留只读指针加显式长度的思路 |
| 代码组织 | 普通函数和简单结构体 | 不无故增加复杂继承体系 |
| 注释 | 中文解释参数和执行过程 | 中文说明契约、边界、论文出处 |
| 构建 | CMake 显式列出源码，`src` 为 include 根目录 | 使用 target 级 include 路径与显式源文件清单 |

## 补充规范

以下为 `[已批准 D-10]`，解决参考仓库写法不统一或未覆盖的问题。

- 使用 `std::uint8_t`、`std::uint32_t`、`std::uint64_t`、`std::size_t`；禁止依赖普通 `char` 的符号性或 `long` 的平台位宽。
- 头文件自行包含所需标准头；统一 `#pragma once`，不再重复叠加 include guard。该选择是已批准的规范化要求，参考仓库同时出现两者。
- `.cpp` 先 include 自己的头；项目头用双引号，标准头用尖括号；删除重复、无用 include。头文件不写 `using namespace std`。
- 比较长度与索引时使用匹配的无符号类型；对减法、乘法和累计长度验证边界，不能把算法规定的模运算与意外溢出混为一谈。
- 只在确有状态回写需求时使用非 const 引用。纯输入用值或 const 引用；指针和长度的有效期写入接口注释。
- 热路径不写日志、不做文件 I/O、不创建随机数发生器。计时和 CSV 输出属于 Experiment。
- 注释应解释“为什么这样分组、窗口如何处理、参数来自哪里”，不保留无关的创建人/日期模板，不复制演示代码中的实现缺陷。
- 用户已批准 Rabin 改为 GearHash，使用来源表与更新函数；实验标记为 Gear 变体。不得继续擅自替换为 `std::hash` 或 xxHash。
- 默认不增加命名空间层级；若后续与其他项目集成产生名称冲突，再评审命名空间方案。

示例只展示风格，不定义具体的哈希行为：

```cpp
// 保留原始子块顺序；分组阶段使用副本排序。
auto groupFinesseFeatures(const FeatureValues &features)
    -> std::array<std::array<FingerprintValue, 4>, 3> {
    // 实现应遵循算法规格中的排名分组。
}
```

## 构建与检查

`[已批准 D-10]` 当前 Finesse 使用 CMake 最低版本 4.2，FastCDC 远端为 4.1；保留当前工程的 4.2，不为“风格一致”降级。显式设置 C++20 required、关闭编译器扩展。算法代码组成可供主程序和测试共用的 target；测试接入 CTest，首期使用简单断言式测试驱动，不额外引入测试框架依赖。

Debug 用于边界调试和 ASan/UBSan，Release 用于性能测试；结果必须写明优化选项。启用合理编译警告，禁止为了消除告警而关闭整个目标的检查。未批准前不生成 `.clang-format`，避免把推断的格式配置误当成用户规范。

摘取的上游接口保留 `BoundariesFinder` 等原名称以及 `BreakingApartConfig` 现有字段，适配层新接口采用 lowerCamelCase。原样摘取文件的 include guard 可保留；新文件按已批准规范使用 `#pragma once`。不得为了统一命名改变上游行为。
