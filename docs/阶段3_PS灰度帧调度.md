# 阶段 3：灰度帧进入 PS 与安全帧调度

## 本次改动

1. 在 Block Design 中开启 PS Fabric IRQ，将 `axi_vdma_1/s2mm_introut` 接到 `processing_system7_0/IRQ_F2P`。重新综合、实现并导出 XSA。新 BSP 的 VDMA1 中断号为 **61**，S2MM 流宽为 **24 位**，帧存储数为 **3**。
2. Vitis 平台已换成 `vitis/system_wrapper.xsa`。应用新增 `frame_pipeline.c/.h`：配置 VDMA1 的 RGB888 灰度三帧、帧计数与错误中断、GIC、当前写帧指针读取、中心 256×256 ROI 复制和超时恢复。ISR 只清中断并记录计数、错误与写帧槽；ROI 复制与日志在主循环。
3. ROI 从当前写帧的前一个槽读取。复制前后再次查询 VDMA 当前写槽，若复制期间可能被改写或跨越多帧则丢弃结果。DMA 写入后对每行 ROI 做 D-cache invalidate；应用在启动 DMA 前清空并 flush 三帧，之后不再对 DMA 缓冲写入。
4. `main.c` 保留 CAM0 RGB→LCD 链；GPIO 第 2 通道控制均值滤波。串口输入 `0` / `1` 在下一次 VSYNC 切换滤波关闭/开启，`s` 打印中断、已复制、丢帧、RGB 三通道不一致、已观察帧槽位图、错误与恢复次数。

## DDR 与软件内存

| 用途 | 地址 / 大小 | 说明 |
| --- | --- | --- |
| 程序与静态数据 | 从 `0x00100000` 起，ELF `_end=0x0012BBC0` | 包括 256×256 字节 ROI 暂存数组；与视频环分离。 |
| CAM0 RGB 三帧 | `0x01000000` 起，最多 9,216,000 字节 | VDMA0 写、VDMA2 读，原显示路径。 |
| 灰度 RGB888 三帧 | `0x02000000` 起，最多 9,216,000 字节 | VDMA1 写，PS 只读稳定 ROI。 |
| DDR 物理上限 | `0x3FFFFFFF` | 由新 BSP `xparameters.h` 给出。 |

每个 VDMA1 帧地址按实际 `width × height × 3` 递增。主程序在启动前检查两个帧环不重叠且不超出 DDR。支持的 480×272、800×480、1024×600、1280×800 模式帧长均符合 64 字节对齐。

## 已执行的检查

| 检查 | 结果 |
| --- | --- |
| BD 中断连线与 `validate_bd_design` | PASS，IRQ_F2P 已连 VDMA1 S2MM。 |
| Vivado 2020.2 综合、实现、bitstream、XSA | PASS；[实现时序](../tests/hdl/stage3_timing_impl.rpt) WNS **+0.778 ns**，WHS **+0.039 ns**，TNS/THS = 0；[资源](../tests/hdl/stage3_utilization_impl.rpt) LUT 14,948、FF 33,956、BRAM Tile 16.5。 |
| 新 BSP 参数 | PASS；VDMA1 中断 ID 61，S2MM TDATA 24 位，三帧。 |
| ARM GCC 全源码编译链接 | PASS，无应用源码编译警告；ELF text 72,532、data 2,440、bss 97,184 字节。 |
| Bootgen 打包 | PASS；FSBL、bitstream、应用 ELF 已生成 `BOOT.BIN`。 |

编译脚本：在工程根目录运行 `tests/build_stage3.ps1`；硬件脚本为 `tests/hdl/connect_vdma1_irq.tcl`（在阶段二 BD 上执行一次）、`synth_stage3.tcl`、`impl_stage3.tcl`。更新平台元数据使用 `tests/hdl/update_stage3_platform.tcl`；在新工作区仍须用 Vitis 2020.2 从 `vitis/system_wrapper.xsa` 生成 BSP，再运行编译脚本。Bootgen 描述为 `artifacts/stage3/boot.bif`，从工程根目录执行打包。

## 实板测试步骤与通过标准（待执行）

目前没有板卡连接，以下测试**尚未通过实测**：

1. 将 `artifacts/stage3/BOOT.BIN` 放到测试 SD 卡启动分区，板卡设为 SD 启动；串口记录启动信息。冷启动 10 次，摄像头与 LCD 均正常，灰度 VDMA1 无启动失败。
2. 使用 800×480 模式，观察串口前 6 次 ROI 日志与 `s` 状态；`slots` 最终为 `0x7`，`triplet=0`、`errors=0`。用 JTAG/串口导出三帧缓冲的首末行并核对地址、stride、像素数与行顺序；移动物体时不得出现新旧半帧拼接。
3. 串口发送 `0`、`1`，在固定彩色图案下比对每个灰度通道和 3×3 均值黄金结果；首两行、首两列应按 RTL 边界规则仅输出灰度。保持原 RGB LCD 画面正常。
4. 逐个测试实际使用的 LCD 模式。连续运行至少 30 分钟，记录帧率、丢帧、错误、恢复次数与 ROI 复制时间；`errors=0`，无 DDR 越界或缓存旧帧。若超时/错误，记录诊断后分析 Genlock、时钟和中断时序。

现有 `pin.xdc` 对未使用的 CAM1 像素时钟网设置属性会产生一条 `Common 17-55` critical warning；bitstream DRC 为 0 error。该警告与板卡时钟约束一并在上板前复核。

