# Mathematical Interpretation of test_encode.c Using Coding Theory

## Overview

The unit tests in `test_encode.c` verify a bit-level encoding function `dap_encode_char_by_char` that implements a **base-2^b encoding scheme**, where `b = a_base_size`. From a coding theory perspective, this function implements a **linear encoding map** from a binary vector space to a representation space.

## Mathematical Framework

### Vector Space Structure

Let $\mathbb{F}_2 = \{0, 1\}$ denote the binary field (Boolean field). The input data is interpreted as a vector in the vector space:

$$\mathbf{x} \in \mathbb{F}_2^n$$

where $n = 8 \cdot \text{input\_size}$ is the total number of bits in the input.

### Encoding Map

The function implements an encoding map:

$$E: \mathbb{F}_2^n \rightarrow \Sigma^m$$

where:
- $\Sigma$ is the output alphabet (defined by the lookup table)
- $m = \lfloor n/b \rfloor$ is the output length
- $b = \text{a\_base\_size}$ is the number of bits extracted per output symbol

### Code Definition

The encoding process defines a **code** $C \subseteq \mathbb{F}_2^n$ where each codeword $\mathbf{c} \in C$ represents an input vector. However, since this is a **bijective encoding** (not error-correcting), the code $C = \mathbb{F}_2^n$ (the entire space).

The code can be viewed as a **linear code** with:
- **Dimension**: $k = n$ (all input vectors are valid)
- **Length**: $n$ (input length in bits)
- **Rate**: $R = 1$ (no redundancy added)

## Test-by-Test Mathematical Interpretation

### 1. `s_test_encode_null_inputs()` - Trivial Cases

**Mathematical Interpretation:**
- Tests the **domain** of the encoding function $E$
- Verifies that $E(\emptyset) = \emptyset$ (empty input maps to empty output)
- Ensures the function is **well-defined** on its domain

**Coding Theory Perspective:**
- Verifies that the encoding map respects the **zero vector**: $E(\mathbf{0}) = \emptyset$
- Tests **function domain constraints**: $E$ is only defined for non-null inputs

### 2. `s_test_encode_empty_input()` - Zero Vector

**Mathematical Interpretation:**
- Tests encoding of the **zero vector** $\mathbf{0} \in \mathbb{F}_2^0$
- Verifies: $E(\mathbf{0}) = \emptyset$ (empty string)

**Coding Theory Perspective:**
- The zero vector is always a codeword in any linear code
- Verifies the **additive identity property**: $E(\mathbf{0}) = \mathbf{0}$

### 3. `s_test_encode_base8()` - Identity-like Encoding

**Mathematical Interpretation:**
- Tests encoding with $b = 8$ (extracts 8 bits at a time)
- Input: $\mathbf{x} \in \mathbb{F}_2^{40}$ (5 bytes = 40 bits for "Hello")
- Output length: $m = \lfloor 40/8 \rfloor = 5$

**Coding Theory Perspective:**
- For $b = 8$, the encoding extracts **one byte** at a time
- The encoding map becomes: $E: \mathbb{F}_2^{8k} \rightarrow \Sigma^k$ where $k$ is the number of bytes
- This is essentially a **byte-to-symbol mapping** with no bit-level transformation

**Vector Space View:**
- Each 8-bit block $\mathbf{x}_i \in \mathbb{F}_2^8$ is mapped to a symbol via the lookup table
- The encoding is: $E(\mathbf{x}) = (T(x_1), T(x_2), \ldots, T(x_k))$ where $T$ is the table mapping

### 4. `s_test_encode_base5()` - Base-32-like Encoding

**Mathematical Interpretation:**
- Tests encoding with $b = 5$ (extracts 5 bits at a time)
- Input: $\mathbf{x} \in \mathbb{F}_2^{16}$ (2 bytes = 16 bits for "AB")
- Output length: $m = \lfloor 16/5 \rfloor = 3$

