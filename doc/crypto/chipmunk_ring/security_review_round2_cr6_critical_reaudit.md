# ChipmunkRing — повторный критический аудит (Round-2 / CR-6)

> **Статус**: DRAFT / Для обсуждения
> **Дата**: 2026-04-19
> **Автор**: Опус (критический ре-аудит)
> **Предпосылка**: Первый раунд аудита (CR-0..CR-5) закрыл все риски R-01..R-08 как PASSED. Настоящий документ — независимая перепроверка, выполненная по прямому указанию: «критически отнестись к уже проаудированному, код изучить — он от статьи отличается, и она сама с ошибками».

---

## 0. Краткое резюме (TL;DR)

Повторный анализ кода и сопоставление его с [научной статьёй](papers/own/chipmunk_ring_scientific_paper.tex), [рецензией](papers/recense_01.txt) и планами CR-0..CR-5 показали, что **первый раунд аудита пропустил класс критических cryptographic-level ошибок**, затрагивающих сами основы схемы.

Первый аудит фокусировался на:

- memory safety (buffer overflow/OOB, use-after-free);
- error-path robustness (NULL, оверсайз/андерсайз, roundtrip сериализации);
- наличие тестов и отсутствие очевидных утечек энтропии;
- соблюдение DAP SDK API-контрактов.

Он НЕ касался:

- собственно **корректности криптографических свойств** (EUF-CMA, unlinkability, anonymity, soundness);
- **соответствия кода научной статье** (паразитные противоречия между статьёй, заголовками и реализацией);
- семантической корректности работы с ключами и подписями.

Ниже перечислены **23 новые находки** (CR-C1..CR-C23) с явным указанием местоположения в коде. Среди них 6 `CRITICAL`, 10 `HIGH`, 6 `MEDIUM`, 1 `LOW`.

**Главный вывод**: в нынешнем виде схема в `module/crypto/src/sig/chipmunk/chipmunk_ring.*` **небезопасна** и непригодна для продакшена. Требуется фундаментальная переработка, а не косметические правки. Подробности — дальше.

---

## 1. Критика первого раунда аудита

### 1.1. Что именно было проверено в CR-0..CR-5

| Фаза | Фокус | Основной артефакт |
|------|-------|-------------------|
| CR-0 | Scope, threat model, risk matrix | `security_review_cr0_preparation_pack.md` |
| CR-1 | Memory safety, classic vulns | `security_review_cr1_classic_vuln_plan.md` |
| CR-2 | Anonymity / leakage (высокоуровнево) | `security_review_cr2_anonymity_leakage_plan.md` |
| CR-3 | Quantum resilience, параметры | `security_review_cr3_quantum_resilience_plan.md` |
| CR-4 | Implementation correctness, serialization | `security_review_cr4_implementation_correctness_plan.md` |
| CR-5 | Final validation, closure | `security_review_cr5_final_validation_and_closure_plan.md` |
| итог | R-01..R-06, R-08 закрыты; R-07 deferred; R-09 open | `security_review_findings_and_closure_summary.md` |

### 1.2. Что было пропущено (по категориям)

1. **EUF-CMA (Existential Unforgeability)**. Никакого теста «возьми валидную подпись, замени один байт, должно завалиться» нет. Нет теста «forge without any private key». Никакая фаза не проверяла, действительно ли `verify` требует знания секрета.
2. **Anonymity as indistinguishability**. CR-2 констатировал, что `signer_index` не сериализуется, и на этом успокоился. Реальный тест — «наблюдатель не может выделить подписанта по публичным данным» — не проводился.
3. **Соответствие статье (doc ↔ code)**. Ни один CR-план этого не требует. В итоге заголовки/код/статья расходятся, и эти расхождения скрывают баги.
4. **Dead code / unreachable crypto**. Модуль `chipmunk_ring_secret_sharing.c` не вызывается нигде в активном пути `sign/verify`, но присутствует в бинарнике и документируется как часть схемы.
5. **Семантика `sizeof` при работе с указателями**. Classic C-ловушка, но именно на криптопримитиве она даёт катастрофу (см. CR-C3).
6. **Review статьи**. Рецензия `recense_01.txt` указывает: NIST Level заявлен ошибочно (112 бит ≠ Level 5), Acorn описан без строгого определения, Ring-LWE связан с аргументом через handwaving. В аудит это не попало.

### 1.3. Вердикт по CR-0..CR-5

