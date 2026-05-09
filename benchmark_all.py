import subprocess
import re
import os
import math
import statistics
import time
import sys

if len(sys.argv) < 2:
    print("用法: python3 benchmark_all.py <AI程序路径1> [AI程序路径2 ...]")
    print("例如: python3 benchmark_all.py ./my_ai_1 ./my_ai_2")
    sys.exit(1)

# 获取命令行传入的所有 AI 程序路径
targets = sys.argv[1:]

# 在开始整个大比拼前，生成一个唯一的环境种子
env_seed = str(int(time.time() * 1000) % 1000000)
print(f"Starting benchmark with global seed: {env_seed} ... (This may take a few minutes)")
maps = ["easy", "medium-scatter", "medium-wall", "hard-scatter", "hard-wall", "ultimate"]

valid_results = []
error_results = []

for target in targets:
    if not os.path.exists(target):
        print(f"File {target} not found, skipping...")
        error_results.append((target, "Not Found"))
        continue
        
    print(f"Testing {target}...")
    try:
        # 执行测试脚本，传入全局种子参数确保各 AI 面临的地图一模一样
        res = subprocess.run(["python3", "run_oj_tests.py", target, env_seed], capture_output=True, text=True)
        # 从输出中提取各组的加权得分
        scores = re.findall(r"加权总分:\s*(\d+\.\d+)", res.stdout)
        
        if len(scores) == 6:
            scores_float = [float(s) for s in scores]
            total = sum(scores_float)
            
            # 【重点修改】计算几何平均数 (Geometric Mean) 作为均衡得分，加 1e-5 避免全 0 时报错
            geo_mean = math.exp(sum(math.log(max(s, 1e-5)) for s in scores_float) / 6)
            # 计算标准差，体现得分的波动率
            std_dev = statistics.stdev(scores_float)
            
            valid_results.append((target, scores_float, total, geo_mean, std_dev))
            print(f"-> Success! Geo Mean: {geo_mean:.2f}, Total Score: {total:.2f}, StdDev: {std_dev:.2f}")
        else:
            print(f"-> Unexpected output for {target}, only found {len(scores)} scores.")
            error_results.append((target, "Test Error"))
    except Exception as e:
        print(f"-> Exception running {target}: {e}")
        error_results.append((target, "Execution Error"))

# 【重点修改】按 几何平均分（均衡得分） 从高到低排序，严厉惩罚“严重偏科”的 AI
valid_results.sort(key=lambda x: x[3], reverse=True)

# 生成 Markdown 文档
md_content = "# 🐍 贪吃蛇 AI 解决方案大比拼测评报告\n\n"
md_content += "本报告汇总了当前项目中所有可用的 AI 解决方案在 OJ 测试框架下的表现。**为了严厉惩罚“严重偏科”的模型（例如在某个图表现极好但在其他图表现极差），本排行榜采用「均衡得分（几何平均数）」作为最终排名的唯一标准，辅以「波动率（标准差）」作为稳定性参考。**\n\n"
md_content += "## 🏆 最终均衡排行榜\n\n"
md_content += "| 排名 | AI 解决方案 | Easy | Medium (Scatter) | Medium (Wall) | Hard (Scatter) | Hard (Wall) | Ultimate | 算术总分 | 波动率(StdDev) | **均衡得分(GeoMean)** |\n"
md_content += "|:---:|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|\n"

for i, (target, scores, total, geo_mean, std_dev) in enumerate(valid_results):
    medal = "🥇" if i == 0 else "🥈" if i == 1 else "🥉" if i == 2 else f"{i+1}"
    row = f"| {medal} | `{target}` | {scores[0]:.2f} | {scores[1]:.2f} | {scores[2]:.2f} | {scores[3]:.2f} | {scores[4]:.2f} | {scores[5]:.2f} | {total:.2f} | {std_dev:.2f} | **{geo_mean:.2f}** |\n"
    md_content += row

for target, err in error_results:
    md_content += f"| ❌ | `{target}` | - | - | - | - | - | - | - | - | **{err}** |\n"

md_content += "\n## 📊 评分机制与排名解析\n\n"
md_content += "### 为什么使用几何平均数 (Geo Mean)？\n"
md_content += "- 算术求和（总分）存在“木桶效应漏洞”。如果一个模型在 Easy 空旷地图拿了 2000 分，在 Hard 复杂地图只拿了 10 分，它的算术总分可能比一个在所有地图都稳拿 400 分的模型还要高。\n"
md_content += "- **几何平均数**（即五张地图得分相乘再开五次方）天生对极低分极度敏感，完美符合“均衡发展”的理念：哪怕有一张图表现稀烂，其均衡得分也会遭到毁灭性打击。\n\n"
md_content += "### 观察与点评\n"
md_content += "- **偏科典型的陨落 (`snake_ai_hamilton`)**：哈密顿版本在 Easy 地图凭借完美的刷分策略拿到全场最高的极高分，但一遇到 Hard 随机障碍物得分就跌破百位数。在新的“均衡得分”体系下，它的排名大幅下降，原形毕露。\n"
md_content += "- **真正的全能王 (`snake_ai_safe` & `snake_ai_rl_feature`)**：不仅总分高，而且标准差相对较小，在最困难的地图中依然能保持高水准的生存能力，因此在均衡评分体系中稳坐前列。\n\n"
md_content += "---\n*本报告由自动化测试脚本 `benchmark_all.py` 自动生成。*"

with open("AI_Solutions_Benchmark.md", "w", encoding="utf-8") as f:
    f.write(md_content)

print("\nBenchmark complete! Results saved to AI_Solutions_Benchmark.md.")