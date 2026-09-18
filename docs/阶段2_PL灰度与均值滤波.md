# 阶段 2：PL 灰度与均值滤波实现记录

## 实现范围

- 在 CAM0 的原生 RGB888 视频端分成两路：原路继续进入 `v_vid_in_axi4s_0 → VDMA0 → VDMA2 → LCD`；新增路进入 `gray_mean_cam0 → v_vid_in_axi4s_1 → VDMA1`。VDMA1 原 CAM1 输入已断开，因此本版本的 CAM1 图像不再写入 DDR。
- 灰度公式：`Y=(77R+150G+29B+128)>>8`。滤波开启后，对当前位置及上方、左方各两像素求 3×3 整数均值（向下取整）。前两行、前两列直通灰度。输出为 `{Y,Y,Y}`，保持 VDMA1 的 24 位像素格式。
- `axi_gpio_0` 已启用第 2 通道，bit0 控制均值滤波：0 为仅灰度，1 为灰度加均值滤波。控制位跨入 CAM0 像素时钟域后在 VSYNC 处锁存，避免一帧中途改变模式。复位后默认 0。
- 像素数据和 `vid_ce`、`vid_active_video`、`vid_vsync` 一同接至 Video In IP；该模块不额外延迟原生视频信号。AXI4-Stream 背压由现有 Video In IP 缓冲处理，不在本模块内实现。

## 代码和重现命令

- RTL：`ip_repo/gray_mean_video/src/gray_mean_video.v`
- 测试台：`tests/hdl/tb_gray_mean_video.sv`
- BD 一次性集成脚本：`tests/hdl/integrate_gray_bd.tcl`（当前 BD 已集成，勿对当前设计重复执行创建命令）
- BD 校验及 IP 生成：`tests/hdl/validate_generate_bd.tcl`
- 系统综合：`tests/hdl/synth_stage2.tcl`
- 实现、bitstream、XSA 导出：`tests/hdl/impl_stage2.tcl`

在工程根目录运行：

```powershell
& 'E:\xilinx\Vivado\2020.2\bin\vivado.bat' -mode batch -source tests/hdl/validate_generate_bd.tcl
& 'E:\xilinx\Vivado\2020.2\bin\vivado.bat' -mode batch -source tests/hdl/synth_stage2.tcl
& 'E:\xilinx\Vivado\2020.2\bin\vivado.bat' -mode batch -source tests/hdl/impl_stage2.tcl
```

在 `tests/hdl` 目录运行：

```powershell
& 'E:\xilinx\Vivado\2020.2\bin\xvlog.bat' -sv '../../ip_repo/gray_mean_video/src/gray_mean_video.v' 'tb_gray_mean_video.sv'
& 'E:\xilinx\Vivado\2020.2\bin\xelab.bat' tb_gray_mean_video -s gray_mean_tb
& 'E:\xilinx\Vivado\2020.2\bin\xsim.bat' gray_mean_tb -runall
```

## 已完成测试（Vivado 2020.2）

| 测试 | 结果 |
| --- | --- |
| HDL 逐像素参考比对 | PASS；5×5 三帧，黑、白、RGB 原色、混合色、边缘直通、均值整除、消隐、不规则 `vid_ce` 停顿、帧中模式变化、行中复位。 |
| Block Design Validate 和目标生成 | PASS。 |
| 系统综合 | PASS；[综合资源](../tests/hdl/stage2_utilization.rpt)：LUT 17,268，FF 35,389，BRAM Tile 16.5；[综合时序](../tests/hdl/stage2_timing_synth.rpt)：WNS +0.950 ns。 |
| 布局布线及 bitstream | PASS；[实现资源](../tests/hdl/stage2_utilization_impl.rpt)：LUT 14,945，FF 33,951，BRAM Tile 16.5；[实现时序](../tests/hdl/stage2_timing_impl.rpt)：WNS +0.466 ns、WHS +0.018 ns、TNS/THS 均为 0。 |
| XSA 导出 | PASS，包含 bitstream。 |

交付文件：`artifacts/stage2/system_wrapper.bit`（SHA-256 `AAB55E5098DE622F3C3C07F73F25219771977360C52BAC904CBA454D091F9884`）和 `artifacts/stage2/system_wrapper.xsa`（SHA-256 `9E8DE25E3C69EE2B3B2C947E2E7A867C7741E0669D23F07E07B1F0E11AD8FD7F`）。两者尚未在实板上烧录验证。

## 仍需实板验证

目前没有板卡访问，也未在 PS 软件中初始化 VDMA1 或 GPIO 第 2 通道。因此还没有验证：灰度三帧写入 DDR、全分辨率像素/行数、滤波开关的板上效果、LCD 与灰度支路并行稳定性。下一阶段先更新 Vitis platform 至本 XSA，并检查 `xparameters.h`，再配置 VDMA1 独立三帧、缓存一致性和帧中断；在 800×480 以及其他实际支持分辨率导出帧逐像素核对，连续运行 30 分钟。

现有 `pin.xdc` 第 7 行对 CAM1 像素时钟缓冲设置属性时，由于 CAM1 路径在本设计中被优化，Vivado 报 1 条 `Common 17-55` critical warning。bitstream DRC 仍为 0 error；后续若恢复 CAM1 输入，应重新检查该约束及双摄资源。由于没有阶段 0 的基线综合报告，暂不能可靠给出 LUT/FF/BRAM 增量。
