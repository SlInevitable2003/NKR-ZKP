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

## 构建与测试

请在仓库根目录执行以下命令：

```bash
make help
make check

# Groth16 CPU 端到端流程
make groth16-test-cpu GROTH16_CPU_MATRIX_SIZE=4

# NVIDIA CUDA；请使用当前 GPU 对应的计算能力
make groth16-test-cuda CUDA_ARCH=80 GROTH16_CUDA_MATRIX_SIZE=12

# HIP；请使用当前工具链报告的 offload target
make groth16-test-hip HIP_ARCH=<gfx-arch> GROTH16_HIP_MATRIX_SIZE=12

# 仅构建 PLONK 框架目标（目前不会生成证明）
make plonk-scaffold

# Winterfell 示例
make stark-matmul THREADS=4 MATRIX_SIZE=4
```

`groth16-dcu` 和 `groth16-test-dcu` 是兼容性别名，对应规范名称 `groth16-hip`。运行时通过 `--backend cpu|cuda|hip` 选择后端；为了兼容旧用法，也接受 `dcu` 作为别名。

所有协议共用以下 CMake 加速器选项：

```text
-DNKR_ZKP_ACCELERATOR=NONE|CUDA|HIP
```

同一个构建目录中只能启用一种加速器工具链，所有配置始终保留 CPU 支持。在 CUDA 和 HIP 之间切换前，需要删除或重新配置共用的 CMake 缓存。

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
