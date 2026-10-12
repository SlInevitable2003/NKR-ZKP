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
make stark-matmul THREADS=4 MATRIX_SIZE=4 MEMORY_MIB=4096 TIMEOUT=300

# PLONK CPU
make plonk-cpu
make plonk-test MEMORY_MIB=4096 TIMEOUT=300

# Groth16：同一套协议应用通过 --backend 选择 CPU/CUDA/DCU
make groth16-test-cpu
make groth16-test-cuda CUDA_ARCH=120
make groth16-dcu
```

Groth16 所需的 `libsnark`、`libff`、`libfqfft`、CGBN 等依赖保存在
`protocols/groth16/third_party/depends`。这是包含本项目多线程随机数修改的源码快照，
不是 Git submodule；构建不再依赖原来的 `~/lightZK` 目录。CUDA、HIP/DCU 和
PLONK GPU 构建仍分别需要相应厂商工具链。本仓库不会提交构建缓存或生成的
kernel 二进制。

矩阵测试默认拒绝大于 16 的维度。Groth16 CUDA 默认使用 n=12，因为导入的
NTT 优化内核面向大域，而框架为小域提供了安全的 radix-2 路径。GPU 命令使用
`MEMORY_MIB=0` 跳过会破坏 CUDA 虚拟地址预留的 `ulimit -v`，但仍有输入规模、
超时、启动前主存余量以及至少保留 512 MiB 空闲显存四层保护。

只有在确认内存和显存预算后才应显式设置
`NKR_ZKP_ALLOW_LARGE=1`；这只是解除保护，不代表规模一定安全。

## 目录

```text
protocols/
  groth16/
    app/                     平台无关的 gen/setup/prove/verify CLI
    common/                  协议类型、验证、I/O 与后端接口
    backends/{cpu,cuda,dcu}/ 平台实现
    third_party/depends/     定制 libsnark 依赖源码快照
  plonk/implementations/
    cpu/                     CPU 基线快照
    cuda/                    CUDA 实验快照
  stark/winterfell/          Winterfell 多线程实现
docs/                        架构与来源记录
scripts/                     仓库检查工具
```
