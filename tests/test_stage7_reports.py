import csv
import tempfile
from pathlib import Path
from analyze_field_test import analyze
from analyze_uart_log import parse

with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/"field.csv"
    fields="sample_id,run_id,timestamp,truth,prediction,confidence,localization_status,lighting,stroke_width,angle_deg,distance_cm,background,end_to_end_ms,notes".split(",")
    with path.open("w",newline="",encoding="utf-8") as stream:
        writer=csv.DictWriter(stream,fieldnames=fields); writer.writeheader()
        for digit in range(10):
            for number in range(30):
                found=number!=0
                writer.writerow({"sample_id":f"{digit}-{number}","truth":digit,
                    "prediction":digit if found else "","confidence":"0.9" if found else "",
                    "localization_status":"found" if found else "not_found",
                    "lighting":"normal","stroke_width":"medium","angle_deg":"0",
                    "distance_cm":"20","background":"white","end_to_end_ms":"80"})
    result=analyze(path)
    assert result["samples"]==300 and result["found"]==290 and result["correct"]==290
    assert result["complete_30_per_class"] and result["end_to_end_p95_ms"]==80
log="""STATUS mode=1 elapsed_ms=10000 capture_fps_x100=3000 copy_fps_x100=2900 process_fps_x100=580
VDMA1 irq=300 copied=290 dropped=10 triplet=0 slots=0x7 errors=0 recoveries=0
PERF preprocess_n=58 avg_us=2000 inference_n=50 errors=1 avg_us=15000 p95_us=17000 min_us=14000 max_us=18000 window=50
"""
u=parse(log); assert u["capture_fps"]==30 and u["inference_p95_us"]==17000
print("STAGE7_REPORT_PASS")
