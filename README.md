# NKR-ZKP

NKR-ZKP 是一个实验性的多协议、多后端零知识证明框架。当前项目将协议语义与可复用的计算算子分离：

```text
common/
  include/                 共享 BinaryArchive、Profile 等主机侧工具
  src/                     共享主机侧工具实现

operators/
  cuda/                    共享的 BN254 CUDA 域、曲线、NTT、MSM 算子
  hip/                     共享的 BN254 HIP 域、曲线、NTT、MSM 算子

protocols/
  groth16/
    app/                   仅含 gen/setup/prove/verify 的 .cpp 入口
    common/                协议类型、I/O、验证及后端注册
    backends/{cpu,cuda,hip}
  plonk/
    app/                   仅含统一的 gen/setup/prove/verify .cpp 入口
    common/                算术化、协议类型、I/O、API 及后端注册
    backends/{cpu,cuda,hip}
  stark/winterfell/

third_party/lightzk/       内置的 libsnark/libff/libfqfft/CGBN 源码快照
docs/                      架构、源码来源及协议文档
```

CUDA 和 HIP 算子由 Groth16 与 PLONK 共用。协议专属逻辑保留在各自的协议后端中：Groth16 负责 R1CS、QAP 和 SpMV，PLONK 负责置换、商多项式、线性化与打开证明等逻辑。CUDA 与 HIP 实现相同的算子边界，但分别保留独立内核，以便针对各平台单独优化和审计。

## 当前状态

- Groth16 已有可用的 CPU、CUDA 和 HIP 后端。CPU 后端始终参与构建；配置时可以选择 CUDA 或 HIP 加速后端。
- PLONK 已实现独立的三线 Plonkish 算术化 Manager、SHA3-256 Transcript、CPU Setup/Prove/Verify、CUDA Prove 和面向 DCU 的 HIP Prove；电路、见证、SRS、PK/VK 与证明均通过共享 `BinaryArchive` 读写。CUDA/HIP Setup 复用 CPU Setup。
- Winterfell 仍作为独立的 Rust 实现保留，支持在 CPU、NVIDIA GPU 和 DCU 平台环境中构建与运行。

## Groth16 构建与运行

Groth16 始终生成同一组可执行程序：

```text
build/groth16/bin/groth16-gen
build/groth16/bin/groth16-setup
build/groth16/bin/groth16-prove
build/groth16/bin/groth16-verify
```

- CPU 配置支持 `--backend cpu`。
- CUDA 配置同时支持 `--backend cpu` 和 `--backend cuda`。
- HIP 配置同时支持 `--backend cpu` 和 `--backend hip`；`dcu` 是 `hip` 的兼容性名称。

以下示例均在仓库根目录执行，并以规模为 100 的矩阵乘法电路为例。

### 使用 CPU 后端

```bash
make groth16-clean
make groth16-cpu

G16=build/groth16/bin
"$G16/groth16-gen" --profile matmul 100
"$G16/groth16-setup" --profile --backend cpu matmul_100.bin
"$G16/groth16-prove" --profile --backend cpu matmul_100.bin
"$G16/groth16-verify" --profile matmul_100.bin
```

### 使用 NVIDIA CUDA 后端

`CUDA_ARCH` 应设置为目标 GPU 的计算能力，例如 `80` 表示 compute capability 8.0。CUDA 默认位于 `/usr/local/cuda`；安装在其他位置时可额外传入 `CUDA_HOME=/path/to/cuda`。

```bash
make groth16-clean
make groth16-cuda CUDA_ARCH=80

G16=build/groth16/bin
"$G16/groth16-gen" --profile matmul 100
"$G16/groth16-setup" --profile --backend cuda matmul_100.bin
"$G16/groth16-prove" --profile --backend cuda matmul_100.bin
"$G16/groth16-verify" --profile matmul_100.bin
```

### 使用 HIP/ROCm 后端

构建前需要确保 CMake 能够找到 HIP/ROCm 工具链。`HIP_ARCH` 应设置为目标 DCU 的 offload target，例如工具链报告的 `gfx...` 值。HIP 后端的 Setup 和 Prove 均使用设备实现；外部仍使用相同的统一接口。

