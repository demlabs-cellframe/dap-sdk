#!/usr/bin/env python3
"""
Автоматизированная статистическая валидация SIMD оптимизации Chipmunk
Day 2 Morning Session - Statistical Validation Framework
"""

import subprocess
import time
import statistics
import json
import sys
from datetime import datetime
from typing import List, Dict, Tuple
import re

class ChipmunkStatisticalValidator:
    def __init__(self):
        self.results = {
            "timestamp": datetime.now().isoformat(),
            "tests": {},
            "analysis": {}
        }
        
    def run_single_test(self, executable: str, iteration: int) -> Dict:
        """Запускает один тест и парсит результаты"""
        print(f"    Running {executable} iteration {iteration+1}...")
        
        start_time = time.time()
        result = subprocess.run([f'./{executable}'], 
                               capture_output=True, text=True, timeout=60)
        end_time = time.time()
        
        if result.returncode != 0:
            raise Exception(f"Test failed: {result.stderr}")
            
        # Парсинг времени выполнения из вывода
        output = result.stdout
        times = {}
        
        # Поиск общего времени теста
        total_time_match = re.search(r'Total time:\s+(\d+\.\d+)\s*ms', output)
        if total_time_match:
            times['total_test_time_ms'] = float(total_time_match.group(1))
            times['total_test_time_s'] = float(total_time_match.group(1)) / 1000.0
            
        # Поиск среднего времени на операцию
        avg_time_match = re.search(r'Average time per operation:\s+(\d+\.\d+)\s*[μu]s', output)
        if avg_time_match:
            times['avg_per_operation_us'] = float(avg_time_match.group(1))
            times['avg_per_operation_s'] = float(avg_time_match.group(1)) / 1000000.0
            
        # Поиск операций в секунду
        ops_per_sec_match = re.search(r'Operations per second:\s+(\d+)', output)
        if ops_per_sec_match:
            times['operations_per_second'] = float(ops_per_sec_match.group(1))
            
        # Wallclock время (реальное время выполнения)
        times['wallclock_time_s'] = end_time - start_time
        
        return {
            'iteration': iteration,
            'times': times,
            'output': output,
            'wallclock': end_time - start_time
        }
    
    def run_test_series(self, executable: str, iterations: int = 25) -> List[Dict]:
        """Запускает серию тестов для статистического анализа"""
        print(f"\n🔄 Running {iterations} iterations of {executable}...")
        results = []
        
        for i in range(iterations):
            try:
                result = self.run_single_test(executable, i)
                results.append(result)
                
                # Промежуточный прогресс каждые 5 итераций
                if (i + 1) % 5 == 0:
                    print(f"    ✅ Completed {i+1}/{iterations} iterations")
                    
            except Exception as e:
                print(f"    ❌ Iteration {i+1} failed: {e}")
                continue
                
        return results
    
    def calculate_statistics(self, data: List[float]) -> Dict:
        """Вычисляет статистические параметры"""
        if not data:
            return {}
            
        return {
            'count': len(data),
            'mean': statistics.mean(data),
            'median': statistics.median(data),
            'stdev': statistics.stdev(data) if len(data) > 1 else 0,
            'min': min(data),
            'max': max(data),
            'range': max(data) - min(data),
            'variance': statistics.variance(data) if len(data) > 1 else 0
        }
    
    def analyze_results(self, simd_results: List[Dict], scalar_results: List[Dict]) -> Dict:
        """Анализирует результаты и вычисляет статистическую значимость"""
        analysis = {}
        
        # Извлекаем времена для различных операций
        operation_types = ['total_test_time_s', 'avg_per_operation_s', 'operations_per_second', 'wallclock_time_s']
        
        for op_type in operation_types:
            simd_times = [r['times'].get(op_type, r.get('wallclock', 0)) for r in simd_results if op_type in r['times']]
            scalar_times = [r['times'].get(op_type, r.get('wallclock', 0)) for r in scalar_results if op_type in r['times']]
            
            if not simd_times or not scalar_times:
                continue
                
            simd_stats = self.calculate_statistics(simd_times)
            scalar_stats = self.calculate_statistics(scalar_times)
            
            # Вычисляем улучшение (для operations_per_second больше = лучше, для времени меньше = лучше)
            if scalar_stats.get('mean', 0) > 0:
                if 'operations_per_second' in op_type:
                    # Для throughput: SIMD/Scalar (больше = лучше)
                    improvement_ratio = simd_stats['mean'] / scalar_stats['mean']
                    improvement_percent = ((simd_stats['mean'] - scalar_stats['mean']) / scalar_stats['mean']) * 100
                else:
                    # Для времени: Scalar/SIMD (меньше = лучше)
                    improvement_ratio = scalar_stats['mean'] / simd_stats['mean']
                    improvement_percent = ((scalar_stats['mean'] - simd_stats['mean']) / scalar_stats['mean']) * 100
            else:
                improvement_ratio = 1.0
                improvement_percent = 0.0
                
            # 95% доверительный интервал (приблизительный)
            import math
            if simd_stats.get('stdev', 0) > 0 and simd_stats['count'] > 1:
                margin_error = 1.96 * (simd_stats['stdev'] / math.sqrt(simd_stats['count']))
                confidence_interval = (simd_stats['mean'] - margin_error, simd_stats['mean'] + margin_error)
            else:
                confidence_interval = (simd_stats.get('mean', 0), simd_stats.get('mean', 0))
            
            analysis[op_type] = {
                'simd_stats': simd_stats,
                'scalar_stats': scalar_stats,
                'improvement_ratio': improvement_ratio,
                'improvement_percent': improvement_percent,
                'simd_confidence_interval_95': confidence_interval,
                'significant': abs(improvement_percent) > 1.0  # Порог значимости 1%
            }
            
        return analysis
    
    def generate_report(self, analysis: Dict) -> str:
        """Генерирует читаемый отчет"""
        report = []
        report.append("\n" + "="*80)
        report.append("📊 СТАТИСТИЧЕСКИЙ АНАЛИЗ SIMD ОПТИМИЗАЦИИ CHIPMUNK")
        report.append("="*80)
        report.append(f"Timestamp: {self.results['timestamp']}")
        report.append("")
        
        for op_type, data in analysis.items():
            if not data:
                continue
                
            report.append(f"🔍 {op_type.upper().replace('_', ' ')}:")
            report.append(f"   SIMD:   {data['simd_stats']['mean']:.6f}s ± {data['simd_stats']['stdev']:.6f}s (n={data['simd_stats']['count']})")
            report.append(f"   Scalar: {data['scalar_stats']['mean']:.6f}s ± {data['scalar_stats']['stdev']:.6f}s (n={data['scalar_stats']['count']})")
            report.append(f"   Improvement: {data['improvement_percent']:+.2f}% ({data['improvement_ratio']:.3f}x)")
            report.append(f"   95% CI: [{data['simd_confidence_interval_95'][0]:.6f}, {data['simd_confidence_interval_95'][1]:.6f}]")
            report.append(f"   Statistically significant: {'✅ YES' if data['significant'] else '❌ NO'}")
            report.append("")
            
        return "\n".join(report)
    
    def save_results(self, filename: str = "chipmunk_statistical_validation.json"):
        """Сохраняет результаты в JSON файл"""
        with open(filename, 'w') as f:
            json.dump(self.results, f, indent=2)
        print(f"📄 Results saved to {filename}")

