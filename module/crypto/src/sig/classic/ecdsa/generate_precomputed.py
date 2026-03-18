#!/usr/bin/env python3
"""
Generate precomputed tables for secp256k1 ECDSA operations.

This script generates:
1. G multiplication table for ecmult (WINDOW_G points)
2. G*2^128 multiplication table for split-128 optimization
3. Comb table for ecmult_gen

Usage:
    python3 generate_precomputed.py [--window-g N] [--output FILE]
"""

import argparse
import sys
from dataclasses import dataclass
from typing import Tuple, Optional

# =============================================================================
# secp256k1 curve parameters
# =============================================================================

# Prime field: p = 2^256 - 2^32 - 977
P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F

# Curve order
N = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141

# Generator point G
GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
GY = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8

# Curve coefficient (y^2 = x^3 + 7)
B = 7

# =============================================================================
# Field arithmetic (mod P)
# =============================================================================

def mod_inv(a: int, p: int = P) -> int:
    """Modular inverse using extended Euclidean algorithm."""
    if a == 0:
        raise ValueError("Cannot invert zero")
    lm, hm = 1, 0
    low, high = a % p, p
    while low > 1:
        ratio = high // low
        nm, new = hm - lm * ratio, high - low * ratio
        lm, low, hm, high = nm, new, lm, low
    return lm % p