```bash
make groth16-clean
make groth16-hip HIP_ARCH=<gfx-arch>

G16=build/groth16/bin
"$G16/groth16-gen" --profile matmul 100
"$G16/groth16-setup" --profile --backend hip matmul_100.bin
"$G16/groth16-prove" --profile --backend hip matmul_100.bin
"$G16/groth16-verify" --profile matmul_100.bin
```

`gen` 和 `verify` 是后端无关的公共程序，不接收 `--backend`。电路、见证、密钥和证明文件默认存放在 `build/groth16/data`。上述命令默认展示 `--profile`，删去该选项即可关闭阶段耗时输出。

Groth16 与 PLONK 使用相同的证明计时分层：`*.prove.program_total` 是包含文件读写、校验和 prepare 的程序端到端时间；`*.prove.prepare_backend` 单独包含设备布局、NTT 幂表等与 witness 无关的预计算以及 PK 上传；`*.prove.total` 只是在线证明生成时间。Groth16 的该项包括 raw proof 和最终盲化，但不包括 prepare 与证明写盘。CUDA 和 HIP 后端在这三层上保持相同的封装。

如果只需要一条命令完成构建和端到端运行，可使用：

```bash
make groth16-test-cpu PROFILE=1 GROTH16_CPU_MATRIX_SIZE=100
make groth16-test-cuda PROFILE=1 CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=100
make groth16-test-hip PROFILE=1 HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=100
```

`groth16-dcu` 和 `groth16-test-dcu` 分别是 HIP 构建与测试目标的兼容性别名。

## PLONK 构建与运行

PLONK 与 Groth16 采用相同的四阶段程序结构：

```text
build/plonk/bin/plonk-gen
build/plonk/bin/plonk-setup
build/plonk/bin/plonk-prove
build/plonk/bin/plonk-verify
```

`gen` 负责产生电路与见证，`setup` 读取电路并产生 SRS、PK 和 VK，`prove` 读取 PK 与见证产生证明，`verify` 读取 VK、公开输入和证明完成验证。只有 `setup` 与 `prove` 接收 `--backend`；`gen` 和 `verify` 是后端无关的公共程序。

CPU 后端可完整运行。CUDA 和 HIP 构建都同时包含 CPU 后端与对应的加速后端：Setup 通过统一的加速后端入口复用 CPU 实现，并将后续证明需要的 NTT 幂表写入 PK；`plonk-prove` 内部先用独立 prepare 阶段上传所有与输入无关的数据，该阶段不计入正式 Prove，然后在设备上完成 NTT、MSM、逐点域运算、逐点求逆、前缀乘积、多项式求值和综合除法；Transcript 和挑战生成保留在 CPU。

HIP 实现以 DCU 兼容性为优先：继续使用无硬件进位链假设的朴素大整数运算；Fp2、MSM、scan、NTT 和多项式算子不依赖 warp shuffle 或 cooperative groups。`make check` 会静态检查这些限制。

### 使用 CPU 后端

以下命令在仓库根目录构建 PLONK，并完整运行一个规模为 79 的矩阵乘法电路：

```bash
make plonk-cpu
PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" --profile matmul 79
"$PLONK_BIN/plonk-setup" --profile --backend cpu matmul_79.bin
"$PLONK_BIN/plonk-prove" --profile --backend cpu matmul_79.bin
"$PLONK_BIN/plonk-verify" --profile matmul_79.bin
```

### 使用 NVIDIA CUDA 后端

CUDA 配置仍生成同名的四个程序，不会生成后端专属可执行文件。完整命令如下：

```bash
make plonk-cuda CUDA_ARCH=80
PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" --profile matmul 79
"$PLONK_BIN/plonk-setup" --profile --backend cuda matmul_79.bin
"$PLONK_BIN/plonk-prove" --profile --backend cuda matmul_79.bin
"$PLONK_BIN/plonk-verify" --profile matmul_79.bin
```

