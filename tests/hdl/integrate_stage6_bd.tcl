set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
add_files -norecurse $root/ip_repo/digit_osd_axis/src/digit_osd_axis.v
set_property file_type Verilog [get_files $root/ip_repo/digit_osd_axis/src/digit_osd_axis.v]
update_compile_order -fileset sources_1
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
if {[llength [get_bd_cells -quiet digit_osd_axis_0]] != 0} {
  validate_bd_design
  puts "STAGE6_BD_ALREADY_INTEGRATED"
  exit
}
create_bd_cell -type module -reference digit_osd_axis digit_osd_axis_0
set_property -dict [list CONFIG.NUM_MI {7}] [get_bd_cells ps7_0_axi_periph]
delete_bd_objs [get_bd_intf_nets axi_vdma_2_M_AXIS_MM2S]
connect_bd_intf_net [get_bd_intf_pins axi_vdma_2/M_AXIS_MM2S] [get_bd_intf_pins digit_osd_axis_0/s_axis]
connect_bd_intf_net [get_bd_intf_pins digit_osd_axis_0/m_axis] [get_bd_intf_pins v_axi4s_vid_out_0/video_in]
connect_bd_intf_net [get_bd_intf_pins ps7_0_axi_periph/M06_AXI] [get_bd_intf_pins digit_osd_axis_0/s_axi]
foreach pin {axi_vdma_2/m_axis_mm2s_aclk v_axi4s_vid_out_0/aclk} {
  disconnect_bd_net [get_bd_nets -of_objects [get_bd_pins $pin]] [get_bd_pins $pin]
}
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
  [get_bd_pins axi_vdma_2/m_axis_mm2s_aclk] \
  [get_bd_pins v_axi4s_vid_out_0/aclk] \
  [get_bd_pins digit_osd_axis_0/aclk] \
  [get_bd_pins ps7_0_axi_periph/M06_ACLK]
connect_bd_net [get_bd_pins rst_ps7_0_100M/peripheral_aresetn] [get_bd_pins digit_osd_axis_0/aresetn]
assign_bd_address
puts "OSD_SEGMENTS=[get_bd_addr_segs digit_osd_axis_0/*]"
puts "OSD_ADDRESSES=[get_bd_addr_segs processing_system7_0/Data/*]"
validate_bd_design
save_bd_design
puts "STAGE6_BD_VALIDATED"
exit
