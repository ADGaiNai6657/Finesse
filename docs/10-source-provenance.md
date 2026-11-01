# FastCDC 摘取记录

来源：ADGaiNai6657/FastCDC，分支 `feature/chunk-store-dedup`。
固定提交：`be5816e8e2e8548738db3e7dd54aa323056df402`。

| 原路径/本项目路径 | 原文件 SHA-256 | 适配 |
| --- | --- | --- |
| `src/GearHash/GearHash.h` | `b8ecf15d6c81faa4e89b7c56f7f230cc8eb49a59bcd0b3fd9a0756de6133c7f6` | 原样摘取；未改动 |
| `src/GearHash/GearHash.cpp` | `877b4bbbfa1ee686218f56af7d69888b705afef9d6a8bd2397f583ffd863edac` | 原样摘取；未改动 |
| `src/BreakingApart/BreakingApart.h` | `d5b55f9f4ca644167a7770ee3cc3754da5bb4e3debd4da8167e6d5b2aafb9ba6` | 原样摘取；未改动 |
| `src/BreakingApart/BreakingApart.cpp` | `67b1ae15256d3cc106115bb015605cf70d96a2e643efdad3d8999c2537acbeec` | 原样摘取；未改动 |

新增 `Chunker.h/.cpp` 在外层严格验证 D-16、拒绝零最小长度、检查偏移溢出与切点全覆盖；没有改变来源判定字节语义。
Hash/ChunkStore 及其全局状态未摘取；首期不做去重。SHA-256 实验完整性校验为新增代码。
