set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
open_bd_design $root/dual_ov5640_lcd.srcs/sources_1/bd/system/system.bd
validate_bd_design
update_compile_order -fileset sources_1
reset_run synth_1
launch_runs synth_1 -jobs 4
wait_on_run synth_1
set status [get_property STATUS [get_runs synth_1]]
puts "STAGE3_SYNTH_STATUS=$status"
if {![string match "synth_design Complete*" $status]} {error "Synthesis did not complete: $status"}
open_run synth_1
report_utilization -file $root/tests/hdl/stage3_utilization.rpt
report_timing_summary -file $root/tests/hdl/stage3_timing_synth.rpt
puts "STAGE3_SYNTH_OK"
exit