这里的 `plonk-setup --backend cuda` 在内部调用 CPU Setup；Setup 不使用 GPU 生成 SRS 或固定多项式，但会在 CPU 上生成 NTT 幂表并写入 PK。选择 CUDA 后端的意义是保持四阶段接口一致，并让随后读取同一 PK 的 Prove 使用 GPU。PK 格式包含这些预计算数据，更新程序后需要重新运行 Setup，不能复用旧 PK。

### 使用 HIP/DCU 后端

HIP 配置同样生成统一的四个程序。Setup 复用 CPU 实现，Prove 使用 DCU 安全的 HIP 算子；`HIP_ARCH` 取目标机器工具链报告的 `gfx...` offload target。

```bash
make plonk-clean
make plonk-hip HIP_ARCH=<gfx-arch>
PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" --profile matmul 79
"$PLONK_BIN/plonk-setup" --profile --backend hip matmul_79.bin
"$PLONK_BIN/plonk-prove" --profile --backend hip matmul_79.bin
"$PLONK_BIN/plonk-verify" --profile matmul_79.bin
```

包含共享 HIP 算子对照测试和端到端证明验证的回归命令为：

```bash
make plonk-test-hip PROFILE=1 HIP_ARCH=<gfx-arch> PLONK_HIP_MATRIX_SIZE=79
```

`plonk-dcu` 和 `plonk-test-dcu` 是对应 HIP 目标的兼容性别名。

也可以生成 Fibonacci 电路，并显式复用已有的 SRS：

```bash
PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" --profile fibonacci 10
"$PLONK_BIN/plonk-setup" --profile --backend cpu \
  --srs matmul_79.srs.bin fibonacci_10.bin
"$PLONK_BIN/plonk-prove" --profile --backend cpu fibonacci_10.bin
"$PLONK_BIN/plonk-verify" --profile fibonacci_10.bin
```

PLONK 使用独立的 `build/plonk` 构建树，产物默认位于 `build/plonk/data`。与 Groth16 一样，`setup`、`prove` 和 `verify` 可用 `--path <dir>` 覆盖默认产物目录；使用默认目录时无需传入。四个程序都可添加 `--profile` 输出阶段耗时。CPU Setup 的 FixMSM 与 Setup 专用 MSM 在调用层使用 OpenMP，可通过 `OMP_NUM_THREADS=<线程数>` 控制；CPU Prove 保持单线程，且项目不会开启 libff/libfqfft 的全局 `MULTICORE`。快速构建、端到端测试和分层测试命令如下：

```bash
make plonk-cpu
make plonk-test-cpu PROFILE=1 PLONK_CPU_MATRIX_SIZE=79
make plonk-cuda CUDA_ARCH=120
make plonk-test-cuda PROFILE=1 CUDA_ARCH=120 PLONK_CUDA_MATRIX_SIZE=79
make plonk-hip HIP_ARCH=<gfx-arch>
make plonk-test-hip PROFILE=1 HIP_ARCH=<gfx-arch> PLONK_HIP_MATRIX_SIZE=79
make plonk-test-arithmetization
make plonk-test-cpu-setup
make plonk-test-cpu-prove-verify
```

更详细的算术化、Transcript、文件格式和 CPU 协议实现说明见 [protocols/plonk/README.md](protocols/plonk/README.md)。

## zk-STARK / Winterfell 构建与运行

zk-STARK 相关代码位于 `protocols/stark/winterfell`，是一个独立的 Rust
workspace。它包含 AIR、有限域与 FFT、Merkle 承诺、FRI、Prover、Verifier
以及示例程序。与 Groth16 和 PLONK 不同，STARK 不需要可信设置；示例程序会在
一次运行中构造执行轨迹、生成证明、完成序列化往返并验证证明。

当前 Winterfell 集成已同样支持在 CPU、NVIDIA GPU 和 DCU 平台环境中构建与运行。
构建时启用 `concurrent` feature，由 Rayon 并行调度证明任务；`THREADS` 通过
`RAYON_NUM_THREADS` 控制工作线程数。构建需要 Rust 1.87 和
Cargo，工具链版本记录在 `protocols/stark/winterfell/rust-toolchain.toml`。

