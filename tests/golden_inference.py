"""Produce deterministic PC reference tensors and predictions for the selected model."""

import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np
from ai_edge_litert.interpreter import Interpreter, OpResolverType


def make_interpreter(model_path):
    interpreter = Interpreter(
        model_path=str(model_path),
        experimental_op_resolver_type=OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES,
    )
    interpreter.allocate_tensors()
    input_spec = interpreter.get_input_details()[0]
    output_spec = interpreter.get_output_details()[0]
    assert input_spec["shape"].tolist() == [1, 28, 28], input_spec["shape"]
    assert input_spec["dtype"] == np.uint8, input_spec["dtype"]
    assert output_spec["shape"].tolist() == [1, 10], output_spec["shape"]
    assert output_spec["dtype"] == np.float32, output_spec["dtype"]
    return interpreter, input_spec["index"], output_spec["index"]


def infer(interpreter, input_index, output_index, digit):
    assert digit.shape == (28, 28) and digit.dtype == np.uint8
    interpreter.set_tensor(input_index, digit[None, :, :])
    interpreter.invoke()
    scores = interpreter.get_tensor(output_index)[0].astype(np.float32)
    return scores


def upstream_candidates(bgr, border=40):
    """Match mnist_tflite_detection.py; preserve contour order and integer padding."""
    gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    _, binary = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY_INV | cv2.THRESH_OTSU)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5))
    closed = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel)
    contours, _ = cv2.findContours(closed, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    height, width = gray.shape
    for contour in contours:
        x, y, w, h = cv2.boundingRect(contour)
        if x < border or x + w > width - 1 - border or y < border or y + h > height - 1 - border:
            continue
        if w < 14 or h < 14 or w > width // 2 or h > height // 2:
            continue
        size = max(w, h)
        y_pad = ((w - h) // 2 if w > h else 0) + size // 5
        x_pad = ((h - w) // 2 if h > w else 0) + size // 5
        square = cv2.copyMakeBorder(
            closed[y:y + h, x:x + w], y_pad, y_pad, x_pad, x_pad,
            cv2.BORDER_CONSTANT, value=0,
        )
        digit = cv2.resize(square, (28, 28), interpolation=cv2.INTER_AREA)
        yield (x, y, w, h), digit


def save_case(output_dir, case_id, digit, scores, truth=None, box=None):
    tensor_name = f"{case_id}.bin"
    (output_dir / tensor_name).write_bytes(digit.tobytes())
    record = {
        "id": case_id,
        "tensor_file": tensor_name,
        "tensor_sha256": hashlib.sha256(digit.tobytes()).hexdigest(),
        "truth": None if truth is None else int(truth),
        "box_xywh": box,
        "prediction": int(np.argmax(scores)),
        "scores": [float(value) for value in scores],
    }
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--mnist-npz", type=Path, help="Local Keras MNIST dataset; do not fetch implicitly")
    parser.add_argument("--mnist-count", type=int, default=100)
    parser.add_argument("--image", type=Path, action="append", default=[])
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    interpreter, input_index, output_index = make_interpreter(args.model)
    records = []
    if args.mnist_npz:
        with np.load(args.mnist_npz) as dataset:
            images, labels = dataset["x_test"], dataset["y_test"]
            assert images.shape == (10000, 28, 28)
            for index in range(min(args.mnist_count, len(images))):
                image = np.asarray(images[index], dtype=np.uint8)
                scores = infer(interpreter, input_index, output_index, image)
                records.append(save_case(args.output_dir, f"mnist_{index:05d}", image, scores, labels[index]))
    for image_path in args.image:
        bgr = cv2.imread(str(image_path))
        if bgr is None:
            raise FileNotFoundError(image_path)
        for index, (box, digit) in enumerate(upstream_candidates(bgr)):
            scores = infer(interpreter, input_index, output_index, digit)
            records.append(save_case(args.output_dir, f"{image_path.stem}_{index:03d}", digit, scores, box=box))
    output = args.output_dir / "manifest.jsonl"
    output.write_text("".join(json.dumps(row) + "\n" for row in records), encoding="utf-8")
    known = [row for row in records if row["truth"] is not None]
    accuracy = sum(row["prediction"] == row["truth"] for row in known) / len(known) if known else None
    print(json.dumps({"records": len(records), "mnist": len(known), "mnist_accuracy": accuracy, "manifest": str(output)}, indent=2))


if __name__ == "__main__":
    main()
