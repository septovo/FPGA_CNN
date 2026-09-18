"""Compare the bare-metal C graph against all pinned TFLite golden cases."""
import json
import math
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "tests/build_stage5/digit_model_host.exe"
MANIFEST = ROOT / "tests/results/golden/manifest.jsonl"
ABS_TOL = 5e-4


def main():
    worst = (0.0, "", -1)
    count = 0
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        case = json.loads(line)
        raw = subprocess.check_output([str(EXE), str(MANIFEST.parent / case["tensor_file"])], text=True)
        actual = [float(v) for v in raw.split()]
        expected = case["scores"]
        assert len(actual) == len(expected) == 10
        assert all(math.isfinite(v) and 0 <= v <= 1 for v in actual)
        assert abs(sum(actual) - 1) < 1e-4
        assert max(range(10), key=actual.__getitem__) == case["prediction"], case["id"]
        for index, (a, e) in enumerate(zip(actual, expected)):
            delta = abs(a - e)
            if delta > worst[0]: worst = (delta, case["id"], index)
            assert delta <= ABS_TOL, (case["id"], index, a, e, delta)
        count += 1
    print(json.dumps({"cases": count, "max_abs_error": worst[0],
                      "worst_case": worst[1], "worst_class": worst[2],
                      "abs_tolerance": ABS_TOL, "all_predictions_match": True}, indent=2))


if __name__ == "__main__": main()
