import subprocess
import os
import csv
import itertools
import statistics

# Threshold candidates - Expanded range
unique_min_list = [64, 96, 128, 160]
avg_run_max_list = [260, 360, 460, 560]
mad_min_list = [20, 50, 80, 120]
entropy_min_list = [5, 10, 20, 40]

def run_bench(unique_min, avg_run_max, mad_min, entropy_min, runs=1, warmup=0):
    env = os.environ.copy()
    env["HKN_NATURAL_UNIQUE_MIN"] = str(unique_min)
    env["HKN_NATURAL_AVG_RUN_MAX"] = str(avg_run_max)
    env["HKN_NATURAL_MAD_MIN"] = str(mad_min)
    env["HKN_NATURAL_ENTROPY_MIN"] = str(entropy_min)
    
    temp_csv = f"bench_results/tmp_{unique_min}_{avg_run_max}_{mad_min}_{entropy_min}.csv"
    cmd = [
        "./build/bench_png_compare",
        "--runs", str(runs),
        "--warmup", str(warmup),
        "--out", temp_csv
    ]
    
    try:
        result = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if result.returncode != 0:
            print("Error for " + str((unique_min, avg_run_max, mad_min, entropy_min)))
            print(result.stderr.strip())
            return None

        # Parse per-image metrics from CSV
        image_ratios = {}
        image_hkn = {}
        with open(temp_csv, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                image_ratios[row['image_name']] = float(row['png_over_hkn'])
                image_hkn[row['image_name']] = int(row['hkn_bytes'])

        ratio_values = list(image_ratios.values())
        median_ratio = statistics.median(ratio_values) if ratio_values else 0.0
        kodim_keys = [k for k in ("kodim01", "kodim02", "kodim03") if k in image_ratios]
        kodim_mean = (sum(image_ratios[k] for k in kodim_keys) / len(kodim_keys)) if kodim_keys else 0.0
        total_hkn = sum(image_hkn.values())

        # Cleanup
        os.remove(temp_csv)

        return {
            "params": (unique_min, avg_run_max, mad_min, entropy_min),
            "median_ratio": median_ratio,
            "kodim_mean": kodim_mean,
            "total_hkn": total_hkn,
            "image_ratios": image_ratios,
            "image_hkn": image_hkn,
        }
    except Exception as e:
        print("Exception for " + str((unique_min, avg_run_max, mad_min, entropy_min)) + ": " + str(e))
        return None

def main():
    if not os.path.exists("bench_results"):
        os.makedirs("bench_results")

    combinations = list(itertools.product(
        unique_min_list, avg_run_max_list, mad_min_list, entropy_min_list
    ))
    
    print("Starting expanded sweep of " + str(len(combinations)) + " combinations...")
    
    results = []
    for i, c in enumerate(combinations):
        res = run_bench(*c)
        if res:
            results.append(res)
        if (i + 1) % 50 == 0:
            print("Progress: " + str(i + 1) + "/" + str(len(combinations)))

    # Rank by median(PNG/HKN), then Kodak average, then total HKN bytes.
    results.sort(
        key=lambda x: (x["median_ratio"], x["kodim_mean"], -x["total_hkn"]),
        reverse=True
    )
    
    with open("bench_results/phase9w_threshold_sweep_expanded.csv", "w", newline="") as f:
        writer = csv.writer(f)
        if results:
            img_names = sorted(results[0]["image_ratios"].keys())
            header = [
                "unique_min", "avg_run_max", "mad_min", "entropy_min",
                "median_ratio", "kodim_mean_ratio", "total_hkn_bytes"
            ] + img_names + [f"hkn_{name}" for name in img_names]
            writer.writerow(header)
            for r in results:
                row = list(r["params"]) + [r["median_ratio"], r["kodim_mean"], r["total_hkn"]]
                row += [r["image_ratios"][name] for name in img_names]
                row += [r["image_hkn"][name] for name in img_names]
                writer.writerow(row)

    print("Top 5 results:")
    for r in results[:5]:
        print(
            "Params: " + str(r['params']) +
            ", Median Ratio: " + str(round(r['median_ratio'], 6)) +
            ", Kodak Mean: " + str(round(r['kodim_mean'], 6)) +
            ", Total HKN: " + str(r['total_hkn'])
        )

if __name__ == "__main__":
    main()
