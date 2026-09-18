"""Reproduce full MNIST test accuracy without creating 10,000 small files."""

import argparse
from importlib.metadata import version
import json
from pathlib import Path

import numpy as np
from ai_edge_litert.interpreter import Interpreter, OpResolverType


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--mnist-npz", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=100)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with np.load(args.mnist_npz) as dataset:
        images, labels = dataset["x_test"], dataset["y_test"]
    assert images.shape == (10000, 28, 28) and labels.shape == (10000,)
    confusion = np.zeros((10, 10), dtype=np.int64)
    for start in range(0, len(images), args.batch_size):
        batch = images[start:start + args.batch_size]
        interpreter = Interpreter(
            model_path=str(args.model),
            experimental_op_resolver_type=OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES,
        )
        input_spec = interpreter.get_input_details()[0]
        interpreter.resize_tensor_input(input_spec["index"], batch.shape)
        interpreter.allocate_tensors()
        input_spec = interpreter.get_input_details()[0]
        output_spec = interpreter.get_output_details()[0]
        interpreter.set_tensor(input_spec["index"], batch)
        interpreter.invoke()
        predictions = interpreter.get_tensor(output_spec["index"]).argmax(axis=1)
        for truth, prediction in zip(labels[start:start + len(batch)], predictions):
            confusion[int(truth), int(prediction)] += 1
    report = {
        "runtime": f"ai-edge-litert {version('ai-edge-litert')}",
        "samples": int(confusion.sum()),
        "correct": int(confusion.trace()),
        "accuracy": float(confusion.trace() / confusion.sum()),
        "confusion_matrix": confusion.tolist(),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("samples", "correct", "accuracy")}, indent=2))


if __name__ == "__main__":
    main()
