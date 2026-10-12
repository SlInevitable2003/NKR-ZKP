# zk-STARK / Winterfell

`winterfell/` 来自项目的 `main` 分支，包含矩阵乘法示例。启用
`concurrent` feature 后由 Rayon 多线程执行，`RAYON_NUM_THREADS` 用于控制
工作线程数。

```bash
make stark THREADS=8
make stark-matmul THREADS=8 MATRIX_SIZE=8
```