Первый раунд аудита — **корректный, но узкий**. Его выводы («R-01..R-06, R-08 closed») справедливы в рамках чисто имплементационной безопасности (memory, error paths, API contract). **Для криптографической безопасности схемы они недостаточны** и создают ложное ощущение зрелости.

---

## 2. Карта расхождений «статья ↔ код»

| Аспект | Статья (`chipmunk_ring_scientific_paper.tex`) | Код |
|--------|------------------------------------------------|-----|
| XOF | SHAKE256 | SHA3-256 + самодельный HKDF-like expander (`s_domain_hash` в `chipmunk_ring.c` и `chipmunk_ring_acorn.c`) |
| Acorn | заявлен как ZKP / замена Fiat-Shamir | обычный итерированный хэш от публичных данных, без знания секрета |
| Ring-LWE security | «builds on Ring-LWE hardness» | в сигнатурной части Ring-LWE фигурирует только в core `chipmunk_sign`; в single-signer режиме эта часть **не верифицируется** |
| NIST Level | 112-bit = Level 5 | 112-bit не дотягивает даже до Level 1 (≥128 бит) |
| Threshold (Shamir) | Lagrange interpolation | реализована в `chipmunk_ring_secret_sharing.c` детерминированно (псевдослучайные коэффициенты из `snprintf`) и **не вызывается** из активного пути |
| Параметры (γ) | γ=6 | `chipmunk.h`: `CHIPMUNK_GAMMA = 6`; `dap_enc_chipmunk_ring_params.h`: `CHIPMUNK_RING_CHIPMUNK_GAMMA_DEFAULT = 4` — **внутреннее противоречие** |
| Linkability tag | функция от `(ring_hash, message, challenge)` | в `chipmunk_ring_acorn_create` считается как `SHA3(pk_i)` — детерминированная функция ключа |
| Quantum layers (Ring-LWE / NTRU / Code-based) | не упоминаются как отдельные | объявлены как `REMOVED` в `.c`, но константы до сих пор в `.h` |

---

## 3. Новые находки (CR-C1..CR-C23)

Нумерация `CR-C*` выбрана, чтобы не конфликтовать с R-01..R-09 из первого раунда.

### CR-C1. `CRITICAL` — **Universal forgery в single-signer режиме**

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c`, `chipmunk_ring_verify`, L.1070–1151.

В ветке `required_signers == 1` (классический ring signature) проверяется **только** пересчёт acorn-пруфов:

```1079:1140:module/crypto/src/sig/chipmunk/chipmunk_ring.c
for (uint32_t l_i = 0; l_i < l_ring_to_use->size; l_i++) {
    if (a_signature->acorn_proofs[l_i].acorn_proof && ...) {
        // ...
        int l_acorn_result = s_domain_hash(CHIPMUNK_RING_DOMAIN_ACORN_COMMITMENT,
                                           NULL, 0,
                                           l_acorn_input, l_acorn_input_size,
                                           l_expected_acorn_proof, a_signature->acorn_proofs[l_i].acorn_proof_size,
                                           a_signature->zk_iterations);
        // ...
        if (memcmp(a_signature->acorn_proofs[l_i].acorn_proof, l_expected_acorn_proof, ...) == 0) {
            valid_acorn_proofs++;
        }
    }
}
```

`acorn_proof[i] = H^I(pk_i ‖ message ‖ randomness_i)` — все входы публичны. Core Chipmunk-подпись `a_signature->signature` **никогда не верифицируется** в этой ветке.

Следствие — тривиальная подделка без знания любого секретного ключа:

1. Атакующий берёт публичный ring.
2. Выбирает любой `pk_i`, генерирует произвольный `randomness_i`.
3. Считает `acorn_proofs[i] = H^I(pk_i ‖ M ‖ randomness_i)`.
4. Остальные `acorn_proofs[j]` заполняет мусором (достаточно одного валидного).
5. `challenge = hash_fast(serialize(M, ring_hash, acorn_proofs))`.
6. `signature` = произвольные байты.
7. `verify → PASS`.

Эта находка одна обесценивает всю single-signer часть. **Схема в этом режиме не является подписью.**

---

### CR-C2. `CRITICAL` — **Анонимность сломана: подписант тривиально идентифицируется**

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c`, `chipmunk_ring_sign`, L.765–787 и `chipmunk_ring_verify`, L.1325–1337.

Подпись содержит:

