# 架构与演进约束

## 依赖方向

项目分为四个层次：

```text
应用程序
  -> 协议公共 API
       -> 协议后端适配层
            -> 公共算子层
                 -> 第三方运行时/工具链
```

依赖只能向下。公共算子不得包含 Groth16 或 PLONK 类型，一个协议也不得包含另一个协议的后端。

## 公共算子

`operators/cuda` 和 `operators/hip` 包含从 LightZK 导入的 BN254 域、曲线、NTT、MSM 及设备运行时原语。Groth16 和 PLONK 都链接这些目标，不再各自保存一份副本。

CUDA 和 HIP 共用接口约定和测试向量，但不共用内核源码。二者在启动策略、归约方式、内存 API 和性能调优方面差异较大，使用宏强行统一源码反而会增加维护难度。

以下操作属于公共算子层：

- BN254 基域和扩域运算；
- G1/G2 表示及群运算；
- NTT/iNTT 及陪集变体；
- G1/G2 MSM；
- 设备内存分配、流、错误处理和性能分析钩子；
- 未来可复用的通用扫描、前缀积和批量求逆原语。

R1CS、QAP、置换乘积、商多项式方程、Transcript 和证明容器不属于算子层。

## 协议目录结构

Groth16 和 PLONK 有意采用相同的目录及命名风格：

```text
protocols/<protocol>/
  app/
  common/include/<protocol>/
  common/src/
  backends/cpu/
  backends/cuda/
  backends/hip/
  tests/
```

`app/` 只包含轻量的命令行入口。`common/` 负责协议类型、公共 API、校验、规范化编码、验证和后端选择。各后端目录负责平台适配与协议专属的融合内核。公共头文件只能使用主机侧类型。

Groth16 是外部 `gen/setup/prove/verify` 组织方式的可用参考，但其当前较粗粒度的 `Backend::setup/prove` 接口不构成 PLONK 内部实现的限制。PLONK 可以采用按轮次组织的会话，在公共层控制 Transcript 顺序的同时，让多项式常驻 GPU。

## PLONK 范围

PLONK 已实现独立的三线算术化 Manager 和 CPU Setup 内存 API；Prove、Verify、规范化文件 I/O 及加速器语义仍是框架。已删除的 Rust 代码基于另一条 BLS12 系列曲线，实现的是带查找功能的四线系统，因此不会作为新实现的基础。首个实现范围为：

- BN254/ALT_BN128；
- KZG 承诺；
- 经典三线算术门；
- 置换大乘积；
- 先完成 CPU 参考实现，再基于公共算子增加 CUDA 和 HIP 适配层。

在 Transcript、挑战生成、规范化编码和低层协议细节完全确定之前，证明容器保持不透明。

## 构建模型

CPU 支持始终存在。每个 CMake 构建目录通过 `NKR_ZKP_ACCELERATOR` 在 `NONE`、`CUDA` 和 `HIP` 中最多选择一种。这样既可避免混用不兼容的编译器驱动与运行时，又能在构建中保留 CPU 和已选加速器之间的运行时后端选择。

内置的 LightZK 依赖快照位于 `third_party/lightzk/depends`。libff 的 x86-64 汇编仅在匹配的主机上启用，其他 CPU 架构使用可移植实现。

## 迁移与评审规则

1. 每次抽取公共算子后，都必须保持 Groth16 CPU 构建可用。
2. CUDA/HIP 算子迁移既要用与协议无关的测试向量验证，也要通过现有 Groth16 端到端流程验证。
3. 开始加速器实现前，先建立确定性的 PLONK CPU 测试向量。
4. 协议语义变更与机械性的 CUDA 到 HIP 迁移应分别提交。
5. 新的 PLONK 产物使用规范化序列化，不得将原始 libff 或设备内存布局作为跨平台文件格式。
6. 生成的构建目录、内核产物和基准测试输出不得提交到 Git。
