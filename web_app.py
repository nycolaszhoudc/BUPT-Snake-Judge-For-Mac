import os
import subprocess
import re
import math
import random
import time
from flask import Flask, request, jsonify, render_template

app = Flask(__name__)

# Serve the UI
@app.route('/')
def index():
    return render_template('index.html')

# Compile code to executable
@app.route('/api/compile', methods=['POST'])
def compile_code():
    data = request.get_json()
    code = data.get('code', '')
    if not code:
        return jsonify({"success": False, "error": "No code provided"})

    # Check code length limit (24KB = 24 * 1024 Bytes)
    if len(code.encode('utf-8')) > 24 * 1024:
        return jsonify({
            "success": False, 
            "error": "Compilation failed", 
            "logs": "错误：代码长度超出 24KB 限制！"
        })

    # Save code to temp_ai.c
    with open('temp_ai.c', 'w') as f:
        f.write(code)

    # Compile using gcc
    compile_cmd = ["gcc", "temp_ai.c", "-o", "temp_ai", "-O2", "-Wall", "-lm"]
    try:
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            return jsonify({
                "success": False,
                "error": "Compilation failed",
                "logs": result.stderr
            })
        return jsonify({
            "success": True,
            "logs": result.stderr if result.stderr else "Compilation successful."
        })
    except Exception as e:
        return jsonify({"success": False, "error": str(e)})

# Debug run
@app.route('/api/debug', methods=['POST'])
def debug_run():
    data = request.get_json() or {}
    # Optional arguments from request (e.g. map type, N, seed)
    args = data.get('args', [])
    difficulty = data.get('difficulty', 'medium-scatter')
    
    cmd = ["./snake_judge", "./temp_ai", "-d", difficulty, "-v"] + args
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=10)
        output = result.stdout + "\n" + result.stderr
        
        # Parse metadata
        score_match = re.search(r"最终得分: (\d+)", output)
        step_match = re.search(r"存活步数: (\d+)", output)
        len_match = re.search(r"最终蛇长: (\d+)", output)
        seed_match = re.search(r"Seed: (\d+)", output)
        
        score = int(score_match.group(1)) if score_match else 0
        step = int(step_match.group(1)) if step_match else 0
        length = int(len_match.group(1)) if len_match else 0
        seed = int(seed_match.group(1)) if seed_match else -1
        
        # Parse frames (blocks of 20 lines of exactly 20 map characters)
        # Using a simple line-by-line parser
        frames = []
        current_frame = []
        lines = output.splitlines()
        for line in lines:
            line = line.strip()
            # Stop parsing frames when we hit the final map sections
            if line == "最终地图:" or line == "评测机客观最终地图:":
                break
                
            # Map lines are exactly 20 chars of specific symbols
            if len(line) == 20 and re.match(r'^[#\.HBFOW\w]+$', line):
                current_frame.append(line)
                if len(current_frame) == 20:
                    frames.append(current_frame)
                    current_frame = []
            else:
                current_frame = []
                
        return jsonify({
            "success": True,
            "metadata": {
                "score": score,
                "step": step,
                "length": length,
                "seed": seed
            },
            "frames": frames,
            "raw_output": output
        })
    except Exception as e:
        return jsonify({"success": False, "error": str(e)})

# Benchmark run
@app.route('/api/benchmark', methods=['POST'])
def benchmark_run():
    if not os.path.exists("./temp_ai"):
        return jsonify({'success': False, 'error': 'AI not compiled yet. Please compile first.'})
        
    data = request.get_json() or {}
    difficulty = data.get('difficulty', 'medium-scatter')

    n_values = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
    total_weighted_score = 0.0
    results = []
    
    base_seed = int(time.time() * 1000) % 1000000
    
    for i, n in enumerate(n_values):
        seed = base_seed + i * 10
        cmd = ["./snake_judge", "./temp_ai", "-d", difficulty, "-n", str(n), "-s", str(seed), "-v"]
        weight = 1.0 / (math.log2(n) + 1)
        
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=15)
            output = res.stdout + "\n" + res.stderr
            
            score_match = re.search(r"最终得分: (\d+)", output)
            score = int(score_match.group(1)) if score_match else 0
            
            # Check for protocol error
            if "结束协议违规(计0分)" in output or "错误: 未能" in output or "错误: AI" in output:
                score = 0
                error_msg = "PROTOCOL ERROR (Score set to 0)"
                results.append({
                    "n": n,
                    "seed": seed,
                    "raw_score": 0,
                    "weight": weight,
                    "weighted_score": 0,
                    "error": error_msg,
                    "frames": []
                })
                continue
            
            weighted_score = score * weight
            total_weighted_score += weighted_score
            
            # Parse frames
            frames = []
            current_frame = []
            lines = output.splitlines()
            for line in lines:
                line = line.strip()
                if line == "最终地图:" or line == "评测机客观最终地图:":
                    break
                    
                if len(line) == 20 and re.match(r'^[#\.HBFOW\w]+$', line):
                    current_frame.append(line)
                    if len(current_frame) == 20:
                        frames.append(current_frame)
                        current_frame = []
                else:
                    current_frame = []
            
            results.append({
                "n": n,
                "seed": seed,
                "raw_score": score,
                "weight": weight,
                "weighted_score": weighted_score,
                "frames": frames
            })
        except subprocess.TimeoutExpired:
            results.append({
                "n": n,
                "seed": seed,
                "raw_score": 0,
                "weight": weight,
                "weighted_score": 0,
                "error": "TIMEOUT",
                "frames": []
            })
        except Exception as e:
            results.append({
                "n": n,
                "seed": seed,
                "raw_score": 0,
                "weight": weight,
                "weighted_score": 0,
                "error": str(e),
                "frames": []
            })

    # Map to 0-50 grade
    grade = 0
    if total_weighted_score >= 500:
        grade = 50
    elif total_weighted_score >= 400:
        grade = 45
    elif total_weighted_score >= 300:
        grade = 40
    elif total_weighted_score >= 200:
        grade = 35
    elif total_weighted_score >= 100:
        grade = 30
    else:
        grade = 0

    return jsonify({
        "success": True,
        "total_weighted_score": total_weighted_score,
        "grade": grade,
        "details": results
    })

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5050)
