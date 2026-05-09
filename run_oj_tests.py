import subprocess
import re
import sys
import math
import random
import time

# 每次运行生成一个新的随机基准种子，但允许从外部传入统一基准种子以保证 AI 间横向对比公平
base_seed = int(time.time() * 1000) % 1000000

executable = "./snake_ai_rl_feature"
if len(sys.argv) > 1:
    executable = sys.argv[1]
if len(sys.argv) > 2:
    base_seed = int(sys.argv[2])

n_values = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
difficulties = ["easy", "medium-scatter", "medium-wall", "hard-scatter", "hard-wall", "ultimate"]

for difficulty in difficulties:
    print(f"\n\n开始按照 OJ 规范执行 10 组不同 N 值的测试 (地图: {difficulty})")
    print("=" * 85)
    print(f"{'组别':<5} | {'N值':<4} | {'原始分':<8} | {'权重':<6} | {'加权得分':<8} | {'存活步数':<8} | {'最终蛇长':<8} | {'结果':<6}")
    print("-" * 85)

    total_weighted_score = 0.0

    for i, n in enumerate(n_values):
        # 采用时间基准种子 + 难度 hash + N 值作为种子，确保每次大比拼的各 AI 在面对同一组时地图是一致的，
        # 但每次运行 benchmark_all.py 时地图都会变化。
        seed = base_seed + hash(difficulty) % 10000 + i * 10
        cmd = f"./snake_judge {executable} -d {difficulty} -m random -n {n} -s {seed}"
        
        weight = 1.0 / (math.log2(n) + 1)
        
        try:
            # OJ 测评一般有时间限制，这里设置 15 秒超时
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=15)
        except subprocess.TimeoutExpired:
            print(f"{i+1:02d}    | {n:<4} | TIMEOUT  | {weight:<6.3f} | -        | -        | -        | ❌ FAIL")
            continue

        score = 0
        steps = 0
        length = 0
        
        score_match = re.search(r"最终得分: (\d+)", result.stdout)
        step_match = re.search(r"存活步数: (\d+)", result.stdout)
        len_match = re.search(r"最终蛇长: (\d+)", result.stdout)
        
        if score_match: score = int(score_match.group(1))
        if step_match: steps = int(step_match.group(1))
        if len_match: length = int(len_match.group(1))
        
        weighted_score = score * weight
        total_weighted_score += weighted_score
        
        status = "💀 DEAD" # 因为现在是无尽模式，一定会死
            
        print(f"{i+1:02d}    | {n:<4} | {score:<10} | {weight:<6.3f} | {weighted_score:<12.2f} | {steps:<10} | {length:<10} | {status}")

    print("=" * 85)
    print(f"加权总分: {total_weighted_score:.2f}")