- `a_signature->signature` = `chipmunk_sign(sk_π, challenge)` — **обычная Chipmunk-подпись**, валидная ровно под одним `pk_π`;
- полный ring публичных ключей (либо embedded, либо по ring_hash + внешний контекст);
- `challenge`.

Любой наблюдатель может за `O(|ring|)` вызовов `chipmunk_verify(pk_i, challenge, signature)` найти индекс `π`:

```1325:1337:module/crypto/src/sig/chipmunk/chipmunk_ring.c
for (uint32_t l_i = 0; l_i < l_ring_to_use->size && l_aggregation_valid; l_i++) {
    int l_partial_result = chipmunk_verify(l_ring_to_use->public_keys[l_i].data,
                                          a_signature->challenge, sizeof(a_signature->challenge),
                                          a_signature->signature);

    if (l_partial_result == CHIPMUNK_ERROR_SUCCESS) {
        // ↑ найденный индекс — это и есть подписант
        break;
    }
}
```

Этот же приём сам код использует и на стадии `sign` (L.765–787), чтобы найти реального подписанта. Анонимность, на которой построена вся схема, отсутствует.

Довод CR-2 «`signer_index` не сериализуется → анонимность сохраняется» — **теоретически наивен**: зная `signature` и множество `pk_i`, индекс вычисляется публично.

---

### CR-C3. `CRITICAL` — `sizeof(a_signature->challenge)` вместо `challenge_size`

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c`, L.1328.

`challenge` в структуре — это `uint8_t *` (динамический буфер), см. `chipmunk_ring.h:94`. Фактический размер challenge-хэша — `CHIPMUNK_RING_CHALLENGE_SIZE = 32` байта.

Но в multi-signer ветке передаётся `sizeof(указатель)`:

```1327:1329:module/crypto/src/sig/chipmunk/chipmunk_ring.c
int l_partial_result = chipmunk_verify(l_ring_to_use->public_keys[l_i].data,
                                      a_signature->challenge, sizeof(a_signature->challenge),
                                      a_signature->signature);
```

На 64-bit системах это `8` байт. Core `chipmunk_verify` считает подпись «по первым 8 байтам challenge» — и это же значение считается на стороне подписи (строка 773: `chipmunk_sign(..., a_signature->challenge_size, ...)`), то есть стороны **расходятся**: sign идёт по 32 байтам, verify — по 8. Следствие зависит от внутренней логики `chipmunk_verify`, но в любом случае это либо массовый false-negative, либо недоверификация.

Ещё один тот же баг встречается в тестах и в других местах — ищется легко: `rg 'sizeof\((\w+->)?challenge[^_]'` по `module/crypto`.

---

### CR-C4. `HIGH` — `l_aggregation_valid` никогда не становится `false`

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c`, L.1321–1340.

```1321:1340:module/crypto/src/sig/chipmunk/chipmunk_ring.c
bool l_aggregation_valid = true;

for (uint32_t l_i = 0; l_i < l_ring_to_use->size && l_aggregation_valid; l_i++) {
    int l_partial_result = chipmunk_verify(...);

    if (l_partial_result == CHIPMUNK_ERROR_SUCCESS) {
        // ...
        break;
    }
    // NB: в случае неудачи ничего не происходит
}

// ...
l_signature_verified = (l_valid_zk_proofs >= a_signature->required_signers && l_aggregation_valid);
```

Переменная инициализируется `true` и ни в одной ветке не присваивается `false`. Результат: шаг 5 «Verify aggregated signature components» для multi-signer — **пустышка**. `l_signature_verified` в multi-signer зависит только от ZK-пруфов, которые, в свою очередь (CR-C15), тоже не зависят от секрета.

---

### CR-C5. `HIGH` — `linkability_tag` в acorn — детерминированная функция `pk`

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring_acorn.c`, L.266–268.

```266:268:module/crypto/src/sig/chipmunk/chipmunk_ring_acorn.c
// Generate linkability tag using SHA3-256 hash of public key for anti-replay
dap_chipmunk_hash_sha3_256(a_commitment->linkability_tag, a_public_key->data,
                            sizeof(a_public_key->data));
