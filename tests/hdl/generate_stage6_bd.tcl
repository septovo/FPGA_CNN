set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
add_files -norecurse $root/ip_repo/gray_mean_video/src/gray_mean_video.v
add_files -norecurse $root/ip_repo/digit_osd_axis/src/digit_osd_axis.v
update_compile_order -fileset sources_1
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
validate_bd_design
save_bd_design
generate_target all [get_files $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd]
update_compile_order -fileset sources_1
puts "STAGE6_GENERATED_OK"
exit
