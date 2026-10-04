# 阶段 6：LCD 检测框与识别结果叠加

## 已实现内容

PL 新增 `digit_osd_axis`，连接位置为：

```text
axi_vdma_2/M_AXIS_MM2S
        → digit_osd_axis
        → v_axi4s_vid_out_0/video_in
```

模块接受 RGB888 AXI4-Stream，叠加绿色候选框、识别数字和整数百分比置信度；
无有效数字时在左上角显示 `NO`。禁用时所有 RGB 像素原样透传。

AXI4-Stream 数据通路使用两级弹性流水，能够传播背压，并保持 `TDATA/TUSER/TLAST`
在停顿期间稳定。显示流使用 PS `FCLK_CLK0` 的 100 MHz 时钟；VDMA DDR 主接口仍使用
150 MHz。100 MHz × 3 byte/cycle 的理论流带宽为 300 MB/s，高于 1280×800 RGB888
在 60 帧/秒时约 184.32 MB/s 的有效像素带宽。

## AXI-Lite 寄存器

基地址：`0x43C20000`。

| 偏移 | 名称 | 内容 |
|---:|---|---|
| `0x00` | X | 框左上角 X，12 位 |
| `0x04` | Y | 框左上角 Y，12 位 |
| `0x08` | W | 框宽，12 位 |
| `0x0C` | H | 框高，12 位 |
| `0x10` | CLASS | 数字 0–9 |
| `0x14` | SCORE | 置信度 0–100 |
| `0x18` | FLAGS | bit0 valid，bit1 enable |
| `0x1C` | SEQUENCE | 识别来源帧序号 |
| `0x20` | TIMEOUT | 结果有效的显示帧数 |
| `0x24` | COMMIT | 写 1 提交全部影子寄存器 |
| `0x28` | STATUS | pending、active valid、剩余帧数 |
| `0x2C` | ACTIVE_SEQUENCE | 当前显示结果的来源帧序号 |

PS 先写影子寄存器，最后写 `COMMIT`。OSD 只在收到下一帧 `TUSER` 时统一锁存，
避免一帧内混合新旧框和文字。默认超时为 120 个显示帧；超时后 valid 自动清零。
串口按 `o` 可以切换 OSD，按 `s` 查看 OSD 状态、CNN 结果和推理统计。

## 自动测试结果

### RTL 仿真

测试平台：`tests/hdl/tb_digit_osd_axis.sv`。

覆盖：

- 数字 9 和 0、85% 和 100% 置信度；
- 画面右下边缘及超出画面的框；
- 一帧中间提交下一份结果，验证只在下一帧生效；
- 结果超时后显示无数字状态；
- OSD 禁用时逐像素透传；
- 随机 `TREADY` 背压；
- 5 帧、3,840 个输出像素的数量、`TUSER` 和 `TLAST` 校验。

结果：`STAGE6_OSD_PASS pixels=3840 frames=5`。

### Vivado 2020.2

- Block Design validate：通过；
- 综合：通过，WNS `+0.468 ns`，TNS `0`；
- 实现与 bitstream：通过，WNS `+0.676 ns`，TNS `0`；
- 实现后资源：15,563 Slice LUT（29.25%）、34,568 Slice Register（32.49%）、
  16.5 BRAM Tile（11.79%）、0 DSP；
- Cortex-A9 裸机 ELF：编译及链接通过；
- Bootgen：通过。

## 构建

```powershell
# 功能仿真
xvlog.bat -sv ip_repo/digit_osd_axis/src/digit_osd_axis.v tests/hdl/tb_digit_osd_axis.sv
xelab.bat tb_digit_osd_axis -s stage6_osd_sim
xsim.bat stage6_osd_sim -runall

# 工程生成、综合、实现
vivado.bat -mode batch -source tests/hdl/generate_stage6_bd.tcl
vivado.bat -mode batch -source tests/hdl/synth_stage6.tcl
vivado.bat -mode batch -source tests/hdl/impl_stage6.tcl

# ARM 应用和启动镜像
& tests/build_stage6_arm.ps1
bootgen.bat -arch zynq -image artifacts/stage6/boot.bif -o artifacts/stage6/BOOT.BIN -w on
```

交付文件：

- `artifacts/stage6/system_wrapper.bit`
- `artifacts/stage6/system_wrapper.xsa`
- `artifacts/stage6/dual_ov5640_lcd.elf`
- `artifacts/stage6/BOOT.BIN`

`BOOT.BIN` SHA-256：
`42a786b40770c0c8df32e806f97b5efea4fe64dd009e73919e6f753a3e715350`。

## 上板测试（待开发板执行）

1. 烧写阶段 6 `BOOT.BIN`，确认相机画面连续显示。
2. 依次展示 0–9，核对绿色框位置、数字和置信度；快速换纸时观察结果只在整帧边界变化。
3. 将数字靠近四边和四角，确认框被屏幕自然裁剪且视频不中断。
4. 移走数字，确认预处理返回无数字并显示 `NO`；停止识别更新，确认约 120 帧后旧结果自动失效。
5. 按 `o` 禁用 OSD，核对背景画面与阶段 3 直通链一致；再次按 `o` 恢复。
6. 持续运行至少 30 分钟，记录 VDMA 错误、丢帧、Video Out 错误及是否出现撕裂。

当前环境没有连接开发板，因此实时摄像头显示效果、30 分钟稳定性和物理 LCD 上的坐标
仍需实机验收。阶段 6 的 RTL 仿真、综合、实现、软件编译和镜像生成已经完成。

