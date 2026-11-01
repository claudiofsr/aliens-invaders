# Golden Vectors (RUST.md — Fase 1)

`vectors-cpp.txt` é o **contrato executável** entre o kernel C++ e o port Rust:
cada linha é um fato determinístico do núcleo puro e da camada de entidades
(RNG, math, super-tabela honeycomb, catálogo de stages, pacing, hash de
scores, serialização do ledger, splines/trajetórias e replays seedados `ent.*`
gravados na ordem de tick de produção).

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
  `profile`, `fleet.*`, `player.*`, `pacing.*`, `score_hash`, `serialize`,
  `spline.*`, `traj.*` e `ent.*` (camada de entidades: `ent.pix` geometria
  headless, `ent.bl.*` projéteis, `ent.wp.*` warp relativístico, `ent.dc.*`
  alpha decay, `ent.kz.*` mergulho kamikaze, `ent.fleet.*` replay completo da
  frota — auto-contido: tabela de comboios, paths e inputs por frame —,
  `ent.pl.*` jogador/bônus, `ent.ex.*` explosões).
- Ordem de emissão é fixa (ver `tools/dump_vectors.cc`); streams RNG são
  sequenciais — a ordem dos draws faz parte do contrato.

## Consumidor

`rust/kernel/tests/golden.rs` — falha se qualquer linha coberta divergir.
`rust/kernel/tests/entity_parity.rs` (Fatia 4b) — replay dos cenários `ent.*`
quadro a quadro contra o port Rust da camada de entidades, **bit-exato e sem
tolerância**: o corpus fixa um toolchain concreto (glibc >= 2.28 para
transcendentais, libstdc++ com Lemire desde GCC 11) e o consumo é em ordem
estrita — linha faltante ou sobrando falha. O gate estrutural
`entity_corpus_present` foi removido: a paridade completa o substitui.
