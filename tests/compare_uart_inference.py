"""Compare a board UART 'g' capture with the pinned TFLite interpreter."""
import argparse
import json
import re
from pathlib import Path

import numpy as np
from ai_edge_litert.interpreter import Interpreter, OpResolverType

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "tests/upstream_mnist/mnist.tflite"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("uart_log", type=Path)
    args = parser.parse_args()
    log = args.uart_log.read_text(encoding="utf-8", errors="replace")
    pairs = re.findall(r"tensor28=([0-9a-fA-F]{1568})\s+scores_ppm=([0-9,]+)", log)
    if not pairs:
        raise SystemExit("No adjacent tensor28/scores_ppm pair; press 'g' on UART")
    interpreter = Interpreter(model_path=str(MODEL), experimental_op_resolver_type=OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES)
    interpreter.allocate_tensors()
    results = []
    for number, (hex_pixels, csv_scores) in enumerate(pairs):
        pixels = np.frombuffer(bytes.fromhex(hex_pixels), dtype=np.uint8).reshape(1, 28, 28)
        board = np.array([int(x) for x in csv_scores.split(",")], dtype=np.float64) / 1e6
        if board.shape != (10,): raise ValueError(f"pair {number}: expected 10 scores")
        interpreter.set_tensor(interpreter.get_input_details()[0]["index"], pixels)
        interpreter.invoke()
        pc = interpreter.get_tensor(interpreter.get_output_details()[0]["index"])[0]
        error = float(np.max(np.abs(board - pc)))
        board_digit = int(np.argmax(board))
        pc_digit = int(np.argmax(pc))
        results.append({"pair": number, "board_digit": board_digit,
                        "pc_digit": pc_digit, "max_abs_error": error})
        if board_digit != pc_digit or error > 5e-4:
            raise AssertionError(results[-1])
    print(json.dumps({"captures": len(results), "max_abs_error": max(r["max_abs_error"] for r in results),
                      "all_passed": True}, indent=2))


if __name__ == "__main__": main()