```

Это даёт `tag_i = SHA3(pk_i)`. Любые две подписи одним участником тривиально линкуются (anti-anonymity даже без CR-C2), а любая подпись линкуется с самим `pk_i` через публичный хэш.

Статья определяет linkability-tag как функцию от `(ring_hash, message, challenge)` — это совсем другое. В `chipmunk_ring.c:829–840` есть *правильный* вариант, но он вычисляется отдельно и не совпадает с тем, что лежит в `acorn_proofs[i].linkability_tag`.

---

### CR-C6. `HIGH` — pointer-address в seed для acorn-рандомности

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring_acorn.c`, `chipmunk_ring_acorn_create`, ~L.199.

```c
char seed[128];
snprintf(seed, sizeof(seed), "acorn_%p_%zu_", (void*)a_public_key, a_message_size);
```

Использование **адреса указателя** (`%p`) в качестве входа криптографической рандомизации — прямой антипаттерн:

- зависит от ASLR и heap-layout,
- на одних и тех же данных на разных процессах даёт разные результаты (ломает детерминизм, где он нужен),
- на одной системе разные вызовы подряд могут возвращать одну и ту же память → коллизии seed,
- не добавляет энтропии по сравнению с `dap_random_bytes`.

Единственная реальная энтропия в функции — это последующий `dap_random_bytes` (если он там есть), но `%p` должен быть удалён.

---

### CR-C7. `HIGH` — Shamir Secret Sharing: dead code + детерминированные коэффициенты

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring_secret_sharing.c`.

1. Модуль **не вызывается** из `chipmunk_ring_sign` / `chipmunk_ring_verify` / `dap_enc_chipmunk_ring.c` / `dap_sign.c`. Тесты `test_chipmunk_ring_multi_signer.c` и пр. проверяют `chipmunk_ring_sign(..., required_signers > 1)`, а не secret-sharing API. Поэтому multi-signer threshold из статьи в активном пути не реализован.
2. «Случайные» коэффициенты для Lagrange-интерполяции получаются детерминированно:

   ```c
   snprintf(buf, sizeof(buf), "coeff_v0_%d_%u", coeff_idx, j);
   dap_hash_fast(buf, strlen(buf), &h);
   // использовать h как coefficient
   ```

   Два одинаковых `(coeff_idx, j)` дают один коэффициент → одна и та же Lagrange-полиномиальная кривая для всех секретов → **два любых share восстанавливают master secret** (или приводят к вычислимой утечке). Это полное разрушение privacy threshold-схемы.

Рекомендация (обновлено 2026-04-20): threshold-фича обязательна (подтверждено владельцем продукта для governance/multisig/social-recovery). Переписать модуль с cryptographically secure RNG (`dap_random_bytes` через rejection sampling mod q), исправить offset извлечения pk (Round-3 CR-D24), добавить PoP против rogue-key и подключить к активному пути с полным набором тестов (correctness, subset-invariance, zero-leakage, rogue-key). Вариант «удалить модуль» отвергнут.

---

### CR-C8. `HIGH` — расхождение параметров γ внутри самого кода

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk.h` vs `module/crypto/include/dap_enc_chipmunk_ring_params.h`.

- `chipmunk.h`: `CHIPMUNK_GAMMA = 6`, `CHIPMUNK_SIGNATURE_SIZE = CHIPMUNK_N * 4 * CHIPMUNK_GAMMA` → 12 288 байт.
- `dap_enc_chipmunk_ring_params.h`: `CHIPMUNK_RING_CHIPMUNK_GAMMA_DEFAULT = 4`, та же формула → 8 192 байт.

В `chipmunk_ring_sign` строка 552 `a_signature->signature_size = CHIPMUNK_SIGNATURE_SIZE` (12 288), но параметрическая модель в `s_pq_params` построена на γ=4 (8 192). Любой путь, который исходит из *параметрического* размера (сериализация, проверки границ), может разойтись с фактическим `signature_size`. Это либо скрытый source of bugs, либо «работает чудом», потому что второй путь не влияет на результат.

Рекомендация: один `source of truth` для γ, `N`, `q`; CI-проверка, что все заголовки выдают одно значение.

---

