import subprocess
import os
import csv
import itertools

# Parameter candidates
chain_depth_list = [8, 16, 24, 32]
window_size_list = [32768, 65535]
min_dist_len3_list = [32, 64, 96, 128]
bias_permille_list = [990, 995, 1000]

def run_bench(chain_depth, window_size, min_dist_len3, bias_permille, runs=1, warmup=0):
    env = os.environ.copy()
    env["HKN_LZ_CHAIN_DEPTH"] = str(chain_depth)
    env["HKN_LZ_WINDOW_SIZE"] = str(window_size)
    env["HKN_LZ_MIN_DIST_LEN3"] = str(min_dist_len3)
    env["HKN_LZ_BIAS_PERMILLE"] = str(bias_permille)
    
    # Prefilter thresholds from previous best
    env["HKN_NATURAL_UNIQUE_MIN"] = "64"
    env["HKN_NATURAL_AVG_RUN_MAX"] = "460"
    env["HKN_NATURAL_MAD_MIN"] = "20"
    env["HKN_NATURAL_ENTROPY_MIN"] = "5"
    
    temp_csv = f"bench_results/tmp_lz_{chain_depth}_{window_size}_{min_dist_len3}_{bias_permille}.csv"
    cmd = [
        "./build/bench_png_compare",
        "--runs", str(runs),
        "--warmup", str(warmup),
        "--out", temp_csv
    ]
    
    try:
        result = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if result.returncode != 0:
            return None
        
        # Parse result from stdout (median ratio)
        lines = result.stdout.splitlines()
        median_ratio = 0.0
        for line in lines:
            if "median(PNG_bytes/HKN_bytes):" in line:
                median_ratio = float(line.split(":")[-1].strip())
                break
        
        # Parse per-image ratios and metrics from CSV
        image_data = {}
        with open(temp_csv, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                image_data[row['image_name']] = {
                    "ratio": float(row['png_over_hkn']),
                    "hkn_bytes": int(row['hkn_bytes']),
                    "dec_ms": float(row['dec_ms']),
                    "natural_row_selected": int(row['natural_row_selected']),
                    "gain_bytes": int(row['gain_bytes']),
                    "loss_bytes": int(row['loss_bytes'])
                }
                
        # Cleanup
        os.remove(temp_csv)
        
        return {
            "params": (chain_depth, window_size, min_dist_len3, bias_permille),
            "median_ratio": median_ratio,
            "image_data": image_data
        }
    except Exception:
        return None

def main():
    if not os.path.exists("bench_results"):
        os.makedirs("bench_results")

    combinations = list(itertools.product(
        chain_depth_list, window_size_list, min_dist_len3_list, bias_permille_list
    ))
    
    print(f"Starting sweep of {len(combinations)} combinations...")
    
    results = []
    for i, c in enumerate(combinations):
        res = run_bench(*c)
        if res:
            results.append(res)
        if (i + 1) % 10 == 0:
            print(f"Progress: {i + 1}/{len(combinations)}")

    # Sort by median_ratio descending
    results.sort(key=lambda x: x["median_ratio"], reverse=True)
    
    with open("bench_results/phase9w_routeparam_sweep_raw.csv", "w", newline="") as f:
        writer = csv.writer(f)
        if results:
            img_names = sorted(results[0]["image_data"].keys())
            header = ["chain_depth", "window_size", "min_dist_len3", "bias_permille", "median_ratio"]
            for name in img_names:
                header.append(f"{name}_ratio")
                header.append(f"{name}_hkn_bytes")
                header.append(f"{name}_dec_ms")
            writer.writerow(header)
            for r in results:
                row = list(r["params"]) + [r["median_ratio"]]
                for name in img_names:
                    d = r["image_data"][name]
                    row.extend([d["ratio"], d["hkn_bytes"], d["dec_ms"]])
                writer.writerow(row)

    print("\nTop 5 results (Crude search):")
    for r in results[:5]:
        print(f"Params: {r['params']}, Median Ratio: {r['median_ratio']:.4f}")

    # Refine top 5
    print("\nRefining top 5 results with 3 runs...")
    refined_results = []
    for r in results[:5]:
        res = run_bench(*r["params"], runs=3, warmup=1)
        if res:
            refined_results.append(res)
    
    refined_results.sort(key=lambda x: x["median_ratio"], reverse=True)
    
    print("\nTop results (Refined):")
    for r in refined_results:
        print(f"Params: {r['params']}, Median Ratio: {r['median_ratio']:.4f}")
        
    if refined_results:
        best = refined_results[0]
        print(f"\nBest params found: {best['params']}")
        print(f"Best median ratio: {best['median_ratio']:.4f}")

if __name__ == "__main__":
    main()
