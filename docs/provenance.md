# 导入来源

导入日期：2026-10-12（Asia/Shanghai）。本文件记录首次组合时的来源；导入后以本仓库历史为准。

| 组件 | 来源 | 基准 | 说明 |
|---|---|---|---|
| Groth16 CUDA | `/home/slinevit2003/lightZK` | `12838f6` (`master`) | 包含未提交的字段修正、Poseidon 新文件与依赖工作树改动；未导入构建目录和嵌套依赖 |
| Groth16 DCU | `/home/slinevit2003/Downloads/lightZK` | `24d97d6` (`master`) | 包含未提交的 hipify 工作树；排除 `.prehip`、`.cu/.cuh` 旧副本、构建目录和压缩包 |
| Groth16 vendored dependencies | `/home/slinevit2003/lightZK/depends` | depends `cf9f722`；libsnark `2799376` | 复制实际源码并移除所有嵌套 `.git`/`.gitmodules`；保留未提交的 ALT_BN128 及 libff `thread_local std::mt19937_64` 随机数修改；后续裁剪未使用的测试、示例、profile、ADSNARK/SUPERCOP 和旧 BN128 后端 |
| PLONK CPU | `/home/slinevit2003/Downloads/linux-bak/down/plonk-cpu` | 无 Git 元数据 | 排除 Notebook 状态和构建输出 |
| PLONK CUDA | `/home/slinevit2003/Downloads/linux-bak/down/plonk-gpu` | 无 Git 元数据 | 排除 Notebook 状态、`target/` 和生成的 `*.bin` |
| Winterfell STARK | `https://github.com/SlInevitable2003/winterfell-gpu` | `388c55b95330b3f339a50e6718eb7925d3f94d02` (`main`) | 浅克隆后导入源码，不包含嵌套 `.git` |

这些目录是工作快照，而不是 Git submodule。这样可以在一个提交图中原子修改公共接口与多个协议，同时避免本地未提交实验代码丢失。

首次导入后的文件树摘要（逐文件 SHA-256 再汇总，不含 `target/` 和上游忽略的 Winterfell `Cargo.lock`）如下：

```text
4576ec643f5736014d5fcdb10a462f2c38e22ad66f9c7447945bdf9e046ad118  groth16/backends/cuda
c53bae78558daf52658793c2406849fce8547bb6dc941477caf38ba4842f77b7  groth16/backends/dcu
023d64130bdd14d39defa011fd3916bd52cafec314a9d862383e22e9a7de4f92  plonk/implementations/cpu
6dc5e9cad6a96902608b70335d9f616c38e71fe3b2f63cfdd39c169b6bd5c135  plonk/implementations/cuda
1dd337a7f21cf61267058222ea0f5a3b61eb7ad688c53b2c452c3a3d762c6ed9  stark/winterfell
```

Groth16 vendored dependencies use the same sorted per-file SHA-256 aggregation:

```text
6b6e90d6491bc3f76479c0602a65c39a0e890bfec1aae337cc0cec66bce424b2  groth16/third_party/depends
```
