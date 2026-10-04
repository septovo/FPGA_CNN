"""Summarize stage-7 UART status snapshots without inventing board results."""
import argparse
import json
import re
from pathlib import Path

STATUS = re.compile(r"STATUS mode=(\d+) elapsed_ms=(\d+) capture_fps_x100=(\d+) copy_fps_x100=(\d+) process_fps_x100=(\d+)")
VDMA = re.compile(r"VDMA1 irq=(\d+) copied=(\d+) dropped=(\d+) triplet=(\d+) slots=0x([0-9a-fA-F]+) errors=(\d+) recoveries=(\d+)")
PERF = re.compile(r"PERF preprocess_n=(\d+) avg_us=(\d+) inference_n=(\d+) errors=(\d+) avg_us=(\d+) p95_us=(\d+) min_us=(\d+) max_us=(\d+) window=(\d+)")


def parse(text):
    status = [tuple(map(int, match)) for match in STATUS.findall(text)]
    vdma = [tuple(int(value, 16) if index == 4 else int(value)
                  for index, value in enumerate(match)) for match in VDMA.findall(text)]
    perf = [tuple(map(int, match)) for match in PERF.findall(text)]
    if not status or not vdma or not perf:
        raise ValueError("missing STATUS, VDMA1 or PERF line; press 's' on the board")
    s, v, p = status[-1], vdma[-1], perf[-1]
    return {"snapshots": min(len(status), len(vdma), len(perf)),
            "mode": s[0], "elapsed_ms": s[1],
            "capture_fps": s[2] / 100, "copy_fps": s[3] / 100,
            "process_fps": s[4] / 100, "vdma_errors": v[5],
            "recoveries": v[6], "dropped_frames": v[2],
            "preprocess_samples": p[0], "preprocess_average_us": p[1],
            "inference_samples": p[2], "inference_errors": p[3],
            "inference_average_us": p[4], "inference_p95_us": p[5],
            "inference_min_us": p[6], "inference_max_us": p[7]}


def main():
    parser=argparse.ArgumentParser(); parser.add_argument("log",type=Path); parser.add_argument("--output",type=Path)
    args=parser.parse_args(); result=parse(args.log.read_text(encoding="utf-8",errors="replace"))
    value=json.dumps(result,ensure_ascii=False,indent=2)
    if args.output: args.output.write_text(value+"\n",encoding="utf-8")
    print(value)
if __name__=="__main__": main()
