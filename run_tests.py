import subprocess
import re
import sys

if len(sys.argv) < 2:
    print("用法: python3 run_tests.py <AI程序路径>")
    sys.exit(1)

target = sys.argv[1]

for seed in range(10):
    cmd = f"./snake_judge {target} -d medium-scatter -m random -n 20 -s {seed}"
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    match = re.search(r"最终得分: (\d+)", result.stdout)
    if match:
        score = int(match.group(1))
        print(f"Seed {seed}: Score {score}")
        if score >= 500:
            print("SUCCESS >= 500")
            break
