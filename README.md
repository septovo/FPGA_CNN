# FPGA_CNN

Zynq-7020 双 OV5640 硬件工程的手写数字识别改造项目。目前保留 **CAM0 → VDMA → RGB LCD** 的单摄像头直通基线；已完成所选数字识别模型的 PC 侧基准测试。PL 图像预处理、PS 裸机推理和 LCD 结果叠加仍按实施路径开发，详见[完整实现路径](手写数字识别系统_完整实现路径.md)。

## 工程内容

| 路径 | 内容 |
|---|---|
| `dual_ov5640_lcd.xpr`、`dual_ov5640_lcd.srcs/` | Vivado 2020.2 工程入口、Block Design、约束和 IP 配置。 |
| `ip_repo/` | OV5640 采集、RGB LCD 自定义 IP 源码。 |
| `vitis/dual_ov5640_lcd/src/` | Vitis 2020.2 裸机 C 应用源码。 |
| `vitis/system_wrapper.xsa` | 当前硬件导出，用于 Vitis platform；后续 PL 改动后需重新导出。 |
| `tests/upstream_mnist/` | 固定版本的第三方模型、原始测试图片、参考脚本与 MIT 许可证。 |
| `tests/*.py`、`tests/results/` | PC 模型检查、完整 MNIST 评估、黄金输入与结果。 |
| `docs/` | 阶段记录与测试报告。 |

## 当前结果

模型来自 [alankrantas/MNIST-Live-Detection-TFLite](https://github.com/alankrantas/MNIST-Live-Detection-TFLite)，固定提交 `0546580c7148366bf0dded55d746298b410c14d5`。模型 SHA-256 为 `b0ae6afa9e9bceed8ae3038c0a844634eddc24f30f262c5b7e5ae800db2db50d`。PC 端在 MNIST 10,000 张测试图上的结果是 **9,897/10,000（98.97%）**；详情见[阶段 1 报告](docs/阶段1_模型基准与PC黄金样本.md)。该结果不是 OV5640 实拍场景的准确率。

## PC 基准复现

推荐 Windows Python 3.12；依赖见 `tests/requirements-pc.txt`。MNIST 原始数据未纳入仓库，需将 [`mnist.npz`](https://storage.googleapis.com/tensorflow/tf-keras-datasets/mnist.npz) 下载为 `tests/mnist.npz`，并核对 SHA-256：`731c5ac602752760c8e48fbffcf8c3b850d9dc2a2aedcf2cc48468fc17b673d1`。

```powershell
python -m venv tests/.venv
tests/.venv/Scripts/python.exe -m pip install -r tests/requirements-pc.txt
tests/.venv/Scripts/python.exe tests/model_inspect.py --model tests/upstream_mnist/mnist.tflite --output tests/results/model_inspection.json
tests/.venv/Scripts/python.exe tests/evaluate_mnist.py --model tests/upstream_mnist/mnist.tflite --mnist-npz tests/mnist.npz --output tests/results/mnist_full_eval.json
tests/.venv/Scripts/python.exe tests/golden_inference.py --model tests/upstream_mnist/mnist.tflite --output-dir tests/results/golden --mnist-npz tests/mnist.npz --mnist-count 100 --image tests/upstream_mnist/test.jpg
```

## 硬件工程重建

1. 用 Vivado 2020.2 打开 `dual_ov5640_lcd.xpr`。工程文件保留了原机器的历史导入路径；若 Vivado 报找不到文件，重新指定本仓库 `dual_ov5640_lcd.srcs/` 的 BD/约束及 `ip_repo/` 的 Repository 路径。
2. Validate Design，生成 IP Output Products、综合、实现和 bitstream；重新导出 XSA。
3. 在 Vitis 2020.2 由 XSA 建立/更新 platform，导入 `vitis/dual_ov5640_lcd/src/` 应用源码，并选择 `ps7_cortexa9_0` standalone BSP。当前 `main.c` 是单摄像头直通显示版本。

仓库排除了 Vivado 缓存、实现结果、Vitis 临时平台、ELF、PC 虚拟环境和 MNIST 数据集。要完全复现本地工作状态，应按上面步骤重新生成这些文件。
