# 阶段 1：模型固定与 PC 黄金基准记录

## 范围与来源

- 上游仓库：[MNIST-Live-Detection-TFLite](https://github.com/alankrantas/MNIST-Live-Detection-TFLite)，固定 commit：`0546580c7148366bf0dded55d746298b410c14d5`。
- 本地只读参考副本：`tests/upstream_mnist/`。模型：`tests/upstream_mnist/mnist.tflite`，446,972 字节，SHA-256：`b0ae6afa9e9bceed8ae3038c0a844634eddc24f30f262c5b7e5ae800db2db50d`。原仓库为 MIT 许可证，许可证在 `tests/upstream_mnist/LICENSE`。
- MNIST 数据：TensorFlow 托管的 [`mnist.npz`](https://storage.googleapis.com/tensorflow/tf-keras-datasets/mnist.npz)，本地 `tests/mnist.npz`，SHA-256：`731c5ac602752760c8e48fbffcf8c3b850d9dc2a2aedcf2cc48468fc17b673d1`。
- 验证环境：Windows、Python 3.12.4、`ai-edge-litert==2.2.0`、`numpy==2.1.3`、`opencv-python-headless==4.11.0.86`。本机 TensorFlow 2.19.1 的原生 DLL 导入失败，因此采用 LiteRT 的非默认 delegate 内置算子路径运行同一 `.tflite` 模型。无权重转换。

## 已核对的模型接口

输入：`input_1`，`[1,28,28]`，`uint8`，无量化 scale/zero-point。输出：`Identity`，`[1,10]`，`float32`。FlatBuffer schema 版本为 3；共 2 个 `CONV_2D`，以及各 1 个 `CAST`、`EXPAND_DIMS`、`MAX_POOL_2D`、`RESHAPE`、`FULLY_CONNECTED`、`SOFTMAX`，各算子版本为 1。共有 19 个张量，按当前形状简单相加约 1,446,692 字节；此数**不是**运行时峰值内存。算子顺序与张量详情见 `tests/results/model_inspection.json`。这意味着输入虽然是 `uint8`，模型内部仍含浮点计算；移植裸机时不能把它当作全整型量化 CNN。

## 预处理规格（来自固定上游脚本）

上游 `mnist_tflite_detection.py`：BGR 转灰度；Otsu 阈值并反相；5×5 矩形核闭运算；提取外轮廓；丢弃距离四边不足 40 像素、宽高过小或超过图像一半的区域；每个框按较长边的 1/5 补黑边并填为近似正方形；使用 `cv2.INTER_AREA` 缩放至 28×28；将原始 `uint8` 字节送入模型；显示阈值 0.7。`tests/golden_inference.py` 已复现该路径并保存最终输入张量。

## 已执行测试与结果

| 测试 | 数据与输出 | 结果 |
|---|---|---|
| 模型检查 | `tests/results/model_inspection.json` | 输入输出、schema 和算子均成功解析。 |
| 标准测试集完整评估 | MNIST 10,000 张；`tests/results/mnist_full_eval.json` | 9,897 张正确，准确率 **98.97%**，与上游 README 报告一致。 |
| 黄金张量生成 | 前 100 张 MNIST + 上游 `test.jpg` 的 10 个纸面数字；`tests/results/golden/manifest.jsonl` 和各 `.bin` | 共 110 条，每条保存 784 字节输入、SHA-256、10 类浮点分数和预测。前 100 张 MNIST 全部正确；10 个纸面数字均预测为对应的 0–9。 |

上述照片来自上游仓库，**不是 OV5640 拍摄数据**。尚无本板卡真实画面，因此阶段 1 的实拍域准确率、各类 ≥30 张独立样本和现场光照测试均未完成；这些需在阶段 3 可导出帧之后采集。不能把 98.97% 写成系统现场准确率。

## 复现命令（在工程根目录）

```powershell
python -m venv tests/.venv
tests/.venv/Scripts/python.exe -m pip install -r tests/requirements-pc.txt
tests/.venv/Scripts/python.exe tests/model_inspect.py --model tests/upstream_mnist/mnist.tflite --output tests/results/model_inspection.json
tests/.venv/Scripts/python.exe tests/evaluate_mnist.py --model tests/upstream_mnist/mnist.tflite --mnist-npz tests/mnist.npz --output tests/results/mnist_full_eval.json
tests/.venv/Scripts/python.exe tests/golden_inference.py --model tests/upstream_mnist/mnist.tflite --output-dir tests/results/golden --mnist-npz tests/mnist.npz --mnist-count 100 --image tests/upstream_mnist/test.jpg
```

上游仓库如需重新获取：`git clone https://github.com/alankrantas/MNIST-Live-Detection-TFLite.git tests/upstream_mnist`，再 `git -C tests/upstream_mnist checkout 0546580c7148366bf0dded55d746298b410c14d5`。重新下载时先比对模型 SHA-256；不一致则停止基准对比并核查来源。

## 进入下一阶段所需输入

阶段 2 可以开始 PL 灰度/均值链开发。并行准备真实采样：从 VDMA1 的完整帧导出至少每类 30 张手写数字，覆盖光照、笔宽和位置变化，保留原始帧及真值标注；届时重新运行黄金脚本，新增本板卡实拍域测试结果。
