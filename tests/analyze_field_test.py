"""Validate camera field-test CSV and produce acceptance metrics."""
import argparse
import csv
import json
from pathlib import Path

REQUIRED = {"sample_id", "truth", "prediction", "confidence",
            "localization_status", "lighting", "stroke_width",
            "angle_deg", "distance_cm", "background", "end_to_end_ms"}


def analyze(path: Path):
    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        missing = REQUIRED - set(reader.fieldnames or [])
        if missing:
            raise ValueError("missing columns: " + ", ".join(sorted(missing)))
        rows = [row for row in reader if any(value.strip() for value in row.values())]
    matrix = [[0] * 11 for _ in range(10)]  # columns 0..9 plus missed
    per_class = [{"total": 0, "found": 0, "correct": 0} for _ in range(10)]
    found = correct = 0
    latencies = []
    seen_ids = set()
    for line, row in enumerate(rows, 2):
        sample_id = row["sample_id"].strip()
        if not sample_id or sample_id in seen_ids:
            raise ValueError(f"line {line}: sample_id empty or duplicated")
        seen_ids.add(sample_id)
        try:
            truth = int(row["truth"])
        except ValueError as error:
            raise ValueError(f"line {line}: truth must be 0..9") from error
        if not 0 <= truth <= 9:
            raise ValueError(f"line {line}: truth must be 0..9")
        status = row["localization_status"].strip().lower()
        if status not in {"found", "not_found"}:
            raise ValueError(f"line {line}: localization_status must be found or not_found")
        per_class[truth]["total"] += 1
        if status == "not_found":
            if row["prediction"].strip() or row["confidence"].strip():
                raise ValueError(f"line {line}: missed samples must have blank prediction/confidence")
            matrix[truth][10] += 1
        else:
            try:
                prediction = int(row["prediction"])
                confidence = float(row["confidence"])
            except ValueError as error:
                raise ValueError(f"line {line}: invalid prediction/confidence") from error
            if not 0 <= prediction <= 9 or not 0.0 <= confidence <= 1.0:
                raise ValueError(f"line {line}: prediction or confidence out of range")
            matrix[truth][prediction] += 1
            per_class[truth]["found"] += 1
            found += 1
            if prediction == truth:
                per_class[truth]["correct"] += 1
                correct += 1
        if row["end_to_end_ms"].strip():
            latency = float(row["end_to_end_ms"])
            if latency < 0: raise ValueError(f"line {line}: negative latency")
            latencies.append(latency)
    latencies.sort()
    p95 = latencies[max(0, (95 * len(latencies) + 99) // 100 - 1)] if latencies else None
    classes = []
    for digit, values in enumerate(per_class):
        total = values["total"]
        classes.append({"digit": digit, **values,
                        "recall": values["correct"] / total if total else None})
    total = len(rows)
    return {"samples": total, "found": found, "correct": correct,
            "localization_rate": found / total if total else None,
            "overall_accuracy": correct / total if total else None,
            "classification_accuracy_when_found": correct / found if found else None,
            "latency_samples": len(latencies),
            "end_to_end_average_ms": sum(latencies) / len(latencies) if latencies else None,
            "end_to_end_p95_ms": p95, "per_class": classes,
            "confusion_columns": [str(i) for i in range(10)] + ["not_found"],
            "confusion_matrix": matrix,
            "complete_30_per_class": all(item["total"] >= 30 for item in classes)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    result = analyze(args.csv)
    text = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    print(text)
    if args.require_complete and not result["complete_30_per_class"]:
        raise SystemExit("field test incomplete: require at least 30 samples per digit")

if __name__ == "__main__": main()