在仓库根目录构建 release 版本：

```bash
make stark THREADS=8
```

运行矩阵乘法证明与验证示例：

```bash
make stark-matmul THREADS=8 MATRIX_SIZE=8 PROFILE=1
```

`MATRIX_SIZE` 必须是不小于 2 的 2 的幂。`PROFILE=1` 会额外输出输入生成、
矩阵计算、执行轨迹构造、证明生成和验证等阶段耗时。生成的示例程序位于：

```text
protocols/stark/winterfell/target/release/winterfell
```

也可以直接查看和运行 Winterfell 自带示例：

```bash
protocols/stark/winterfell/target/release/winterfell --help
RAYON_NUM_THREADS=8 \
  protocols/stark/winterfell/target/release/winterfell \
  matrix-mul -n 8 --profile
```

示例程序还包含 Fibonacci、乘法 Fibonacci、VDF、Rescue hash chain、
随机化 AIR permutation、Merkle membership 和 Lamport+ signature 等 AIR。
可通过 `--hash_fn`、`--queries`、`--blowup`、`--grinding`、
`--field_extension` 与 `--folding` 调整证明参数；这些参数会影响证明时间、
证明大小和安全级别。

更详细的组件和命令说明见[protocols/stark/README.md](protocols/stark/README.md)。

## 分层规则

- `app/` 只负责解析命令和组织文件流程，入口实现直接放在该目录，不设置空的 `include/`、`src/` 或 `backends/` 子层级。
- `protocols/*/common` 负责公共协议类型、Transcript/I/O 规则、验证以及与后端无关的语义。
- `protocols/*/backends` 负责协议专属的加速数据流和融合内核。
- `operators/*` 负责与协议无关的域运算、曲线运算、NTT、MSM、内存管理、扫描、逐点求逆、多项式求值和综合除法等基础算子。
- 公共协议头文件不得暴露 `cudaStream_t`、`hipStream_t` 或设备指针。
- 一个协议不得包含另一个协议的后端头文件。
- `common/`、`operators/` 和 `protocols/` 下不保留空的占位目录；新增目录必须同时承载实际源码或头文件。

详细的依赖方向与迁移约束请参阅 [docs/architecture.md](docs/architecture.md)，导入源码的来源请参阅 [docs/provenance.md](docs/provenance.md)。

## 常用变量

- `JOBS`：构建并行度，默认为 `nproc`；
- `PROFILE=1`：启用支持的性能分析输出；
- `CUDA_ARCH`：NVIDIA 计算能力；
- `HIP_ARCH`：HIP offload target；
- `LIGHTZK_DEPS_DIR`：内置依赖根目录，默认为 `third_party/lightzk/depends`；
- `GROTH16_BUILD_DIR`：CMake 构建目录，默认为 `build/groth16`；
- `GROTH16_BIN_DIR`：可执行文件目录，默认为 `build/groth16/bin`；
- `GROTH16_DATA_DIR`：Groth16 产物目录，默认为 `build/groth16/data`。
- `PLONK_BUILD_DIR`：PLONK CMake 构建目录，默认为 `build/plonk`；
- `PLONK_BIN_DIR`：PLONK 可执行文件目录，默认为 `build/plonk/bin`；
- `PLONK_DATA_DIR`：PLONK 产物目录，默认为 `build/plonk/data`。
- `PLONK_CPU_MATRIX_SIZE`：`plonk-test-cpu` 使用的矩阵规模，默认为 `2`。
- `PLONK_CUDA_MATRIX_SIZE`：`plonk-test-cuda` 使用的矩阵规模，默认为 `10`。
- `PLONK_HIP_MATRIX_SIZE`：`plonk-test-hip` 使用的矩阵规模，默认为 `10`。
- `THREADS`：Winterfell/Rayon 工作线程数，默认为 `4`；
- `MATRIX_SIZE`：Winterfell 矩阵乘法示例规模，默认为 `4`，必须是不小于 2 的 2 的幂。