def mod_sqrt(a: int, p: int = P) -> Optional[int]:
    """Modular square root for p ≡ 3 (mod 4)."""
    # For secp256k1, p ≡ 3 (mod 4), so sqrt(a) = a^((p+1)/4)
    r = pow(a, (p + 1) // 4, p)
    if (r * r) % p == a % p:
        return r
    return None

# =============================================================================
# Point arithmetic on secp256k1
# =============================================================================

@dataclass
class Point:
    """Affine point on secp256k1 curve."""
    x: int
    y: int
    infinity: bool = False
    
    @classmethod
    def infinity_point(cls) -> 'Point':
        return cls(0, 0, infinity=True)
    
    def is_on_curve(self) -> bool:
        if self.infinity:
            return True
        left = (self.y * self.y) % P
        right = (self.x * self.x * self.x + B) % P
        return left == right
    
    def __eq__(self, other: 'Point') -> bool:
        if self.infinity and other.infinity:
            return True
        if self.infinity or other.infinity:
            return False
        return self.x == other.x and self.y == other.y
    
    def __neg__(self) -> 'Point':
        if self.infinity:
            return Point.infinity_point()
        return Point(self.x, (-self.y) % P)

def point_double(p: Point) -> Point:
    """Double a point: 2*P."""
    if p.infinity:
        return Point.infinity_point()
    if p.y == 0:
        return Point.infinity_point()
    
    # λ = (3*x^2) / (2*y)
    lam = (3 * p.x * p.x * mod_inv(2 * p.y)) % P
    
    # x3 = λ^2 - 2*x
    x3 = (lam * lam - 2 * p.x) % P
    
    # y3 = λ*(x - x3) - y
    y3 = (lam * (p.x - x3) - p.y) % P
    
    return Point(x3, y3)

def point_add(p1: Point, p2: Point) -> Point:
    """Add two points: P1 + P2."""
    if p1.infinity:
        return p2
    if p2.infinity:
        return p1
    
    if p1.x == p2.x:
        if p1.y == p2.y:
            return point_double(p1)
        else:
            return Point.infinity_point()
    
    # λ = (y2 - y1) / (x2 - x1)
    lam = ((p2.y - p1.y) * mod_inv(p2.x - p1.x)) % P
    
    # x3 = λ^2 - x1 - x2
    x3 = (lam * lam - p1.x - p2.x) % P
    
    # y3 = λ*(x1 - x3) - y1
    y3 = (lam * (p1.x - x3) - p1.y) % P
    
    return Point(x3, y3)

def point_mul(k: int, p: Point) -> Point:
    """Scalar multiplication: k * P using double-and-add."""
    if k == 0 or p.infinity:
        return Point.infinity_point()
    
    if k < 0:
        k = -k
        p = -p
    
    result = Point.infinity_point()
    addend = p
    
    while k > 0:
        if k & 1:
            result = point_add(result, addend)
        addend = point_double(addend)
        k >>= 1
    
    return result

# =============================================================================
# Table generation
# =============================================================================

def generate_wnaf_table(base: Point, table_size: int) -> list:
    """
    Generate wNAF table: table[i] = (2*i + 1) * base
    For window w, table_size = 2^(w-1)
    """
    table = []
    double_base = point_double(base)
    current = base
    
    for i in range(table_size):
        table.append(current)
        if i < table_size - 1:
            current = point_add(current, double_base)
    
    return table

def generate_comb_table(base: Point, teeth: int, table_size: int) -> list:
    """
    Generate comb table for ecmult_gen.
    comb_table[i][j] = (j+1) * 2^(bits*i) * G
    """
    bits = 256 // teeth
    table = []
    
    current_base = base
    for i in range(teeth):
        row = []
        accum = current_base
        for j in range(table_size):
            row.append(accum)
            if j < table_size - 1:
                accum = point_add(accum, current_base)
        table.append(row)
        
        # current_base = current_base * 2^bits
        for _ in range(bits):
            current_base = point_double(current_base)
    
    return table

def format_field_element(val: int, name_prefix: str = "") -> str:
    """Format a 256-bit field element as 5x52-bit limbs."""
    # 5x52-bit representation (like bitcoin-core)
    mask52 = (1 << 52) - 1
    limbs = []
    v = val
    for _ in range(5):
        limbs.append(v & mask52)
        v >>= 52
    
    return f"{{ 0x{limbs[0]:013x}ULL, 0x{limbs[1]:013x}ULL, 0x{limbs[2]:013x}ULL, 0x{limbs[3]:013x}ULL, 0x{limbs[4]:013x}ULL }}"

def format_ge_storage(point: Point) -> str:
    """Format affine point as ecdsa_ge_storage_t (compact 64-byte storage)."""
    if point.infinity:
        return "{ { 0 }, { 0 } }"
    
    # 4x64-bit representation for storage
    def to_4x64(val):
        limbs = []
        v = val
        for _ in range(4):
            limbs.append(v & ((1 << 64) - 1))
            v >>= 64
        return limbs
    
    x_limbs = to_4x64(point.x)
    y_limbs = to_4x64(point.y)
    
    x_str = ", ".join(f"0x{l:016x}ULL" for l in x_limbs)
    y_str = ", ".join(f"0x{l:016x}ULL" for l in y_limbs)
    
    return f"{{ {{ {x_str} }}, {{ {y_str} }} }}"

# =============================================================================
# Code generation
# =============================================================================

def generate_header(window_g: int) -> str:
    """Generate header file."""
    table_size = 1 << (window_g - 2)
    
    return f'''/*
 * Precomputed tables for secp256k1 ECDSA operations
 * 
 * AUTO-GENERATED FILE - DO NOT EDIT
 * Generated by: generate_precomputed.py
 * 
 * Configuration:
 *   WINDOW_G = {window_g}
 *   Table size = {table_size} points per table
 *   Memory usage = ~{table_size * 64 * 2 // 1024} KB (two tables)
 */

#ifndef ECDSA_PRECOMPUTED_ECMULT_H
#define ECDSA_PRECOMPUTED_ECMULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

// Window size for G multiplication (larger = faster, more memory)
#define ECDSA_WINDOW_G {window_g}

// Number of points in each table: 2^(WINDOW_G - 2)
#define ECDSA_ECMULT_TABLE_SIZE (1 << (ECDSA_WINDOW_G - 2))

// Compact storage format for affine points (64 bytes each)
typedef struct {{
    uint64_t x[4];
    uint64_t y[4];
}} ecdsa_ge_storage_t;

// Precomputed table for G: pre_g[i] = (2*i + 1) * G
extern const ecdsa_ge_storage_t ecdsa_pre_g[ECDSA_ECMULT_TABLE_SIZE];

// Precomputed table for G*2^128: pre_g_128[i] = (2*i + 1) * (2^128 * G)
extern const ecdsa_ge_storage_t ecdsa_pre_g_128[ECDSA_ECMULT_TABLE_SIZE];

#ifdef __cplusplus
}}
#endif

#endif // ECDSA_PRECOMPUTED_ECMULT_H
'''

def generate_source(window_g: int) -> str:
    """Generate source file with precomputed tables."""
    table_size = 1 << (window_g - 2)
    
    print(f"Generating tables with WINDOW_G={window_g}, table_size={table_size}...", file=sys.stderr)
    
    # Generator point
    G = Point(GX, GY)
    assert G.is_on_curve(), "Generator not on curve!"
    
    # G * 2^128
    G_128 = point_mul(1 << 128, G)
    assert G_128.is_on_curve(), "G*2^128 not on curve!"
    
    print("Generating pre_g table...", file=sys.stderr)
    pre_g = generate_wnaf_table(G, table_size)
    
    print("Generating pre_g_128 table...", file=sys.stderr)
    pre_g_128 = generate_wnaf_table(G_128, table_size)
    
    # Verify all points
    for i, p in enumerate(pre_g):
        assert p.is_on_curve(), f"pre_g[{i}] not on curve!"
    for i, p in enumerate(pre_g_128):
        assert p.is_on_curve(), f"pre_g_128[{i}] not on curve!"
    
    print("Formatting output...", file=sys.stderr)
    
    lines = [f'''/*
 * Precomputed tables for secp256k1 ECDSA operations
 * 
 * AUTO-GENERATED FILE - DO NOT EDIT
 * Generated by: generate_precomputed.py
 * 
 * Configuration:
 *   WINDOW_G = {window_g}
 *   Table size = {table_size} points
 */

#include "ecdsa_precomputed_ecmult.h"

// =============================================================================
// pre_g: Odd multiples of G
// pre_g[i] = (2*i + 1) * G for i in [0, {table_size-1}]
// =============================================================================

const ecdsa_ge_storage_t ecdsa_pre_g[ECDSA_ECMULT_TABLE_SIZE] = {{''']
    
    for i, p in enumerate(pre_g):
        comment = f"    /* {2*i+1}*G */"
        lines.append(f"    {format_ge_storage(p)},  {comment}")
    
    lines.append('''};

// =============================================================================
// pre_g_128: Odd multiples of G*2^128
// pre_g_128[i] = (2*i + 1) * (2^128 * G) for i in [0, ''' + str(table_size-1) + ''']
// =============================================================================

const ecdsa_ge_storage_t ecdsa_pre_g_128[ECDSA_ECMULT_TABLE_SIZE] = {''')
    
    for i, p in enumerate(pre_g_128):
        comment = f"    /* {2*i+1}*G*2^128 */"
        lines.append(f"    {format_ge_storage(p)},  {comment}")
    
    lines.append('};')
    
    return '\n'.join(lines)

# =============================================================================
# Main
# =============================================================================

def main():
    parser = argparse.ArgumentParser(description='Generate precomputed secp256k1 tables')
    parser.add_argument('--window-g', type=int, default=15,
                        help='Window size for G tables (default: 15, bitcoin-core default)')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Output file prefix (generates .h and .c)')
    parser.add_argument('--header-only', action='store_true',
                        help='Generate only header file')
    parser.add_argument('--source-only', action='store_true',
                        help='Generate only source file')
    
    args = parser.parse_args()
    
    if args.window_g < 2 or args.window_g > 24:
        print("Error: window-g must be in range [2, 24]", file=sys.stderr)
        sys.exit(1)
    
    if args.output:
        if not args.source_only:
            header_file = args.output + '.h' if not args.output.endswith('.h') else args.output
            with open(header_file, 'w') as f:
                f.write(generate_header(args.window_g))
            print(f"Generated: {header_file}", file=sys.stderr)
        
        if not args.header_only:
            source_file = args.output + '.c' if not args.output.endswith('.c') else args.output
            if args.output.endswith('.h'):
                source_file = args.output[:-2] + '.c'
            with open(source_file, 'w') as f:
                f.write(generate_source(args.window_g))
            print(f"Generated: {source_file}", file=sys.stderr)
    else:
        # Output to stdout
        if not args.source_only:
            print(generate_header(args.window_g))
        if not args.header_only:
            print(generate_source(args.window_g))

if __name__ == '__main__':
    main()
