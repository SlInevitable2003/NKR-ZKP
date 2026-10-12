# NKR-ZKP

NKR-ZKP 是一个面向多协议、多应用和多计算平台的零知识证明实验框架。目前汇集：

- Groth16：CPU/libsnark 公共依赖、NVIDIA CUDA 后端和 HIP/DCU 后端；
- zk-STARK：基于 Winterfell 的 CPU/多线程实现及矩阵乘法应用；
- PLONK：CPU 基线和实验性 NVIDIA CUDA 实现。

仓库以“协议语义与计算后端分离”为目标。当前导入保留了各实验版本的可追溯快照，先保证不丢代码，再逐步把 MSM、NTT/FFT、哈希和矩阵运算收敛到统一后端接口。详细边界见 [架构说明](docs/architecture.md)，原始来源见 [导入清单](docs/provenance.md)。

## 快速入口

```bash
make help
make check

# Winterfell 多线程构建/矩阵乘法；线程数可覆盖
make stark THREADS=8
make stark-matmul THREADS=8 MATRIX_SIZE=8

# PLONK CPU
make plonk-cpu

# Groth16 需要指出 lightZK 的 depends 目录
make groth16-cuda GROTH16_DEPS_DIR=/path/to/lightZK/depends
make groth16-dcu  GROTH16_DEPS_DIR=/path/to/lightZK/depends
```

CUDA、HIP/DCU 和 PLONK GPU 构建分别需要相应厂商工具链。本仓库不会提交构建缓存或生成的 kernel 二进制。

## 目录

```text
protocols/
  groth16/
    common/                  平台无关头文件
    backends/{cuda,dcu}/     平台实现
  plonk/implementations/
    cpu/                     CPU 基线快照
    cuda/                    CUDA 实验快照
  stark/winterfell/          Winterfell 多线程实现
docs/                        架构与来源记录
scripts/                     仓库检查工具
```
