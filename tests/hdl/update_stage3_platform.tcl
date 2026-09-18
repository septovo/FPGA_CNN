set root E:/xuanxiu/39_dual_ov5640_lcd
setws $root/vitis
platform active system_wrapper
platform config -updatehw $root/vitis/system_wrapper.xsa
platform write
puts "STAGE3_PLATFORM_POINTER_UPDATED"
exit
