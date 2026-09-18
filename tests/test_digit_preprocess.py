import json
import subprocess
from pathlib import Path

import cv2
import numpy as np

from golden_inference import make_interpreter, infer

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "build_stage4"
CLI = OUT / "preprocess_cli.exe"


def run_c(image, name):
    source = OUT / f"{name}.raw"
    tensor = OUT / f"{name}.bin"
    source.write_bytes(image.tobytes())
    result = subprocess.run([str(CLI), str(source), str(image.shape[1]),
                             str(image.shape[0]), str(tensor)],
                            check=True, capture_output=True, text=True)
    fields = dict(item.split("=") for item in result.stdout.strip().split())
    data = tensor.read_bytes() if int(fields["status"]) == 0 else None
    return {k: int(v) for k, v in fields.items()}, data


def golden_cases():
    bgr = cv2.imread(str(ROOT / "tests/upstream_mnist/test.jpg"))
    assert bgr is not None
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    _, binary = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    closed = cv2.morphologyEx(binary, cv2.MORPH_CLOSE,
                              cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5)))
    records = [json.loads(line) for line in
               (ROOT / "tests/results/golden/manifest.jsonl").read_text().splitlines()]
    max_mismatches = 0
    interpreter, input_index, output_index = make_interpreter(
        ROOT / "tests/upstream_mnist/mnist.tflite")
    for record in records:
        if not record["id"].startswith("test_"):
            continue
        x, y, w, h = record["box_xywh"]
        roi = np.full((256, 256), 255, dtype=np.uint8)
        rx, ry = (256 - w) // 2, (256 - h) // 2
        roi[ry:ry+h, rx:rx+w] = 255 - closed[y:y+h, x:x+w]
        fields, tensor = run_c(roi, record["id"])
        expected = (ROOT / "tests/results/golden" / record["tensor_file"]).read_bytes()
        if fields["status"] != 0 or (fields["x"], fields["y"], fields["w"], fields["h"]) != (rx, ry, w, h):
            raise AssertionError((record["id"], fields, (rx, ry, w, h)))
        mismatches = np.count_nonzero(np.frombuffer(tensor, np.uint8) !=
                                      np.frombuffer(expected, np.uint8))
        max_mismatches = max(max_mismatches, int(mismatches))
        print(record["id"], "byte_mismatches", mismatches)
        if mismatches:
            c_scores = infer(interpreter, input_index, output_index,
                             np.frombuffer(tensor, np.uint8).reshape(28, 28))
            cv_scores = infer(interpreter, input_index, output_index,
                              np.frombuffer(expected, np.uint8).reshape(28, 28))
            assert int(np.argmax(c_scores)) == int(np.argmax(cv_scores)), record["id"]
            print("prediction", int(np.argmax(c_scores)),
                  "confidence_c", float(np.max(c_scores)),
                  "confidence_cv", float(np.max(cv_scores)))
    print("golden_max_mismatches", max_mismatches)
    assert max_mismatches <= 2


