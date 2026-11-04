# 上游源码摘取与适配记录

来源：ADGaiNai6657/FastCDC，分支 `feature/chunk-store-dedup`。
固定提交：`be5816e8e2e8548738db3e7dd54aa323056df402`。

| 原路径/本项目路径 | 原文件 SHA-256 | 适配 |
| --- | --- | --- |
| `src/GearHash/GearHash.h` | `b8ecf15d6c81faa4e89b7c56f7f230cc8eb49a59bcd0b3fd9a0756de6133c7f6` | 原样摘取；未改动 |
| `src/GearHash/GearHash.cpp` | `877b4bbbfa1ee686218f56af7d69888b705afef9d6a8bd2397f583ffd863edac` | 原样摘取；未改动 |
| `src/BreakingApart/BreakingApart.h` | `d5b55f9f4ca644167a7770ee3cc3754da5bb4e3debd4da8167e6d5b2aafb9ba6` | 原样摘取；未改动 |
| `src/BreakingApart/BreakingApart.cpp` | `67b1ae15256d3cc106115bb015605cf70d96a2e643efdad3d8999c2537acbeec` | 原样摘取；未改动 |

新增 `Chunker.h/.cpp` 在外层严格验证 D-16、拒绝零最小长度、检查偏移溢出与切点全覆盖；没有改变来源判定字节语义。
D-21 已参考下列来源适配 Hash/ChunkStore，保留 EVP SHA-1、同摘要候选桶及长度/逐字节确认。新的实现使用显式实验独占状态与 storedId，不照搬全局变量；SHA-256 完整性校验继续独立使用。

## D-21 适配模块来源

| 来源路径/本项目路径 | 固定来源 SHA-256 | 适配差异 |
| --- | --- | --- |
| `src/Hash/Hash.h` | `3aa53d4368cb6c644fb6035950aff7405640128b5cfdcde80510ac84ffba368b` | 保留 20 字节摘要接口，新增显式十六进制编码，统一 pragma once |
| `src/Hash/Hash.cpp` | `7f4d020c45e3515942139cce5b1bff8d17ec86b180bbb8276cdf5b5f679b3700` | EVP_Digest 检查错误，移除线程局部裸指针，容器桶号使用完整摘要混合 |
| `src/ChunkStore/ChunkStore.h` | `27b030ddbdff8fc75f6a4bbd088f2aba6acc9c505beef2dd08e342198cdeaacd` | 实验独占 Store，无可变全局；用 storedId 及出现记录代替全局指针容器 |
| `src/ChunkStore/ChunkStore.cpp` | `9167f0e9c34118d5697a9e6b9d0128df5d1a2d515139c5bc12c5a8b8d3e6dfba` | 保留摘要＋长度/字节确认，新增查询不计数、计数溢出检查与插入回滚 |

以上均为适配实现，不声称与上游文件逐字节相同。实际本项目文件摘要由构建生成的 source-files.csv 保存。上游仓库未修改。

## D-22：jmacd/xdelta

固定仓库 https://github.com/jmacd/xdelta ，提交 `5d936d2c30f78729375c3ae070248a05bef27bd2`。源码版本 3.2.2 加后续空解码段/空指针修复，正式 v3.2.2 标签 `5f8f5a553181c139d91b0289c009e5c276c5318c` 不同；不得仅凭版本字符串替换。Apache 2.0 原许可证见 [LICENSE](../third_party/xdelta/xdelta3/LICENSE)。

23 个原样上游文件收录于 third_party/xdelta/xdelta3，包括 C 核心/依赖头、上游 CMake/模板、说明和许可证。[UPSTREAM.csv](../third_party/xdelta/UPSTREAM.csv) 保存 Git blob SHA-1 与长度，取得时逐文件按 `SHA1("blob " + length + NUL + bytes)` 确认；[SOURCE-MANIFEST.csv](../third_party/xdelta/SOURCE-MANIFEST.csv) 保存逐文件 SHA-256，CMake 每次配置校验，构建摘要另覆盖实际文件。

[cmake/Xdelta.cmake](../cmake/Xdelta.cmake) 调用原上游 CMake，仅构建 C99 静态核心库；禁用 CLI armor、LZMA/BLAKE3 下载；运行时不选择二级压缩（原上游静态库仍包含 DJW 能力）。通过上游支持的 `HAVE_ALIGNED_ACCESS_REQUIRED=1` 采用 memcpy 读取，避免直接未对齐整数解引用的 C UB；未修改任一上游文件。Debug sanitizer 同时覆盖 C 库。运行产物嵌入 xdelta-source-files.csv 及其摘要，没有运行时下载/源码依赖。

本项目新增 DeltaCodec 仅包装内存 API、容量与异常；不重新实现 Delta 算法。运行 flags 为 `XD3_COMPLEVEL_6 | XD3_ADLER32`，不设置任何 XD3_SEC_TYPE；`SEC_NOALL` 不代表独立的“无二级压缩”配置。完整方向、存储和验证见 [15](15-xdelta-pipeline-proposal.md)。
