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
make stark-matmul THREADS=4 MATRIX_SIZE=4

# PLONK CPU
make plonk-cpu
make plonk-test

# Groth16：同一套协议应用通过 --backend 选择 CPU/CUDA/DCU
make groth16-test-cpu
make groth16-test-cuda CUDA_ARCH=80       # NVIDIA A100
make groth16-test-dcu DCU_ARCH=<gfx-arch>
```

Groth16 所需的 `libsnark`、`libff`、`libfqfft`、CGBN 等依赖保存在
`protocols/groth16/third_party/depends`。这是包含本项目多线程随机数修改的源码快照，
不是 Git submodule；构建不再依赖原来的 `~/lightZK` 目录。CUDA、HIP/DCU 和
PLONK GPU 构建仍分别需要相应厂商工具链。本仓库不会提交构建缓存或生成的
kernel 二进制。

Groth16 CUDA 默认测试规模为 n=12。矩阵维度可以通过相应目标的 size 变量
调整；较大的电路会相应增加内存、显存和运行时间。

三个构建目标共用 `build/groth16`。CPU 后端总会编入；CUDA 和 DCU 是构建时
二选一的可选加速器，因为两者需要不同的编译器和运行时。`--backend` 在运行时
从当前二进制已经编入的后端中选择。例如 CUDA 构建同时支持 `cpu` 和 `cuda`，
DCU 构建同时支持 `cpu` 和 `dcu`。

切换 CUDA/DCU 工具链或目标平台前，先删除同一个 CMake 缓存再重新构建：

```bash
make groth16-clean
make groth16-cpu
```

`make clean` 会在清理 Cargo 产物的同时执行同样的 Groth16 清理。Groth16
需要支持 C++17 的编译器；libff 的 x86-64 汇编优化会按目标架构自动启用或
关闭，也可以在直接配置 CMake 时通过 `-DUSE_ASM=ON|OFF` 显式覆盖。

## 各项目怎么运行

下面的命令都从仓库根目录执行。构建命令只生成程序；带 `test` 或示例名称的
命令才会真正生成并验证证明。

### Groth16

推荐先运行完整的后端冒烟测试。它会依次生成矩阵乘法 R1CS 和 witness，执行
setup、prove，最后用平台无关的 verifier 验证证明：

```bash
# CPU：默认 n=4
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4

# NVIDIA GPU：CUDA_ARCH 是 compute capability；A100 为 80，RTX 5060 为 120
make groth16-test-cuda GROTH16_CUDA_MATRIX_SIZE=12 CUDA_ARCH=80

# HIP/DCU：DCU_ARCH 是 HIP offload target，以所用 DCU/编译器报告的 gfx 值为准
make groth16-test-dcu GROTH16_DCU_MATRIX_SIZE=12 DCU_ARCH=<gfx-arch>

# 打印公共流程和后端内部的完整分阶段 profile
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4 PROFILE=1
```

也可以在构建后手工执行四个阶段。下面以 CPU 和 `n=4` 为例：

```bash
make groth16-cpu
G16=build/groth16/bin
"$G16/groth16-gen" --profile matmul 4
"$G16/groth16-setup" --profile --backend cpu matmul_4.bin
"$G16/groth16-prove" --profile --backend cpu matmul_4.bin
"$G16/groth16-verify" --profile matmul_4.bin
```

四个程序的 `--profile` 都是可选参数；移除后不打印详细 profile。

生成物位于 `build/groth16/data/`：

- `matmul_<n>.bin`：R1CS；
- `matmul_<n>.witness.bin`：满足约束的 witness；
- `*.pk.bin` / `*.vk.bin`：证明密钥和验证密钥；
- `*.proof.bin`：最终证明。

CUDA 的手工用法相同：先执行 `make groth16-cuda CUDA_ARCH=<arch>`，然后把
setup/prove 的 `--backend` 改为 `cuda`。程序路径仍是上面的 `G16`，不会因
后端而改变。

DCU 需要安装可用的 HIP/DCU 工具链和设备。用 `DCU_ARCH` 指定 HIP 架构，
并用 `--backend dcu` 执行 setup/prove；gen 和 verify 是平台无关程序：

```bash
make groth16-dcu DCU_ARCH=<gfx-arch>
G16=build/groth16/bin
"$G16/groth16-gen" matmul 4
"$G16/groth16-setup" --backend dcu matmul_4.bin
"$G16/groth16-prove" --backend dcu matmul_4.bin
"$G16/groth16-verify" matmul_4.bin
```

### zk-STARK / Winterfell

矩阵乘法示例会构造 trace、生成 STARK 证明并立即验证。`THREADS` 控制
Rayon 工作线程数：

```bash
make stark-matmul THREADS=4 MATRIX_SIZE=4 PROFILE=1
```

`MATRIX_SIZE` 必须是至少为 2 的 2 次幂。`PROFILE=1` 会输出矩阵计算、trace
构造、证明生成和验证的分阶段耗时，并启用 Winterfell tracing；不启用时只
输出运行结果、总体证明/验证时间、证明大小和安全级别。证明不会默认持久化
为文件。

同一个 runner 还包含 Fibonacci、Rescue、Merkle 等例子，例如：

```bash
RAYON_NUM_THREADS=4 cargo run --release \
  --manifest-path protocols/stark/winterfell/Cargo.toml \
  -p examples --features concurrent -- fib -n 1024

protocols/stark/winterfell/target/release/winterfell --help
```

### PLONK CPU

当前最小可运行入口是 `simple_circuit`。它会在内存中完成 CRS、编译电路、
生成证明和验证：

```bash
make plonk-test
```

也可以直接选择其他 example：

```bash
cargo run --release \
  --manifest-path protocols/plonk/implementations/cpu/Cargo.toml \
  --example example_poly
```

CPU 快照中的 `matmul` example 仍使用源码内固定的矩阵和 CRS 规模，尚未
参数化，因此这里以 `simple_circuit` 作为标准运行入口。

### PLONK NVIDIA CUDA

`plonk-test-cuda` 会构建实验快照，然后运行 `simple_circuit`。该示例创建 GPU
MSM context、生成证明并在 CPU 上验证：

```bash
make plonk-test-cuda CUDA_HOME=<cuda-toolkit-path>
```

CUDA 快照的 `matmul` example 同样使用源码内固定的规模，尚未参数化。PLONK
CPU/CUDA 仍是两个独立快照；它们尚未提供跨后端共用的文件格式或统一 CLI。

### 常用参数

- `JOBS`：编译并发数，默认使用 `nproc` 返回的可用处理器数；
- `THREADS`：Winterfell/Rayon 线程数，默认 4；
- `PROFILE`：设为 `1` 时向支持的运行目标传递 `--profile`；
- `CUDA_ARCH`：NVIDIA GPU 的 CUDA compute capability，例如 A100 为 `80`；
- `DCU_ARCH`：HIP/DCU 的 offload target（`CMAKE_HIP_ARCHITECTURES`），例如工具链报告的 `gfx...`；
- `GROTH16_BUILD_DIR`：统一的 Groth16 构建目录，默认 `build/groth16`；
- `GROTH16_BIN_DIR`：四个 Groth16 程序的统一目录，默认 `build/groth16/bin`；
- `GROTH16_DATA_DIR`：Groth16 测试数据和证明产物目录，默认 `build/groth16/data`；
- `BUILD_DIR`：构建根目录，默认 `build/`。

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