def main():
    print("🎯 CHIPMUNK SIMD STATISTICAL VALIDATION")
    print("=======================================")
    print("Day 2 Morning Session - Automated Statistical Analysis")
    print()
    
    validator = ChipmunkStatisticalValidator()
    
    # Запускаем тесты
    iterations = 25  # Начинаем с 25 итераций для быстрого прототипа
    
    try:
        # Тестируем SIMD версию
        simd_results = validator.run_test_series('test_chipmunk_simple', iterations)
        validator.results['tests']['simd'] = simd_results
        
        # Тестируем Scalar версию
        scalar_results = validator.run_test_series('test_chipmunk_scalar', iterations)
        validator.results['tests']['scalar'] = scalar_results
        
        # Анализируем результаты
        analysis = validator.analyze_results(simd_results, scalar_results)
        validator.results['analysis'] = analysis
        
        # Генерируем отчет
        report = validator.generate_report(analysis)
        print(report)
        
        # Сохраняем результаты
        validator.save_results()
        
        print("\n✅ Statistical validation completed successfully!")
        print("📊 Check chipmunk_statistical_validation.json for detailed results")
        
    except KeyboardInterrupt:
        print("\n⚠️  Validation interrupted by user")
        validator.save_results("chipmunk_partial_results.json")
    except Exception as e:
        print(f"\n❌ Validation failed: {e}")
        return 1
        
    return 0

if __name__ == "__main__":
    exit(main()) 