### CR-C9. `HIGH` — XOF: статья SHAKE256, код SHA3-256 + самодельный expander

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c:s_domain_hash` и `chipmunk_ring_acorn.c:s_domain_hash` (дубль).

```c
for (uint32_t iter = 0; iter < iterations; iter++) {
    // feedback + counter
    dap_chipmunk_hash_sha3_256(state, buf, buf_len);
    // copy state → output block
}
```

Это ручной counter-mode на SHA3-256. Формально XOF, но:

- не соответствует статье (SHAKE256), значит результат аудита статьи неприменим к коду;
- дублируется в двух файлах (`chipmunk_ring.c`, `chipmunk_ring_acorn.c`) — риск drift;
- нет анализа на indifferentiability / collision resistance на длинных выходах;
- domain-separation строка передаётся через `salt`, но сам формат confusion-prone (см. ниже, возможна namespace collision).

Рекомендация: заменить на стандартный SHAKE256 из libsodium/oqs/etc., или документировать и обосновать текущую схему с формальным анализом.

---

### CR-C10. `HIGH` — ring_hash в `dap_enc_chipmunk_ring_sign` и `chipmunk_ring_verify` считается разными хэшами

**Местоположение**: `module/crypto/src/dap_enc_chipmunk_ring.c` ~L.328 (sign path) vs `chipmunk_ring.c` embedded-mode verify ~L.920–945.

- В sign-пути для построения ring_hash используется `dap_chipmunk_hash_sha3_256(...)`.
- В verify-пути для embedded keys — `dap_hash_fast(combined_keys, combined_size, &ring_hash)` (generic hash-fast, обычно BLAKE/SHA2-based в конфигурации).

Разные хэши → два ring_hash. Если это сглажено совпадением размеров и код «везде берёт одно» — то только по случайности. Любое изменение конфигурации `dap_hash_fast` даст расхождение и поломку verify. Плюс: это признак того, что `ring_hash` вообще не имеет чёткой спецификации.

---

### CR-C11. `HIGH` — embedded ring_hash не сверяется с `a_signature->ring_hash`

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c`, embedded verify L.920–945 + L.1015.

В embedded-mode код:

1. Считает `ring_hash_computed = hash(embedded_keys)`.
2. Кладёт его в `l_effective_ring.ring_hash` и дальше использует в recompute challenge.
3. **Не сверяет** `ring_hash_computed` с `a_signature->ring_hash`.

Атака: берём валидную подпись под ring `R`, подменяем `embedded_keys` на произвольный `R'`, синхронно пересчитываем `acorn_proofs[i] = H^I(pk'_i ‖ M ‖ randomness_i)` (все входы публичны — см. CR-C1) и challenge. Подпись верифицируется под подмененным ring.

Для любого приложения, опирающегося на «подпись `s` может быть выпущена только членом `R`», это полное нарушение semantics.

Рекомендация: `memcmp(ring_hash_computed, a_signature->ring_hash, ring_hash_size) != 0 ⇒ return -1`.

---

### CR-C12. `HIGH` — NIST Level заявлен неверно

**Местоположение**: статья `chipmunk_ring_scientific_paper.tex`, раздел «Security Analysis»; рецензия `recense_01.txt`.

Статья утверждает «112-bit security, NIST Level 5» (местами «149-bit»). По NIST PQC Call-for-Proposals:

- Level 1 ≈ AES-128 (≥ 143-бит квантовой безопасности Grover-circuit, классически ~128 бит);
- Level 5 ≈ AES-256.

112 бит — это **ниже Level 1**, не Level 5. Для корректного Level 5 нужно ≥ 256 бит классически. Это прямой блокер для публикации: рецензент это заметил (`recense_01.txt`).

---

### CR-C13. `MEDIUM` — multi-signer ZK proofs не зависят от секрета

**Местоположение**: `chipmunk_ring.c:680–756` (sign), `chipmunk_ring.c:1186–1310` (verify).

Multi-signer «ZK-пруф» для участника `i`:

```
proof_i = s_domain_hash(MULTI_SIGNER_DOMAIN,
                        salt = serialize(challenge, required_signers, ring_size),
                        input = serialize(randomness_i, message, participant_context=i),
                        iterations)
```

Все входы — публичны. Никакой `sk_i` не используется. Проверка тривиально выполнима любым внешним наблюдателем, то есть это **не ZK-пруф знания секрета** (нет knowledge extractor). Это просто «подпись-сувенир». В сочетании с CR-C4 это означает, что multi-signer режим, как и single-signer (CR-C1), **не является подписью** в криптографическом смысле.

---

### CR-C14. `MEDIUM` — анонимность-тесты не проверяют фактическую неразличимость

**Местоположение**: `tests/unit/crypto/chipmunk_ring/test_chipmunk_ring_anonymity.c`.

Тест строит N подписей разными подписантами и проверяет:

- что все подписи одинакового размера,
- что `verify` возвращает OK,
- что «`signer_index` не сериализуется».

