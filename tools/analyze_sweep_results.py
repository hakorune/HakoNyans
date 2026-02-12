import csv

with open("bench_results/phase9w_threshold_sweep_expanded.csv", "r") as f:
    reader = csv.DictReader(f)
    rows = list(reader)

for row in rows:
    for key in row:
        if key != 'image_id' and key != 'image_name':
            row[key] = float(row[key])

best_sum = -1
best_row = None

baseline_nature_01 = 1.567939
baseline_nature_02 = 1.442787
cols = ['hd_01', 'kodim01', 'kodim02', 'kodim03', 'nature_01', 'nature_02']

for row in rows:
    if row['nature_01'] < baseline_nature_01 * 0.97: continue
    if row['nature_02'] < baseline_nature_02 * 0.97: continue
    
    s = sum(row[c] for c in cols)
    if s > best_sum:
        best_sum = s
        best_row = row

print("Best constrained row:")
print(best_row)
