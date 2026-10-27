# NKR-ZKP

NKR-ZKP 是一个实验性的多协议、多后端零知识证明框架。当前项目将协议语义与可复用的计算算子分离：

```text
operators/
  cuda/                    共享的 BN254 CUDA 域、曲线、NTT、MSM 算子
  hip/                     共享的 BN254 HIP 域、曲线、NTT、MSM 算子

protocols/
  groth16/
    app/                   gen/setup/prove/verify 命令行程序
    common/                协议类型、I/O、验证及后端注册
    backends/{cpu,cuda,hip}
  plonk/
    app/                   gen/setup/prove/verify 占位命令行程序
    common/                公共类型、API 及后端注册框架
    backends/{cpu,cuda,hip}
  stark/winterfell/

third_party/lightzk/       内置的 libsnark/libff/libfqfft/CGBN 源码快照
docs/                      架构、源码来源及协议文档
```

CUDA 和 HIP 算子由 Groth16 与 PLONK 共用。协议专属逻辑保留在各自的协议后端中：Groth16 负责 R1CS、QAP 和 SpMV，PLONK 将负责置换、商多项式、线性化与打开证明等逻辑。CUDA 与 HIP 实现相同的算子边界，但分别保留独立内核，以便针对各平台单独优化和审计。

## 当前状态

- Groth16 已有可用的 CPU、CUDA 和 HIP 后端。CPU 后端始终参与构建；配置时可以选择 CUDA 或 HIP 加速后端。
- PLONK 目前只有 C++ 接口和目录框架。相关目标可以编译，但由于尚未实现协议语义，命令行程序会按设计以状态码 2 退出。
- 原有的 Rust PLONK CPU/CUDA 代码已删除。新实现计划以 `docs/plonk_protocol_low_level.pdf` 描述的 BN254、KZG 方案为准。
- Winterfell 仍作为独立的 Rust CPU 实现保留。

## Groth16 构建与测试

Groth16 需要 CMake 3.21 及以上版本、支持 C++17 的编译器和 OpenMP。CUDA 构建还需要 NVIDIA CUDA Toolkit，HIP 构建需要可被 CMake 检测到的 HIP/ROCm 工具链。

以下命令均在仓库根目录执行。“构建”目标只进行配置和编译，“测试”目标会在构建后继续执行 `gen/setup/prove/verify` 端到端流程。

| 后端 | 仅构建 | 构建并运行端到端测试 | 构建中可用的运行时后端 |
|---|---|---|---|
| CPU | `make groth16-cpu` | `make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4` | `cpu` |
| NVIDIA CUDA | `make groth16-cuda CUDA_ARCH=80` | `make groth16-test-cuda CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=12` | `cpu`、`cuda` |
| HIP/ROCm | `make groth16-hip HIP_ARCH=<gfx-arch>` | `make groth16-test-hip HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=12` | `cpu`、`hip` |

CUDA 的 `CUDA_ARCH` 必须与目标 GPU 的计算能力匹配，例如 `80` 表示 compute capability 8.0。CUDA 默认从 `/usr/local/cuda` 查找，也可以通过 `CUDA_HOME` 指定：

```bash
make groth16-cuda CUDA_HOME=/opt/cuda CUDA_ARCH=80
```

HIP 构建前应确保 `hipcc` 及对应 ROCm 工具链可被当前环境找到。`HIP_ARCH` 使用目标 GPU 的 offload target，例如工具链报告的 `gfx...` 值：

```bash
make groth16-hip HIP_ARCH=<gfx-arch>
```

CPU 后端始终会编译；每个构建目录最多再包含 CUDA 或 HIP 中的一种。为了避免 CMake 缓存互相影响，建议为不同后端使用独立目录：

```bash
make groth16-cpu  GROTH16_BUILD_DIR=build/groth16-cpu
make groth16-cuda GROTH16_BUILD_DIR=build/groth16-cuda CUDA_ARCH=80
make groth16-hip  GROTH16_BUILD_DIR=build/groth16-hip HIP_ARCH=<gfx-arch>
```

如果复用默认的 `build/groth16`，切换加速器前先执行 `make groth16-clean`。底层对应的 CMake 选项为：

```text
-DNKR_ZKP_ACCELERATOR=NONE|CUDA|HIP
```

`groth16-dcu` 和 `groth16-test-dcu` 是 `groth16-hip` 和 `groth16-test-hip` 的兼容性别名。程序运行时通过 `--backend cpu|cuda|hip` 选择后端；`dcu` 也可作为 `hip` 的兼容性名称。

其他项目目标：

```bash
make help
make check
make plonk-scaffold
make stark-matmul THREADS=4 MATRIX_SIZE=4
```

## Groth16 手动运行流程

四个程序默认生成在 `build/groth16/bin`：

```bash
make groth16-cpu
G16=build/groth16/bin
"$G16/groth16-gen" matmul 4
"$G16/groth16-setup" --backend cpu matmul_4.bin
"$G16/groth16-prove" --backend cpu matmul_4.bin
"$G16/groth16-verify" matmul_4.bin
```

添加 `--profile` 可以输出协议层及后端各阶段的耗时。除非另行指定数据目录，生成的电路、见证、密钥和证明文件都存放在 `build/groth16/data`。

### CUDA 手动流程

```bash
make groth16-cuda GROTH16_BUILD_DIR=build/groth16-cuda CUDA_ARCH=80
G16=build/groth16-cuda/bin
"$G16/groth16-gen" matmul 100
"$G16/groth16-setup" --backend cuda matmul_100.bin
"$G16/groth16-prove" --backend cuda matmul_100.bin
"$G16/groth16-verify" matmul_100.bin
```

### HIP 手动流程

```bash
make groth16-hip GROTH16_BUILD_DIR=build/groth16-hip HIP_ARCH=<gfx-arch>
G16=build/groth16-hip/bin
"$G16/groth16-gen" matmul 100
"$G16/groth16-setup" --backend hip matmul_100.bin
"$G16/groth16-prove" --backend hip matmul_100.bin
"$G16/groth16-verify" matmul_100.bin
```

`gen` 和 `verify` 属于后端无关的公共层，因此不接收 `--backend`。`setup` 和 `prove` 根据 `--backend` 选择 CPU、CUDA 或 HIP；当前 HIP 的 `setup` 会委托给 CPU 参考实现，HIP 加速用于 `prove`。

## 分层规则

- `app/` 只负责解析命令和组织文件流程。
- `protocols/*/common` 负责公共协议类型、Transcript/I/O 规则、验证以及与后端无关的语义。
- `protocols/*/backends` 负责协议专属的加速数据流和融合内核。
- `operators/*` 负责与协议无关的域运算、曲线运算、NTT、MSM、内存管理，以及未来可加入的扫描和批量求逆等基础算子。
- 公共协议头文件不得暴露 `cudaStream_t`、`hipStream_t` 或设备指针。
- 一个协议不得包含另一个协议的后端头文件。

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
