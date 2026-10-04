set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
add_files -norecurse $root/ip_repo/gray_mean_video/src/gray_mean_video.v
add_files -norecurse $root/ip_repo/digit_osd_axis/src/digit_osd_axis.v
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
foreach pin {axi_vdma_2/m_axis_mm2s_aclk v_axi4s_vid_out_0/aclk digit_osd_axis_0/aclk ps7_0_axi_periph/M06_ACLK} {
  disconnect_bd_net [get_bd_nets -of_objects [get_bd_pins $pin]] [get_bd_pins $pin]
  connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins $pin]
}
disconnect_bd_net [get_bd_nets -of_objects [get_bd_pins digit_osd_axis_0/aresetn]] [get_bd_pins digit_osd_axis_0/aresetn]
connect_bd_net [get_bd_pins rst_ps7_0_100M/peripheral_aresetn] [get_bd_pins digit_osd_axis_0/aresetn]
validate_bd_design
save_bd_design
generate_target all [get_files $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd]
update_compile_order -fileset sources_1
puts "STAGE6_VIDEO_CLOCK_100M_OK"
exit
