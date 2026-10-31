# Golden Vectors (RUST.md — Fase 1)

`vectors-cpp.txt` é o **contrato executável** entre o kernel C++ e o port Rust:
cada linha é um fato determinístico do núcleo puro (RNG, math, super-tabela
honeycomb, catálogo de stages, pacing, hash de scores, serialização do ledger).

## Regenerar (após QUALQUER mudança no kernel puro C++)

```bash
cmake -B build -DBUILD_TESTS=OFF && cmake --build build --target dump_vectors
./build/dump_vectors > golden/vectors-cpp.txt
# ou: make dump_vectors && ./build/dump_vectors > golden/vectors-cpp.txt
```

Revise o `git diff` do arquivo antes de commitar: os vetores SÃO a especificação.

## Formato

- Uma fato por linha, tokens separados por espaço; primeiro token = seção.
- Floats como **bits IEEE-754 em hex** (`%08x`) — divergência de último bit não
  se esconde em formatação decimal.
- Seções: `rng.*`, `math.*`, `unit_grid`, `slots`, `slots.metrics`, `slot_edge`,
  `profile`, `fleet.*`, `player.*`, `pacing.*`, `score_hash`, `serialize`.
- Ordem de emissão é fixa (ver `tools/dump_vectors.cc`); streams RNG são
  sequenciais — a ordem dos draws faz parte do contrato.

## Consumidor

`rust/kernel/tests/golden.rs` — falha se qualquer linha coberta divergir.
Seções ainda não portadas (Fase 2b/3) são contadas, não assertadas.
