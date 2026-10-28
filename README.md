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
- PLONK 已实现独立的三线 Plonkish 算术化 Manager、SHA3-256 Transcript，以及 CPU Setup、Prove 和 Verify；电路、见证、SRS、PK/VK 与证明均通过共享 `BinaryArchive` 读写。CUDA/HIP 的 PLONK 协议适配仍是框架。
- 原有的 Rust PLONK CPU/CUDA 代码已删除。新实现计划以 `docs/plonk_protocol_low_level.pdf` 描述的 BN254、KZG 方案为准。
- Winterfell 仍作为独立的 Rust CPU 实现保留。

## Groth16 构建与运行

Groth16 始终生成同一组可执行程序：

```text
build/groth16/bin/groth16-gen
build/groth16/bin/groth16-setup
build/groth16/bin/groth16-prove
build/groth16/bin/groth16-verify
```

不存在 `groth16-prove-cpu`、`groth16-prove-cuda` 或 `groth16-prove-hip` 之类的独立程序。`make groth16-cpu`、`make groth16-cuda` 和 `make groth16-hip` 只是决定把哪些后端实现编入上述统一程序；运行 `setup` 和 `prove` 时，再通过 `--backend` 选择实际使用的后端。

- CPU 配置支持 `--backend cpu`。
- CUDA 配置同时支持 `--backend cpu` 和 `--backend cuda`。
- HIP 配置同时支持 `--backend cpu` 和 `--backend hip`；`dcu` 是 `hip` 的兼容性名称。

由于 CUDA 和 HIP 使用不同的平台工具链，一个构建目录只选择其中一种加速器。切换构建配置前执行 `make groth16-clean`，重新生成的仍然是上面同名、同接口的四个程序。

以下示例均在仓库根目录执行，并以规模为 100 的矩阵乘法电路为例。

### 使用 CPU 后端

```bash
make groth16-clean
make groth16-cpu

G16=build/groth16/bin
"$G16/groth16-gen" matmul 100
"$G16/groth16-setup" --backend cpu matmul_100.bin
"$G16/groth16-prove" --backend cpu matmul_100.bin
"$G16/groth16-verify" matmul_100.bin
```

### 使用 NVIDIA CUDA 后端

`CUDA_ARCH` 应设置为目标 GPU 的计算能力，例如 `80` 表示 compute capability 8.0。CUDA 默认位于 `/usr/local/cuda`；安装在其他位置时可额外传入 `CUDA_HOME=/path/to/cuda`。

```bash
make groth16-clean
make groth16-cuda CUDA_ARCH=80

G16=build/groth16/bin
"$G16/groth16-gen" matmul 100
"$G16/groth16-setup" --backend cuda matmul_100.bin
"$G16/groth16-prove" --backend cuda matmul_100.bin
"$G16/groth16-verify" matmul_100.bin
```

### 使用 HIP/ROCm 后端

构建前需要确保 CMake 能够找到 HIP/ROCm 工具链。`HIP_ARCH` 应设置为目标 GPU 的 offload target，例如工具链报告的 `gfx...` 值。当前 HIP 后端的 `setup` 在内部委托给 CPU 参考实现，`prove` 使用 HIP 加速；外部仍使用相同的统一接口。

```bash
make groth16-clean
make groth16-hip HIP_ARCH=<gfx-arch>

G16=build/groth16/bin
"$G16/groth16-gen" matmul 100
"$G16/groth16-setup" --backend hip matmul_100.bin
"$G16/groth16-prove" --backend hip matmul_100.bin
"$G16/groth16-verify" matmul_100.bin
```

`gen` 和 `verify` 是后端无关的公共程序，不接收 `--backend`。电路、见证、密钥和证明文件默认存放在 `build/groth16/data`。可为任意命令添加 `--profile` 以输出支持的阶段耗时。

如果只需要一条命令完成构建和端到端运行，可使用：

```bash
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=100
make groth16-test-cuda CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=100
make groth16-test-hip HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=100
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

当前可完整运行的是 CPU 后端。CUDA/HIP 目录已经保留统一的后端接口，但 PLONK 协议适配尚未实现，因此目前不要向 PLONK 的 `setup` 或 `prove` 传入 `--backend cuda` / `--backend hip`。这不影响 Groth16 使用已有的 CUDA/HIP 后端。

以下命令在仓库根目录构建 PLONK，并完整运行一个规模为 100 的矩阵乘法电路：

```bash
make plonk-cpu
PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" matmul 100
"$PLONK_BIN/plonk-setup" --backend cpu matmul_100.bin
"$PLONK_BIN/plonk-prove" --backend cpu matmul_100.bin
"$PLONK_BIN/plonk-verify" matmul_100.bin
```

也可以生成 Fibonacci 电路，并显式复用已有的 SRS：

```bash
PLONK_BIN=build/plonk/bin

"$PLONK_BIN/plonk-gen" fibonacci 10
"$PLONK_BIN/plonk-setup" --backend cpu \
  --srs matmul_100.srs.bin fibonacci_10.bin
"$PLONK_BIN/plonk-prove" --backend cpu fibonacci_10.bin
"$PLONK_BIN/plonk-verify" fibonacci_10.bin
```

PLONK 使用独立的 `build/plonk` 构建树，产物默认位于 `build/plonk/data`。与 Groth16 一样，`setup`、`prove` 和 `verify` 可用 `--path <dir>` 覆盖默认产物目录；使用默认目录时无需传入。四个程序都可添加 `--profile` 输出阶段耗时。快速构建、端到端测试和分层测试命令如下：

```bash
make plonk-cpu
make plonk-test-cpu
make plonk-test-arithmetization
make plonk-test-cpu-setup
make plonk-test-cpu-prove-verify
```

自动生成的 SRS 仅用于本地开发和功能测试，不是可用于生产环境的多方可信设置。更详细的算术化、Transcript、文件格式和 CPU 协议实现说明见 [protocols/plonk/README.md](protocols/plonk/README.md)。

## 分层规则

- `app/` 只负责解析命令和组织文件流程，入口实现直接放在该目录，不设置空的 `include/`、`src/` 或 `backends/` 子层级。
- `protocols/*/common` 负责公共协议类型、Transcript/I/O 规则、验证以及与后端无关的语义。
- `protocols/*/backends` 负责协议专属的加速数据流和融合内核。
- `operators/*` 负责与协议无关的域运算、曲线运算、NTT、MSM、内存管理，以及未来可加入的扫描和批量求逆等基础算子。
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