**Coding Theory Perspective:**
- The encoding extracts **5-bit blocks** from the input bitstream
- Each 5-bit block $\mathbf{b}_i \in \mathbb{F}_2^5$ represents a value in $\{0, 1, \ldots, 31\}$
- The encoding map: $E: \mathbb{F}_2^{16} \rightarrow \Sigma^3$

**Vector Space Decomposition:**
- The input space $\mathbb{F}_2^{16}$ is partitioned into 3 blocks (with the last block potentially incomplete)
- Each block spans across byte boundaries, requiring bit-level extraction

### 5. `s_test_encode_base6()` - Base-64-like Encoding

**Mathematical Interpretation:**
- Tests encoding with $b = 6$ (extracts 6 bits at a time)
- Input: $\mathbf{x} \in \mathbb{F}_2^{24}$ (3 bytes = 24 bits for "ABC")
- Output length: $m = \lfloor 24/6 \rfloor = 4$

**Coding Theory Perspective:**
- This is the standard **Base64 encoding scheme**
- Each 6-bit block $\mathbf{b}_i \in \mathbb{F}_2^6$ represents a value in $\{0, 1, \ldots, 63\}$
- The encoding map: $E: \mathbb{F}_2^{24} \rightarrow \Sigma^4$

**Mathematical Structure:**
- The input is divided into **4 blocks of 6 bits each**
- Perfect alignment: $24 = 4 \times 6$ (no padding needed)
- This is why Base64 is commonly used: 3 bytes map cleanly to 4 characters

### 6. `s_test_encode_different_sizes()` - Dimension Testing

**Mathematical Interpretation:**
- Tests the encoding function across different **input dimensions**
- Verifies the **dimension formula**: $m = \lfloor n/b \rfloor$

**Test Cases:**
1. $n = 8$ bits, $b = 5$: $m = \lfloor 8/5 \rfloor = 1$
2. $n = 32$ bits, $b = 5$: $m = \lfloor 32/5 \rfloor = 6$
3. $n = 64$ bits, $b = 5$: $m = \lfloor 64/5 \rfloor = 12$

**Coding Theory Perspective:**
- Verifies that the encoding map $E: \mathbb{F}_2^n \rightarrow \Sigma^m$ has the correct **codomain dimension**
- Tests the **linearity property**: the output dimension scales correctly with input dimension

### 7. `s_test_encode_output_size()` - Dimension Formula Verification

**Mathematical Interpretation:**
- Systematically tests the **dimension relationship**: $m = \lfloor n/b \rfloor$
- Tests multiple values of $b \in \{1, 2, 4, 8\}$

**Coding Theory Perspective:**
- For a linear code, the **rate** is $R = k/n$ where $k$ is the dimension
- Here, $k = n$ (no redundancy), so $R = 1$
- The output dimension $m$ represents the **representation length** in the new alphabet

**Mathematical Verification:**
- $b = 1$: $m = n$ (bit-by-bit encoding)
- $b = 2$: $m = n/2$ (2 bits per symbol)
- $b = 4$: $m = n/4$ (4 bits per symbol, like hexadecimal)
- $b = 8$: $m = n/8$ (byte-by-byte encoding)

### 8. `s_test_encode_custom_table()` - Encoding Function Verification

**Mathematical Interpretation:**
- Tests that the encoding uses the **lookup table** $T: \{0, 1, \ldots, 2^b-1\} \rightarrow \Sigma$
- Verifies the **encoding function composition**: $E(\mathbf{x}) = (T(b_1), T(b_2), \ldots, T(b_m))$

**Coding Theory Perspective:**
- The lookup table defines the **symbol mapping** for the code
- Different tables create different **representations** of the same underlying code
- The code structure (vector space) remains the same, only the **presentation** changes

### 9. `s_test_encode_base58()` - Base-58 Encoding

**Mathematical Interpretation:**
- Tests Base-58 encoding with $b = 6$ (uses 6-bit extraction, but only 58 symbols)
- Input: $\mathbf{x} \in \mathbb{F}_2^{40}$ (5 bytes = 40 bits)
- Output length: $m = \lfloor 40/6 \rfloor = 6$