Тест, который реально нужен: «возьми `signature`, `message`, `ring`; вызови `chipmunk_verify(pk_i, challenge, signature)` для каждого `i`; ожидается, что успех либо у всех, либо ни у кого» (или другой формальный параметр indistinguishability). Такого теста нет, и как видно в CR-C2, его нельзя пройти при текущей реализации.

Итог: anonymity-тесты в CR-2 были «security theater».

---

### CR-C15. `HIGH` — отсутствуют forgery/mutation-тесты

**Местоположение**: `tests/unit/crypto/chipmunk_ring/*.c`.

`grep` по `forge|tamper|modify|malic|unauthorized|without.*key` по тестам даёт 1 (нерелевантное) совпадение. Это означает, что:

- нет теста «измени 1 байт `signature` → verify должен вернуть ошибку»;
- нет теста «подделай acorn_proofs[i] = `0x00...`, verify должен вернуть ошибку»;
- нет теста «подпиши ключом вне ring, но с тем же ring-hash → verify должен вернуть ошибку»;
- нет теста «генерация подписи без знания sk_i (CR-C1)».

Минимальный тестовый caveat EUF-CMA не выполнен.

---

### CR-C16. `MEDIUM` — многозначность `CHIPMUNK_RING_MAX_RING_SIZE`

**Местоположение**: `dap_enc_chipmunk_ring_params.h`.

`CHIPMUNK_RING_MAX_RING_SIZE = 1024`, при этом статья утверждает «practical for 2..64». Разрыв порядка величин даёт:

- DoS-вектор: ring размером 1024 даёт ~1024 acorn-пруфов * `zk_iterations` итераций хэша (до 1000 и более). Один verify может стоить сотни миллисекунд на CPU.
- Отсутствие проверок против «rate-limit» по размеру ring в API `dap_sign_verify_ring`.

Рекомендация: опустить `MAX_RING_SIZE` до документированного 64 или ввести явный «budget» для proof-of-work-like операций.

---

### CR-C17. `MEDIUM` — мёртвые quantum-layer константы

**Местоположение**: `dap_enc_chipmunk_ring_params.h` (много констант `CHIPMUNK_RING_RING_LWE_*`, `CHIPMUNK_RING_NTRU_*`, `CHIPMUNK_RING_CODE_*`).

В `chipmunk_ring.c` и документации значится «quantum layers REMOVED, Acorn handles all». Несмотря на это, константы остаются доступными и могут быть случайно импортированы/использованы, что даёт путаницу (см. [quantum_threat_analysis.md](quantum_threat_analysis.md)). Это также раздувает оценку quantum-security, которую делал CR-3.

Рекомендация: удалить устаревшие константы, убрать упоминания из документации.

---

### CR-C18. `MEDIUM` — дубликат `s_domain_hash`

**Местоположение**: `chipmunk_ring.c` и `chipmunk_ring_acorn.c`.

Функция объявлена `static` в двух файлах с идентичной реализацией. Риски:

- drift (одно место поправят, другое забудут — и получим два разных «xof»);
- затруднено ревью и формальный анализ.

Рекомендация: вынести в один модуль (`chipmunk_ring_xof.c`), сделать публичной в рамках подсистемы.

---

### CR-C19. `HIGH` — memory-leak в failure-path `chipmunk_ring_verify`

**Местоположение**: `module/crypto/src/sig/chipmunk/chipmunk_ring.c`, L.1095–1113 и аналогичные ветки.

`if (!l_acorn_input) return CHIPMUNK_RING_ERROR_MEMORY_ALLOC;` — возврат без освобождения `l_effective_ring.ring_hash` (если `use_embedded_keys == true`, см. L.942–945). То же для `!l_expected_acorn_proof`. Первый раунд аудита (R-05) закрыл OOB, но memleak-путь в error-branch остался.

Рекомендация: единый `goto cleanup;` в конце функции и централизованное освобождение.

---

### CR-C20. `HIGH` — linkability_tag в `chipmunk_ring.c` vs в `chipmunk_ring_acorn.c` дают разные значения

**Местоположение**: `chipmunk_ring.c` L.829–840 (linkability = H(ring_hash ‖ message ‖ challenge)) vs `chipmunk_ring_acorn.c` L.266–268 (linkability = H(pk_i)).

