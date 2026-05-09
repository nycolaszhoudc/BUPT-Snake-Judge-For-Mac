import subprocess
import re
import os
import math
import time
import sys
from multiprocessing import Pool, cpu_count

if len(sys.argv) < 2:
    print("用法: python3 batch_benchmark.py <AI程序路径1> [AI程序路径2 ...] [轮数]")
    print("例如: python3 batch_benchmark.py ./my_ai_1 ./my_ai_2 10")
    sys.exit(1)

# 解析参数：尝试判断最后一个参数是否为轮数
targets = []
num_runs = 10  # 默认跑10轮

for arg in sys.argv[1:]:
    if arg.isdigit():
        num_runs = int(arg)
    else:
        targets.append(arg)

if not targets:
    print("错误: 请至少提供一个 AI 程序路径进行评测！")
    sys.exit(1)

maps = ["easy", "medium-scatter", "medium-wall", "hard-scatter", "hard-wall", "ultimate"]
n_values = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]

def run_single_benchmark(seed_offset):
    # 保证不同进程拿到不同的基准种子
    env_seed = int(time.time() * 1000) % 1000000 + seed_offset * 123457
    results = {}
    for target in targets:
        scores_float = []
        for difficulty in maps:
            total_weighted_score = 0.0
            for i, n in enumerate(n_values):
                seed = env_seed + hash(difficulty) % 10000 + i * 10
                # 直接调用 snake_judge
                cmd = ["./snake_judge", target, "-d", difficulty, "-m", "random", "-n", str(n), "-s", str(seed)]
                try:
                    res = subprocess.run(cmd, capture_output=True, text=True, errors='ignore', timeout=5)
                    score_match = re.search(r"最终得分:\s*(\d+)", res.stdout)
                    if score_match:
                        score = int(score_match.group(1))
                        total_weighted_score += score * (1.0 / (math.log2(n) + 1))
                except subprocess.TimeoutExpired:
                    pass
            scores_float.append(total_weighted_score)
        
        total = sum(scores_float)
        # 防止计算 log(0)
        geo_mean = math.exp(sum(math.log(max(s, 1e-5)) for s in scores_float) / 6)
        results[target] = {
            "scores": scores_float,
            "total": total,
            "geo_mean": geo_mean
        }
    return results

if __name__ == '__main__':
    print(f"Starting {num_runs} batch benchmark using single core...")
    
    start_time = time.time()
    
    # 改为单进程串行跑
    all_results = []
    for i in range(num_runs):
        res = run_single_benchmark(i)
        all_results.append(res)
        # 使用 sys.stdout.write 和 \r 实现单行实时刷新进度
        sys.stdout.write(f"\rProgress: {i + 1}/{num_runs} runs completed... (Elapsed: {time.time() - start_time:.2f}s)")
        sys.stdout.flush()
        
    print(f"\nFinished in {time.time() - start_time:.2f} seconds.")
    
    stats = {target: {
        "geo_means": [], "totals": [], "max_geo": 0, "max_total": 0,
        "map_scores": [[] for _ in range(6)], "max_map_scores": [0]*6
    } for target in targets}
    
    for res in all_results:
        for target, data in res.items():
            stats[target]["geo_means"].append(data["geo_mean"])
            stats[target]["totals"].append(data["total"])
            stats[target]["max_geo"] = max(stats[target]["max_geo"], data["geo_mean"])
            stats[target]["max_total"] = max(stats[target]["max_total"], data["total"])
            for i in range(6):
                stats[target]["map_scores"][i].append(data["scores"][i])
                stats[target]["max_map_scores"][i] = max(stats[target]["max_map_scores"][i], data["scores"][i])
                
    final_rank = []
    for target in targets:
        if len(stats[target]["geo_means"]) > 0:
            avg_geo = sum(stats[target]["geo_means"]) / len(stats[target]["geo_means"])
            avg_total = sum(stats[target]["totals"]) / len(stats[target]["totals"])
            avg_maps = [sum(stats[target]["map_scores"][i]) / len(stats[target]["map_scores"][i]) for i in range(6)]
            
            final_rank.append({
                "target": target,
                "avg_geo": avg_geo,
                "max_geo": stats[target]["max_geo"],
                "avg_total": avg_total,
                "max_total": stats[target]["max_total"],
                "avg_maps": avg_maps,
                "max_maps": stats[target]["max_map_scores"]
            })
            
    # 按平均均衡得分从高到低排序
    final_rank.sort(key=lambda x: x["avg_geo"], reverse=True)
    
    md = f"# 🐍 贪吃蛇 AI {num_runs}次终极大比拼测评报告\n\n"
    md += f"本报告汇总了 {num_runs} 次随机种子的测试结果，取**平均分**并记录了各模型在测试中的**最好成绩（上限）**。\n\n"
    
    md += "## 🏆 综合能力排行榜 (按平均均衡得分)\n\n"
    md += "| 排名 | AI 解决方案 | 平均均衡得分 (GeoMean) | 最高均衡得分 | 平均算术总分 | 最高算术总分 |\n"
    md += "|:---:|---|:---:|:---:|:---:|:---:|\n"
    
    for i, item in enumerate(final_rank):
        medal = "🥇" if i == 0 else "🥈" if i == 1 else "🥉" if i == 2 else f"{i+1}"
        md += f"| {medal} | `{item['target']}` | **{item['avg_geo']:.2f}** | {item['max_geo']:.2f} | {item['avg_total']:.2f} | {item['max_total']:.2f} |\n"
        
    md += "\n## 🗺️ 各地图平均表现与上限 (平均分 / **最高分**)\n\n"
    md += "| 排名 | AI 解决方案 | Easy | Medium (Scatter) | Medium (Wall) | Hard (Scatter) | Hard (Wall) | Ultimate |\n"
    md += "|:---:|---|:---:|:---:|:---:|:---:|:---:|:---:|\n"
    for i, item in enumerate(final_rank):
        medal = "🥇" if i == 0 else "🥈" if i == 1 else "🥉" if i == 2 else f"{i+1}"
        maps_str = " | ".join([f"{avg:.2f} / **{m:.2f}**" for avg, m in zip(item["avg_maps"], item["max_maps"])])
        md += f"| {medal} | `{item['target']}` | {maps_str} |\n"
        
    with open(f"AI_Solutions_Benchmark_{num_runs}.md", "w", encoding="utf-8") as f:
        f.write(md)
        
    print(f"\nBenchmark complete! Results saved to AI_Solutions_Benchmark_{num_runs}.md")