def reference(image):
    height, width = image.shape
    threshold, binary = cv2.threshold(image, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    closed = cv2.morphologyEx(binary, cv2.MORPH_CLOSE,
                              cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5)))
    count, _, stats, _ = cv2.connectedComponentsWithStats(closed, 8)
    best = None
    for x, y, w, h, area in stats[1:]:
        if (x < 40 or y < 40 or x + w > width - 1 - 40 or
            y + h > height - 1 - 40 or w < 14 or h < 14 or
            w > width // 2 or h > height // 2 or area < 32):
            continue
        if best is None or (area, -y, -x) > (best[4], -best[1], -best[0]):
            best = (x, y, w, h, area)
    if best is None:
        return int(threshold), None, None
    x, y, w, h, area = best
    size = max(w, h)
    y_pad = (w - h) // 2 if w > h else 0
    x_pad = (h - w) // 2 if h > w else 0
    y_pad += size // 5
    x_pad += size // 5
    padded = cv2.copyMakeBorder(closed[y:y+h, x:x+w],
                                y_pad, y_pad, x_pad, x_pad,
                                cv2.BORDER_CONSTANT, value=0)
    tensor = cv2.resize(padded, (28, 28), interpolation=cv2.INTER_AREA)
    return int(threshold), best, tensor.tobytes()


def synthetic_cases():
    cases = []
    for number in range(10):
        for variant in range(3):
            image = np.full((256, 256), 245, dtype=np.uint8)
            scale = [2.0, 1.6, 1.2][variant]
            thickness = [3, 2, 4][variant]
            cv2.putText(image, str(number), (92, 164), cv2.FONT_HERSHEY_SIMPLEX,
                        scale, 15, thickness, cv2.LINE_AA)
            if variant == 2:
                image = np.clip(image.astype(np.int16) +
                                np.linspace(-20, 20, 256, dtype=np.int16)[None, :],
                                0, 255).astype(np.uint8)
            cases.append((f"digit_{number}_{variant}", image))
    cases.append(("blank_white", np.full((256, 256), 255, np.uint8)))
    cases.append(("blank_black", np.zeros((256, 256), np.uint8)))
    for number, angle in [(2, -14), (3, 12), (5, -9), (7, 17)]:
        image = np.full((256, 256), 255, np.uint8)
        cv2.putText(image, str(number), (92, 164), cv2.FONT_HERSHEY_SIMPLEX,
                    1.8, 0, 2, cv2.LINE_AA)
        transform = cv2.getRotationMatrix2D((128, 128), angle, 1.0)
        cases.append((f"slant_{number}_{angle}",
                      cv2.warpAffine(image, transform, (256, 256),
                                     borderValue=255)))
    tiny_digit = np.full((256, 256), 255, np.uint8)
    cv2.rectangle(tiny_digit, (110, 110), (123, 123), 0, -1)
    cases.append(("minimum_14px", tiny_digit))
    for x in [1, 28, 40, 45]:
        image = np.full((256, 256), 255, np.uint8)
        cv2.rectangle(image, (x, 90), (x+22, 130), 0, -1)
        cases.append((f"edge_{x}", image))
    rng = np.random.default_rng(42)
    for density in [10, 100, 500, 1000]:
        image = np.full((256, 256), 255, np.uint8)
        points = rng.integers(0, 256, size=(density, 2))
        image[points[:, 0], points[:, 1]] = 0
        cases.append((f"noise_{density}", image))
    crowded = np.full((256, 256), 255, np.uint8)
    crowded[::6, ::6] = 0
    cases.append(("too_noisy", crowded))
    worst_mismatch = 0
    for name, image in cases:
        fields, tensor = run_c(image, name)
        threshold, box, expected = reference(image)
        _, cv_binary = cv2.threshold(image, 0, 255,
                                     cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
        cv_closed = cv2.morphologyEx(cv_binary, cv2.MORPH_CLOSE,
                         cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5)))
        assert (OUT / f"{name}.bin.binary").read_bytes() == cv_binary.tobytes(), name
        assert (OUT / f"{name}.bin.closed").read_bytes() == cv_closed.tobytes(), name
        assert fields["otsu"] == threshold, (name, fields["otsu"], threshold)
        if name == "too_noisy":
            assert fields["status"] == 2, fields
            continue
        assert (fields["status"] == 0) == (box is not None), (name, fields, box)
        if box is not None:
            assert (fields["x"], fields["y"], fields["w"], fields["h"], fields["area"]) == box, (name, fields, box)
            actual = np.frombuffer(tensor, np.uint8).astype(np.int16)
            target = np.frombuffer(expected, np.uint8).astype(np.int16)
            difference = np.abs(actual - target)
            mismatch_count = int(np.count_nonzero(difference))
            worst_mismatch = max(worst_mismatch, mismatch_count)
            if mismatch_count:
                print("synthetic_difference", name, mismatch_count,
                      "max_delta", int(difference.max()))
            assert difference.max() <= 1, (name, difference.max())
            assert np.count_nonzero(difference) <= 8, (name, np.count_nonzero(difference))
    print("synthetic_cases", len(cases), "worst_byte_mismatches", worst_mismatch)
    for name in ["blank_white", "blank_black"]:
        fields, _ = run_c(dict(cases)[name], name)
        assert fields["status"] == 1, (name, fields)
    tiny = np.full((16, 16), 255, np.uint8)
    path = OUT / "invalid.raw"
    path.write_bytes(tiny.tobytes())
    result = subprocess.run([str(CLI), str(path), "16", "16",
                             str(OUT / "invalid.bin")], capture_output=True, text=True)
    assert "status=-1" in result.stdout


if __name__ == "__main__":
    golden_cases()
    synthetic_cases()
