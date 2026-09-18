# 阶段 5：Zynq PS 端 CNN 推理

## 实现范围

固定模型为 `tests/upstream_mnist/mnist.tflite`，SHA-256 为
`b0ae6afa9e9bceed8ae3038c0a844634eddc24f30f262c5b7e5ae800db2db50d`。
`tests/export_digit_weights.py` 验证摘要后，从原 FlatBuffer 按原始 float32 位模式导出
`digit_weights.c/.h`。PS 使用 `digit_model.c` 运行同一计算图：
`CAST(uint8→float32) → EXPAND_DIMS → VALID CONV_2D+ReLU(32) →
VALID CONV_2D+ReLU(64) → 2×2 MAX_POOL → RESHAPE → FULLY_CONNECTED(10)
→ SOFTMAX`。输入为原始 0–255 灰度值，不除以 255。

推理在阶段 4 的预处理返回 `DIGIT_FOUND` 后执行；采样频率为每 5 个成功复制的 ROI
处理一次。推理为同步调用。两个静态激活缓冲区合计 233,984 字节；权重是只读常量，
无需动态分配。`digit_model_infer` 对空指针、非有限 logits、Softmax 异常返回错误码。

## 构建和自动测试

在仓库根目录执行：

```powershell
& tests/build_stage5.ps1
& tests/build_stage5_arm.ps1
& 'E:/xilinx/Vitis/2020.2/bin/bootgen.bat' -arch zynq -image artifacts/stage5/boot.bif -o artifacts/stage5/BOOT.BIN -w on
```

主机测试使用同一 C 推理代码和原模型的 110 份黄金输入（100 份 MNIST、10 份纸张图片）：
逐份核对 10 类分数、概率和、最大类及有限值。接受阈值为每类绝对误差
`≤ 5e-4`。本次结果：110/110 分类一致，最大绝对误差 `1.3114205e-6`
（`mnist_00073` 的第 9 类）。ARM 交叉编译和 Bootgen 均通过。
ELF `_end=0x00227100`，RGB 帧缓冲从 `0x01000000` 开始。

交付镜像：`artifacts/stage5/BOOT.BIN`。该镜像复用阶段 3 已生成的 FSBL 和 PL bitstream，
阶段 5 仅更新 PS 应用。权重由固定模型导出；更新模型时须先重新做摘要、算子和黄金结果核验。

## 板上测试步骤（需连接开发板执行）

1. 用阶段 5 `BOOT.BIN` 启动，核对串口打印的模型 SHA、算子列表和 233,984 字节工作区；检查相机与 LCD 仍正常。
2. 依次展示 0–9 数字、空白背景和噪声背景；记录 `CNN digit`、置信度和预处理状态。空白应为 `DIGIT_NONE`，不触发新推理。
3. 串口按 `g`，将包含相邻 `tensor28=` 和 `scores_ppm=` 两行的完整串口记录保存为 UTF-8 文本；执行：

   ```powershell
   & tests/.venv/Scripts/python.exe tests/compare_uart_inference.py <串口日志路径>
   ```

   脚本对每组抓取在 PC 原 TFLite 上重新推理，要求分类一致、分数最大绝对误差 `≤ 5e-4`。
   `p` 单独打印最新张量，`v` 单独打印最新分数，`s` 打印处理数量、错误和推理耗时。
4. 连续运行至少 1000 次识别，记录错误数、丢帧数、平均/P95/最大推理时间和静态内存用量；检查显示不卡死、VDMA 不持续报错。若同步推理妨碍采集，调大 `PREPROCESS_EVERY_N_FRAMES`，重新量测。

当前无开发板接入，因此**板上输出对齐、准确率、时延、长时间稳定性尚未验证**，不能以主机测试代替这些验收项。叠加显示属于下一阶段。
