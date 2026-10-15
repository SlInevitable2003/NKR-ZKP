# Groth16

`common/` 保存平台无关的数据结构；`backends/cuda` 与 `backends/dcu` 分别保存 NVIDIA CUDA 和 HIP/DCU 实现。

依赖源码已经 vendored 到 `third_party/depends`，不需要原来的 `~/lightZK`。
从仓库根目录运行完整 CPU/CUDA 流程：

```bash
make groth16-test-cpu
make groth16-test-cuda CUDA_ARCH=120
make groth16-test-dcu DCU_ARCH=<gfx-arch>
```

应用层固定为 `groth16-gen`、`groth16-setup`、`groth16-prove` 和
`groth16-verify`，统一生成到 `build/groth16/bin`。CPU 始终编入，
构建时可以再选择 CUDA 或 DCU 加速器；setup/prove 通过
`--backend cpu|cuda|dcu` 从已编入的后端中选择。gen/verify 只链接公共协议层。
完整的手工命令、产物说明及资源限制见仓库根目录的 `README.md`。
