set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
validate_bd_design
save_bd_design
generate_target all [get_files $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd]
update_compile_order -fileset sources_1
puts "STAGE2_GENERATED_OK"
exit
