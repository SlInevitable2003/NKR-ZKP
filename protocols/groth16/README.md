# Groth16

Groth16 采用项目统一的协议目录结构：

- `app/`：与平台无关的 `gen/setup/prove/verify` 程序；
- `common/`：协议类型、I/O、校验、验证、性能分析及后端选择；
- `backends/cpu`：基于 libff/libfqfft 的参考实现；
- `backends/cuda` 和 `backends/hip`：包含 QAP、SpMV 及其他 Groth16 专属数据流的协议适配层。

BN254 域、曲线、NTT、MSM 及设备运行时代码通过顶层 `operators/cuda` 和 `operators/hip` 目标共享，不在本协议目录中重复保存。

在仓库根目录执行：

```bash
make groth16-test-cpu
make groth16-test-cuda CUDA_ARCH=80
make groth16-test-hip HIP_ARCH=<gfx-arch>
```

CPU 后端始终可用，CUDA 或 HIP 可以与其一同编译。`setup` 和 `prove` 通过 `--backend cpu|cuda|hip` 选择实际实现；`gen` 和 `verify` 仅链接协议公共层。
