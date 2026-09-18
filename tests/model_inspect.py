"""Inspect the pinned MNIST TFLite model before bare-metal porting."""

import argparse
import hashlib
from importlib.metadata import version
import json
from pathlib import Path

import numpy as np
from ai_edge_litert import schema_py_generated as schema
from ai_edge_litert.interpreter import Interpreter, OpResolverType


def tensor_info(tensor):
    quant = tensor["quantization_parameters"]
    return {
        "name": tensor["name"],
        "index": int(tensor["index"]),
        "shape": tensor["shape"].tolist(),
        "shape_signature": tensor["shape_signature"].tolist(),
        "dtype": np.dtype(tensor["dtype"]).name,
        "quantization_scales": quant["scales"].tolist(),
        "quantization_zero_points": quant["zero_points"].tolist(),
    }


def inspect(model_path):
    data = model_path.read_bytes()
    model = schema.Model.GetRootAsModel(data, 0)
    enum_names = {
        value: name
        for name, value in vars(schema.BuiltinOperator).items()
        if name.isupper() and isinstance(value, int)
    }
    codes = []
    for index in range(model.OperatorCodesLength()):
        code = model.OperatorCodes(index)
        builtin = code.BuiltinCode()
        codes.append({
            "index": index,
            "name": enum_names.get(builtin, f"UNKNOWN_{builtin}"),
            "version": code.Version(),
            "custom_code": code.CustomCode().decode() if code.CustomCode() else None,
        })
    subgraphs = []
    for index in range(model.SubgraphsLength()):
        graph = model.Subgraphs(index)
        operators = []
        for operator_index in range(graph.OperatorsLength()):
            operator = graph.Operators(operator_index)
            operators.append({
                "index": operator_index,
                "opcode": codes[operator.OpcodeIndex()]["name"],
                "inputs": operator.InputsAsNumpy().tolist(),
                "outputs": operator.OutputsAsNumpy().tolist(),
            })
        subgraphs.append({"index": index, "tensor_count": graph.TensorsLength(), "operators": operators})
    interpreter = Interpreter(
        model_path=str(model_path),
        experimental_op_resolver_type=OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES,
    )
    interpreter.allocate_tensors()
    tensor_details = interpreter.get_tensor_details()
    tensor_bytes = sum(
        int(np.prod(t["shape"], dtype=np.int64)) * np.dtype(t["dtype"]).itemsize
        for t in tensor_details
    )
    return {
        "model": str(model_path),
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "runtime": f"ai-edge-litert {version('ai-edge-litert')}",
        "schema_version": model.Version(),
        "inputs": [tensor_info(t) for t in interpreter.get_input_details()],
        "outputs": [tensor_info(t) for t in interpreter.get_output_details()],
        "tensor_count": len(tensor_details),
        "sum_current_tensor_shape_bytes": tensor_bytes,
        "operator_codes": codes,
        "subgraphs": subgraphs,
        "note": "Summed tensor shapes are not peak runtime arena memory; measure that on the target runtime.",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.model.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("sha256", "bytes", "inputs", "outputs", "operator_codes")}, indent=2))


if __name__ == "__main__":
    main()
