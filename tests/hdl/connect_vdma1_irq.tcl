set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
set_property -dict [list CONFIG.PCW_USE_FABRIC_INTERRUPT {1} CONFIG.PCW_IRQ_F2P_INTR {1}] [get_bd_cells processing_system7_0]
set irq [get_bd_pins -quiet processing_system7_0/IRQ_F2P]
set source [get_bd_pins -quiet axi_vdma_1/s2mm_introut]
if {[llength $irq] != 1 || [llength $source] != 1} {error "Missing VDMA or PS interrupt pin"}
connect_bd_net $source $irq
validate_bd_design
save_bd_design
generate_target all [get_files $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd]
update_compile_order -fileset sources_1
puts "STAGE3_IRQ_CONNECTED"
exit
