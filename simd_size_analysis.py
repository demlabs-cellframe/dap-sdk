#!/usr/bin/env python3
"""
SIMD Context-Dependency Analysis
Day 2 Afternoon Session - Investigating SIMD performance across different data sizes
"""

import subprocess
import time
import statistics
import json
import sys
from datetime import datetime
from typing import List, Dict, Tuple
import re
# import matplotlib.pyplot as plt  # Not needed for this analysis
# import numpy as np

class SIMDSizeAnalyzer:
    def __init__(self):
        self.results = {
            "timestamp": datetime.now().isoformat(),
            "analysis_type": "SIMD_SIZE_DEPENDENCY",
            "tests": {},
            "analysis": {}
        }
        
    def create_test_with_size(self, size: int, test_type: str = "simd") -> str:
        """Создает временный тест с заданным размером данных"""
        template = f"""
#include <stdio.h>
#include <time.h>
#include <arm_neon.h>

#define TEST_SIZE {size}
#define ITERATIONS 1000

void simd_pointwise_test(float* a, float* b, float* result, int size) {{
    int simd_blocks = size / 4;
    int remainder = size % 4;
    
    for (int i = 0; i < simd_blocks; i++) {{
        float32x4_t va = vld1q_f32(&a[i * 4]);
        float32x4_t vb = vld1q_f32(&b[i * 4]);
        float32x4_t vresult = vmulq_f32(va, vb);
        vst1q_f32(&result[i * 4], vresult);
    }}
    
    // Handle remainder
    for (int i = simd_blocks * 4; i < size; i++) {{
        result[i] = a[i] * b[i];
    }}
}}

void scalar_pointwise_test(float* a, float* b, float* result, int size) {{
    for (int i = 0; i < size; i++) {{
        result[i] = a[i] * b[i];
    }}
}}

int main() {{
    float a[TEST_SIZE], b[TEST_SIZE], result[TEST_SIZE];
    
    // Initialize data
    for (int i = 0; i < TEST_SIZE; i++) {{
        a[i] = (float)i * 1.5f;
        b[i] = (float)i * 2.5f;
    }}
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int iter = 0; iter < ITERATIONS; iter++) {{
        {'simd_pointwise_test' if test_type == 'simd' else 'scalar_pointwise_test'}(a, b, result, TEST_SIZE);
    }}
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double total_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double avg_time = total_time / ITERATIONS;
    double ops_per_sec = ITERATIONS / total_time;
    
    printf("Performance Results:\\n");
    printf("- Data size: %d elements\\n", TEST_SIZE);
    printf("- Iterations: %d\\n", ITERATIONS);
    printf("- Total time: %.2f ms\\n", total_time * 1000);
    printf("- Average time per operation: %.2f μs\\n", avg_time * 1000000);
    printf("- Operations per second: %.0f\\n", ops_per_sec);
    printf("- Test type: %s\\n", "{test_type}");
    
    return 0;
}}
"""
        
        filename = f"temp_test_{test_type}_{size}.c"
        with open(filename, 'w') as f:
            f.write(template)
        return filename
    
    def compile_and_run_test(self, source_file: str, size: int, test_type: str, iterations: int = 10) -> List[Dict]:
        """Компилирует и запускает тест заданное количество раз"""
        executable = source_file.replace('.c', '')
        
        # Компиляция
        compile_cmd = f"gcc -O3 -march=native -o {executable} {source_file}"
        result = subprocess.run(compile_cmd, shell=True, capture_output=True, text=True)
        
        if result.returncode != 0:
            raise Exception(f"Compilation failed: {result.stderr}")
        
        # Запуск тестов
        results = []
        print(f"    Testing {test_type} with {size} elements ({iterations} iterations)...")
        
        for i in range(iterations):
            start_time = time.time()
            run_result = subprocess.run([f'./{executable}'], capture_output=True, text=True, timeout=30)
            end_time = time.time()
            
            if run_result.returncode != 0:
                print(f"    ❌ Iteration {i+1} failed: {run_result.stderr}")
                continue
                
            # Парсинг результатов
            output = run_result.stdout
            times = {'wallclock_time_s': end_time - start_time}
            
            # Поиск метрик
            total_time_match = re.search(r'Total time:\s+(\d+\.\d+)\s*ms', output)
            if total_time_match:
                times['total_time_ms'] = float(total_time_match.group(1))
                
            avg_time_match = re.search(r'Average time per operation:\s+(\d+\.\d+)\s*μs', output)
            if avg_time_match:
                times['avg_per_op_us'] = float(avg_time_match.group(1))
                
            ops_per_sec_match = re.search(r'Operations per second:\s+(\d+)', output)
            if ops_per_sec_match:
                times['ops_per_sec'] = float(ops_per_sec_match.group(1))
            
            results.append({
                'iteration': i,
                'size': size,
                'test_type': test_type,
                'times': times,
                'output': output
            })
            
        # Cleanup
        subprocess.run(f"rm -f {source_file} {executable}", shell=True)
        return results
    
    def analyze_size_performance(self, sizes: List[int] = None, iterations: int = 10) -> Dict:
        """Анализирует производительность SIMD vs scalar для различных размеров данных"""
        if sizes is None:
            sizes = [64, 128, 256, 512, 1024, 2048, 4096]
        
        analysis = {}
        all_results = {}
        
        print(f"\n🔍 SIMD SIZE DEPENDENCY ANALYSIS")
        print(f"Testing sizes: {sizes}")
        print(f"Iterations per test: {iterations}")
        print()
        
        for size in sizes:
            print(f"📊 Testing size: {size} elements")
            
            # Test SIMD
            try:
                simd_source = self.create_test_with_size(size, "simd")
                simd_results = self.compile_and_run_test(simd_source, size, "simd", iterations)
                
                scalar_source = self.create_test_with_size(size, "scalar")
                scalar_results = self.compile_and_run_test(scalar_source, size, "scalar", iterations)
                
                # Анализируем результаты для этого размера
                if simd_results and scalar_results:
                    size_analysis = self.analyze_size_results(simd_results, scalar_results, size)
                    analysis[size] = size_analysis
                    all_results[size] = {
                        'simd': simd_results,
                        'scalar': scalar_results
                    }
                    
                    print(f"    ✅ Size {size}: SIMD vs Scalar = {size_analysis.get('performance_ratio', 'N/A'):.3f}x")
                    
            except Exception as e:
                print(f"    ❌ Size {size} failed: {e}")
                continue
        
        self.results['tests'] = all_results
        self.results['analysis'] = analysis
        
        return analysis
    
    def analyze_size_results(self, simd_results: List[Dict], scalar_results: List[Dict], size: int) -> Dict:
        """Анализирует результаты для конкретного размера"""
        if not simd_results or not scalar_results:
            return {}
        
        # Извлекаем ops_per_sec для анализа
        simd_ops = [r['times'].get('ops_per_sec', 0) for r in simd_results if 'ops_per_sec' in r['times']]
        scalar_ops = [r['times'].get('ops_per_sec', 0) for r in scalar_results if 'ops_per_sec' in r['times']]
        
        if not simd_ops or not scalar_ops:
            return {}
        
        simd_mean = statistics.mean(simd_ops)
        scalar_mean = statistics.mean(scalar_ops)
        
        return {
            'size': size,
            'simd_ops_per_sec': {
                'mean': simd_mean,
                'stdev': statistics.stdev(simd_ops) if len(simd_ops) > 1 else 0,
                'count': len(simd_ops)
            },
            'scalar_ops_per_sec': {
                'mean': scalar_mean,
                'stdev': statistics.stdev(scalar_ops) if len(scalar_ops) > 1 else 0,
                'count': len(scalar_ops)
            },
            'performance_ratio': simd_mean / scalar_mean if scalar_mean > 0 else 0,
            'simd_advantage': ((simd_mean - scalar_mean) / scalar_mean * 100) if scalar_mean > 0 else 0
        }
    
    def generate_report(self, analysis: Dict) -> str:
        """Генерирует отчет по размерной зависимости"""
        report = []
        report.append("\n" + "="*80)
        report.append("📊 SIMD SIZE DEPENDENCY ANALYSIS REPORT")
        report.append("="*80)
        report.append(f"Timestamp: {self.results['timestamp']}")
        report.append("")
        
        # Сортируем по размеру
        sorted_sizes = sorted(analysis.keys())
        
        report.append("📈 Performance Ratio (SIMD/Scalar) by Data Size:")
        report.append("-" * 60)
        
        for size in sorted_sizes:
            data = analysis[size]
            ratio = data.get('performance_ratio', 0)
            advantage = data.get('simd_advantage', 0)
            
            status = "🟢 ADVANTAGE" if ratio > 1.0 else "🔴 DISADVANTAGE"
            report.append(f"  {size:>4} elements: {ratio:.3f}x ({advantage:+.1f}%) {status}")
        
        # Находим break-even point
        break_even = None
        for size in sorted_sizes:
            if analysis[size].get('performance_ratio', 0) > 1.0:
                break_even = size
                break
        
        report.append("")
        if break_even:
            report.append(f"⚡ SIMD Break-even Point: ~{break_even} elements")
            report.append(f"   SIMD becomes advantageous at {break_even}+ elements")
        else:
            report.append("⚠️  SIMD shows disadvantage across all tested sizes")
            report.append("   Consider investigating larger sizes or different workloads")
        
        return "\n".join(report)
    
    def save_results(self, filename: str = "simd_size_analysis.json"):
        """Сохраняет результаты анализа"""
        with open(filename, 'w') as f:
            json.dump(self.results, f, indent=2)
        print(f"📄 Analysis saved to {filename}")

def main():
    print("🎯 SIMD SIZE DEPENDENCY ANALYSIS")
    print("================================")
    print("Day 2 Afternoon Session - SIMD Context Investigation")
    print()
    
    analyzer = SIMDSizeAnalyzer()
    
    # Тестируем различные размеры
    test_sizes = [64, 128, 256, 512, 1024, 2048, 4096]
    iterations = 10  # Меньше итераций для экономии времени
    
    try:
        analysis = analyzer.analyze_size_performance(test_sizes, iterations)
        
        if analysis:
            report = analyzer.generate_report(analysis)
            print(report)
            
            analyzer.save_results()
            
            print("\n✅ Size dependency analysis completed!")
            print("📊 Check simd_size_analysis.json for detailed results")
        else:
            print("❌ No valid results obtained")
            
    except KeyboardInterrupt:
        print("\n⚠️  Analysis interrupted by user")
        analyzer.save_results("simd_size_partial.json")
    except Exception as e:
        print(f"\n❌ Analysis failed: {e}")
        return 1
        
    return 0

if __name__ == "__main__":
    exit(main()) 