set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
add_files -norecurse $root/ip_repo/gray_mean_video/src/gray_mean_video.v
set_property file_type Verilog [get_files $root/ip_repo/gray_mean_video/src/gray_mean_video.v]
update_compile_order -fileset sources_1
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
create_bd_cell -type module -reference gray_mean_cam0 gray_mean_video_0
set_property -dict [list CONFIG.C_IS_DUAL {1} CONFIG.C_GPIO2_WIDTH {1} CONFIG.C_ALL_OUTPUTS_2 {1}] [get_bd_cells axi_gpio_0]
delete_bd_objs [get_bd_intf_nets ov5640_capture_data_0_vid_rgb]
delete_bd_objs [get_bd_intf_nets ov5640_capture_data_1_vid_rgb]
foreach {src dst0 dstg} {
  vid_data vid_data rgb_in
  vid_active_video vid_active_video pixel_active_in
  vid_vsync vid_vsync frame_vsync_in
} {
  connect_bd_net [get_bd_pins ov5640_capture_data_0/$src] [get_bd_pins v_vid_in_axi4s_0/$dst0] [get_bd_pins gray_mean_video_0/$dstg]
}
foreach {src dst} {
  vid_clk pixel_clk
  vid_ce pixel_ce_in
} {
  connect_bd_net [get_bd_pins ov5640_capture_data_0/$src] [get_bd_pins gray_mean_video_0/$dst]
}
connect_bd_net [get_bd_pins ov5640_capture_data_0/rst_n] [get_bd_pins gray_mean_video_0/rst_n]
connect_bd_net [get_bd_pins axi_gpio_0/gpio2_io_o] [get_bd_pins gray_mean_video_0/filter_enable]
foreach {src dst} {
  gray_rgb_out vid_data
  pixel_active_out vid_active_video
  frame_vsync_out vid_vsync
} {
  connect_bd_net [get_bd_pins gray_mean_video_0/$src] [get_bd_pins v_vid_in_axi4s_1/$dst]
}
delete_bd_objs [get_bd_nets ov5640_capture_data_1_vid_clk]
delete_bd_objs [get_bd_nets ov5640_capture_data_1_vid_ce]
connect_bd_net [get_bd_pins ov5640_capture_data_0/vid_clk] [get_bd_pins v_vid_in_axi4s_1/vid_io_in_clk]
connect_bd_net [get_bd_pins gray_mean_video_0/pixel_ce_out] [get_bd_pins v_vid_in_axi4s_1/vid_io_in_ce]
validate_bd_design
save_bd_design
save_project
puts "STAGE2_BD_VALIDATED"
exit