В структуре `chipmunk_ring_signature_t` есть оба поля: верхнеуровневый `linkability_tag` и `linkability_tag` внутри каждого `acorn_proofs[i]`. Какой из них реально используется `dap_sign_verify_ring` для anti-replay — неочевидно. Проверки на равенство между sign- и verify-путь нет. Это либо избыточное поле (dead data), либо семантический баг.

---

### CR-C21. `MEDIUM` — non-constant-time memcmp на криптоданных

**Местоположение**: `chipmunk_ring_verify`, `chipmunk_ring_sign` — все сравнения типа `memcmp(a_signature->acorn_proofs[l_i].acorn_proof, l_expected_acorn_proof, ...)`.

Для ring signature это формально не даёт side-channel на секрет (входы публичны — см. CR-C1), но:

- в multi-signer верификации partial-results леакают индекс первого «совпадающего» участника по тайминг-каналу;
- для core `chipmunk_verify` (Ring-LWE arithmetic) неизвестно, constant-time ли оно — не проверялось в CR-1.

Рекомендация: зафиксировать `constant_time_cmp`-политику для всех криптосравнений, добавить в CI проверку через `dudect` или аналог (R-07 → расширить).

---

### CR-C22. `LOW` — R-09 reproducibility до сих пор open

**Местоположение**: `security_review_findings_and_closure_summary.md` — R-09 помечен как open.

Без reproducible builds и публикации байтовых test-vectors любой внешний аудитор не сможет воспроизвести результаты CR-3 (quantum estimates) и CR-5 (unit tests). Для схемы, претендующей на «post-quantum», это блокирующий organizational issue. В Round-2 риск остаётся, расширяется на публикацию **всех** intermediate hashes (acorn proofs, challenges) для фиксированного seed.

---

### CR-C23. `MEDIUM` — документация Acorn расходится с кодом

**Местоположение**: `doc/crypto/chipmunk_ring/chipmunk_ring_acorn_verification.md` vs `chipmunk_ring_acorn.c`.

Документ описывает Acorn как «zk proof of knowledge of randomness», но фактически код считает просто `H^I(pk ‖ M ‖ r)` без witness-extraction. Это вводит в заблуждение integration-потребителей (Cellframe), которые могут ошибочно строить свою безопасность на ZK-свойствах, которых нет.

---

## 4. Обновлённая risk-матрица (Round-2)

| ID | Severity | Класс | Статус | Комментарий |
|----|----------|-------|--------|-------------|
| CR-C1 | CRITICAL | unforgeability | OPEN | single-signer не верифицирует подпись |
| CR-C2 | CRITICAL | anonymity | OPEN | signer вычисляется перебором |
| CR-C3 | CRITICAL | implementation | OPEN | `sizeof(pointer)` |
| CR-C4 | HIGH | implementation / logic | OPEN | `l_aggregation_valid` всегда true |
| CR-C5 | HIGH | anonymity / linkability | OPEN | `tag = H(pk)` |
| CR-C6 | HIGH | entropy hygiene | OPEN | `%p` в seed |
| CR-C7 | HIGH | threshold / dead-code | OPEN | Shamir детерминирован, вне активного пути |
| CR-C8 | HIGH | parameter drift | OPEN | γ=6 vs γ=4 |
| CR-C9 | HIGH | spec compliance | OPEN | SHA3 vs SHAKE + самодельный expander |
| CR-C10 | HIGH | spec consistency | OPEN | два разных ring-hash |
| CR-C11 | HIGH | unforgeability | OPEN | ring_hash не сверяется в embedded |
| CR-C12 | CRITICAL | claim validity | OPEN | NIST Level ложно завышен |
| CR-C13 | HIGH | soundness | OPEN | multi-signer ZK не зависит от sk |
| CR-C14 | MEDIUM | testing | OPEN | нет реальных anonymity-тестов |
| CR-C15 | HIGH | testing | OPEN | нет forgery / mutation тестов |
| CR-C16 | MEDIUM | DoS | OPEN | MAX_RING=1024 |
| CR-C17 | MEDIUM | dead code | OPEN | Quantum layers |
| CR-C18 | MEDIUM | code hygiene | OPEN | дубль `s_domain_hash` |
| CR-C19 | HIGH | memory | OPEN | leak в error-branch |
| CR-C20 | HIGH | semantic consistency | OPEN | два разных linkability_tag |
| CR-C21 | MEDIUM | side-channel | OPEN | non-const-time cmp |
| CR-C22 | LOW | organizational | OPEN | reproducibility |
| CR-C23 | MEDIUM | documentation | OPEN | Acorn doc vs code |

