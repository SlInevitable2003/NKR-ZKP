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

## 各项目怎么运行

下面的命令都从仓库根目录执行。构建命令只生成程序；带 `test` 或示例名称的
命令才会真正生成并验证证明。

### Groth16

推荐先运行完整的后端冒烟测试。它会依次生成矩阵乘法 R1CS 和 witness，执行
setup、prove，最后用平台无关的 verifier 验证证明：

```bash
# CPU：默认 n=4
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4 MEMORY_MIB=4096 TIMEOUT=120

# NVIDIA GPU：CUDA_ARCH 按目标设备设置
make groth16-test-cuda GROTH16_CUDA_MATRIX_SIZE=12 CUDA_ARCH=<arch> TIMEOUT=120
```

也可以在构建后手工执行四个阶段。下面以 CPU 和 `n=4` 为例：

```bash
make groth16-cpu
G16=build/groth16-cpu/protocols/groth16
"$G16/groth16-gen" matmul 4
"$G16/groth16-setup" --backend cpu matmul_4.bin
"$G16/groth16-prove" --backend cpu matmul_4.bin
"$G16/groth16-verify" matmul_4.bin
```

生成物位于对应构建目录的 `protocols/groth16/data/`：

- `matmul_<n>.bin`：R1CS；
- `matmul_<n>.witness.bin`：满足约束的 witness；
- `*.pk.bin` / `*.vk.bin`：证明密钥和验证密钥；
- `*.proof.bin`：最终证明。

CUDA 的手工用法相同，只需使用 `build/groth16-cuda` 并把 setup/prove 的
`--backend` 改为 `cuda`。GPU 进程应通过
`scripts/run-with-limits.sh --memory-mib 0 --timeout 120 -- ...` 启动；CUDA
驱动会预留很大的虚拟地址空间，因此不能用 `ulimit -v` 限制它。

DCU 需要安装可用的 HIP/ROCm 工具链和 DCU 设备。构建后将目录换成
`build/groth16-dcu`，并用 `--backend dcu` 执行 setup/prove；gen 和 verify
仍是同一套平台无关应用：

```bash
make groth16-dcu
G16=build/groth16-dcu/protocols/groth16
"$G16/groth16-gen" matmul 4
"$G16/groth16-setup" --backend dcu matmul_4.bin
"$G16/groth16-prove" --backend dcu matmul_4.bin
"$G16/groth16-verify" matmul_4.bin
```

### zk-STARK / Winterfell

矩阵乘法示例会构造 trace、生成 STARK 证明并立即验证。`THREADS` 控制
Rayon 工作线程数：

```bash
make stark-matmul THREADS=4 MATRIX_SIZE=4 MEMORY_MIB=4096 TIMEOUT=300
```

`MATRIX_SIZE` 必须是至少为 2 的 2 次幂。顶层命令默认将测试规模限制为 16。
程序会在终端输出 trace/proof 耗时、证明大小、安全级别和验证结果，不会把
证明持久化为文件。

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
make plonk-test MEMORY_MIB=4096 TIMEOUT=300
```

也可以直接选择其他 example：

```bash
scripts/run-with-limits.sh --memory-mib 4096 --timeout 300 -- \
  cargo run --release \
  --manifest-path protocols/plonk/implementations/cpu/Cargo.toml \
  --example example_poly
```

CPU 快照中的 `matmul` example 仍使用源码内固定的矩阵和 CRS 规模，尚未接入
顶层资源保护，因此这里以 `simple_circuit` 作为标准运行入口。

### PLONK NVIDIA CUDA

`plonk-test-cuda` 会构建实验快照，然后运行 `simple_circuit`。该示例创建 GPU
MSM context、生成证明并在 CPU 上验证：

```bash
make plonk-test-cuda CUDA_HOME=<cuda-toolkit-path> TIMEOUT=300
```

CUDA 快照的 `matmul` example 同样使用源码内固定的规模，尚未接入顶层资源
保护。PLONK CPU/CUDA 仍是两个独立快照；它们尚未提供跨后端共用的文件格式
或统一 CLI。

### 常用资源参数

- `JOBS`：编译并发数，默认 2；
- `THREADS`：Winterfell/Rayon 线程数，默认 4；
- `MEMORY_MIB`：CPU 进程虚拟内存上限，默认 4096 MiB；
- `TIMEOUT`：单个受保护命令的最长秒数，默认 300；
- `CUDA_ARCH`：目标 GPU 的 CUDA compute capability；
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
