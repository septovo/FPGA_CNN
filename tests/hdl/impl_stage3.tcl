set root E:/xuanxiu/39_dual_ov5640_lcd
open_project $root/dual_ov5640_lcd.xpr
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
set status [get_property STATUS [get_runs impl_1]]
puts "STAGE3_IMPL_STATUS=$status"
if {![string match "write_bitstream Complete*" $status]} {error "Implementation did not complete: $status"}
open_run impl_1
report_utilization -file $root/tests/hdl/stage3_utilization_impl.rpt
report_timing_summary -file $root/tests/hdl/stage3_timing_impl.rpt
file mkdir $root/artifacts/stage3
file copy -force $root/dual_ov5640_lcd.runs/impl_1/system_wrapper.bit $root/artifacts/stage3/system_wrapper.bit
write_hw_platform -fixed -include_bit -force -file $root/artifacts/stage3/system_wrapper.xsa
puts "STAGE3_IMPL_OK"
exit