---

## 5. План Round-2 (фазы CR-6..CR-11)

### CR-6. Specification alignment (3–5 дней)

- Единый источник параметров (один header, CI-проверка).
- Single-source XOF (SHAKE256, один модуль).
- Синхронизировать статью ↔ код ↔ документацию.
- Закрывает: CR-C8, CR-C9, CR-C17, CR-C18, CR-C23.

### CR-7. Fix: unforgeability & anonymity (2–3 недели)

- Переработать `chipmunk_ring_verify`: в single-signer ветке **обязательно** верифицировать core Chipmunk (или честно переделать на OR-proof из статьи, где секрет участвует в ответе).
- Полностью переработать генерацию подписи: не использовать метод «перебрать ring, найти pk, подходящий к sk» (это сам по себе anti-anonymous).
- Вариант: реализовать классический CLSAG / LSAG / SAG с поправкой на Ring-LWE primitives.
- Закрывает: CR-C1, CR-C2, CR-C13, CR-C11.

### CR-8. Fix: implementation bugs (3–5 дней)

- `sizeof(pointer)` баг.
- `l_aggregation_valid` logic.
- `linkability_tag` унификация.
- memory leak cleanup.
- Закрывает: CR-C3, CR-C4, CR-C5, CR-C6, CR-C10, CR-C19, CR-C20.

### CR-9. Threshold scheme (обязательная фаза, 2–4 недели)

> Обновлено 2026-04-20: threshold-фича обязательна (governance/multisig/social-recovery). Вариант «удалить модуль» отвергнут.

- Переписать secret-sharing с CS-PRNG коэффициентами (`dap_random_bytes` + rejection sampling mod q).
- Исправить offset извлечения pk (Round-3 CR-D24).
- Формальная проверка Lagrange-восстановления + zero-knowledge свойств share'ов.
- Proof-of-Possession против rogue-key атак.
- Подключить к активному пути через новый API `threshold_deal/sign_partial/combine`.
- Интеграционный дизайн-док для Cellframe governance.
- Peer-reviewed security proof sketch.
- Закрывает: CR-C7, CR-D24.

### CR-10. Testing uplift (1 неделя)

- EUF-CMA тесты: mutation, forgery, wrong-key.
- Anonymity тесты: indistinguishability, key-tracing via `chipmunk_verify`.
- DoS-тесты: ring size 1024.
- Constant-time tests (`dudect`).
- Закрывает: CR-C14, CR-C15, CR-C16, CR-C21.

### CR-11. Publication readiness (2 недели)

- Исправить NIST Level claims, переписать security analysis.
- Reproducible builds, test vectors, публикация hash-traces.
- Формальное определение Acorn (или смена терминологии).
- Закрывает: CR-C12, CR-C22.

---

## 6. Рекомендация аудитора

1. **Немедленно** пометить `DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING` как **experimental / do-not-use-in-production** в `dap_enc_key.h` (короткий PR).
2. Отозвать текущие заявления «CR-0..CR-5 passed → production-ready», дополнить [security_review_findings_and_closure_summary.md](security_review_findings_and_closure_summary.md) этим Round-2.
3. Не отправлять научную статью на публикацию до завершения CR-6..CR-11.
4. Не использовать ChipmunkRing в Cellframe для юридически/финансово значимых операций до закрытия CR-C1, CR-C2, CR-C3, CR-C11, CR-C12.

---

## 7. Связанные документы

- [CR-0 Preparation Pack](security_review_cr0_preparation_pack.md)
- [CR-1 Classic Vuln Plan](security_review_cr1_classic_vuln_plan.md)
- [CR-2 Anonymity/Leakage Plan](security_review_cr2_anonymity_leakage_plan.md)
- [CR-3 Quantum Resilience Plan](security_review_cr3_quantum_resilience_plan.md)
- [CR-4 Implementation Correctness Plan](security_review_cr4_implementation_correctness_plan.md)
- [CR-5 Final Validation Plan](security_review_cr5_final_validation_and_closure_plan.md)
- [CR-0..5 Findings Summary](security_review_findings_and_closure_summary.md)
- [Scientific Paper](papers/own/chipmunk_ring_scientific_paper.tex)
- [External Reviewer Notes](papers/recense_01.txt)

---
*Round-2 audit, 2026-04-19.*