**Coding Theory Perspective:**
- Base-58 is a **non-standard base** encoding (58 symbols instead of 64)
- The encoding extracts 6 bits (values 0-63) but maps only 58 of them to valid symbols
- This creates a **subset code** where some 6-bit patterns are invalid in the output

**Mathematical Structure:**
- The code $C \subseteq \mathbb{F}_2^n$ remains the same
- The encoding map $E: \mathbb{F}_2^n \rightarrow \Sigma_{58}^m$ uses a **58-symbol alphabet**
- The lookup table $T: \{0, 1, \ldots, 63\} \rightarrow \Sigma_{58} \cup \{\text{invalid}\}$ is a **partial function**

### 10. `s_test_encode_base64_standard()` and `s_test_encode_base64_url_safe()` - Base-64 Variants

**Mathematical Interpretation:**
- Both test Base-64 encoding with $b = 6$
- They differ only in the **lookup table** (symbol mapping)

**Coding Theory Perspective:**
- Both implement the **same code** $C = \mathbb{F}_2^n$
- They differ in the **presentation**: standard Base64 vs. URL-safe Base64
- The encoding maps are **equivalent** up to symbol renaming

**Mathematical Equivalence:**
- Standard: $E_{\text{std}}: \mathbb{F}_2^n \rightarrow \Sigma_{\text{std}}^m$
- URL-safe: $E_{\text{url}}: \mathbb{F}_2^n \rightarrow \Sigma_{\text{url}}^m$
- There exists a **bijection** $\phi: \Sigma_{\text{std}} \rightarrow \Sigma_{\text{url}}$ such that $E_{\text{url}} = \phi \circ E_{\text{std}}$

## General Mathematical Properties

### Linearity (Partial)

The encoding function is **not fully linear** in the algebraic sense because:
1. It operates on **bit-level extraction** across byte boundaries
2. The lookup table introduces **non-linear symbol mapping**

However, the **bit extraction** process is linear:
- The bit extraction: $\mathbf{b}_i = \text{extract}_b(\mathbf{x}, i)$ is a **linear operation**
- The table lookup: $T(\mathbf{b}_i)$ is a **non-linear symbol mapping**

### Vector Space Structure

The input space maintains the structure:
- **Addition**: Bitwise XOR in $\mathbb{F}_2^n$
- **Scalar multiplication**: Multiplication in $\mathbb{F}_2$ (trivial: $0 \cdot \mathbf{x} = \mathbf{0}$, $1 \cdot \mathbf{x} = \mathbf{x}$)

### Code Properties

- **Code dimension**: $k = n$ (all input vectors are valid codewords)
- **Code length**: $n$ (input length in bits)
- **Minimum distance**: Not applicable (this is not an error-correcting code)
- **Rate**: $R = 1$ (no redundancy)

### Encoding Efficiency

The encoding efficiency is determined by:
- **Input dimension**: $n$ bits
- **Output dimension**: $m = \lfloor n/b \rfloor$ symbols
- **Alphabet size**: $|\Sigma| = 2^b$ (for Base-64: 64 symbols)

The **expansion factor** is: $\frac{m}{n} = \frac{1}{b}$ symbols per bit

## Conclusion

The unit tests verify a **bit-level encoding scheme** that implements a mapping from binary vectors $\mathbb{F}_2^n$ to symbol strings in an alphabet $\Sigma$. While not a traditional error-correcting code, the encoding maintains the vector space structure of the input and provides a systematic way to represent binary data in different bases (Base-5, Base-6/Base-64, Base-58, etc.).

The tests verify:
1. **Domain and codomain** correctness
2. **Dimension relationships** between input and output
3. **Encoding function** correctness for various base sizes
4. **Symbol mapping** via lookup tables
5. **Edge cases** (empty input, null inputs)

From a coding theory perspective, this is a **bijective encoding** (no redundancy) that preserves all information while changing the representation format.
