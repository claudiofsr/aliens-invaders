# Golden Vectors (RUST.md — Fase 1)

`vectors-cpp.txt` é o **contrato executável** entre o kernel C++ e o port Rust:
cada linha é um fato determinístico do núcleo puro e da camada de entidades
(RNG, math, super-tabela honeycomb, catálogo de stages, pacing, hash de
scores, serialização do ledger, splines/trajetórias, a economia de score
`score.*`, replays seedados `ent.*` gravados na ordem de tick de produção, o
catálogo de comboios `level.*`, a transição de fase `pt.*`/`ent.pt.*` e a
economia de bônus `ent.fb.*`).

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
  `spline.*`, `traj.*`, `score.*` (Fatia 7: replay roteirizado da lógica pura
  do `Score` — linhas auto-descritivas `score.op <i> <op> <arg> …estado…`) e
  `ent.*` (camada de entidades: `ent.pix` geometria
  headless, `ent.bl.*` projéteis, `ent.wp.*` warp relativístico, `ent.dc.*`
  alpha decay, `ent.kz.*` mergulho kamikaze, `ent.fleet.*` replay completo da
  frota — auto-contido: tabela de comboios, paths e inputs por frame —,
  `ent.pl.*` jogador/bônus, `ent.ex.*` explosões, `ent.stars.*` starfield —
  LUT de twinkle e easing do warp congelados como dados gravados — e
  `ent.fs.*` contexto de score da sessão do cenário de frota: Fatia 8, linhas
  por quadro creditado + cascata de prêmios pós-replay com os bits do
  progresso fracionário) e `level.*` (Fatia 9: catálogo de comboios —
  comprimentos das 30 tabelas, os 136 comboios vivos campo a campo, o
  mapeamento de resolução `GetConvoyData` em 39 níveis e todos os pontos
  reamostrados das 25 rotas de chegada em bits IEEE), `pt.*` (Fatia 10: a
  fórmula do hold da transição — max/ceil em f32 sobre as bordas de bake do
  fanfare — e o warp de 360 quadros) e `ent.pt.*` (o arco vivo: trigger,
  hold de 361 quadros, countdown com a armada congelada, rebuild no
  catálogo real de nível 2 e os quadros de cauda), `ent.fb.*` (Fatia 11: a
  economia de bônus no replay multi-wave do catálogo real cd1→cd2→cd3 —
  prelúdio pontuado, spawn determinístico do nuke pela máscara, pickup +
  `NukeAll` pontuado, duas transições de wave e o roll capped sem draw com
  a economia no cap).
- Ordem de emissão é fixa (ver `tools/dump_vectors.cc`); streams RNG são
  sequenciais — a ordem dos draws faz parte do contrato.

## Consumidor

`rust/kernel/tests/golden.rs` — falha se qualquer linha coberta divergir.
`rust/kernel/tests/entity_parity.rs` (Fatia 4b) — replay dos cenários `ent.*`
quadro a quadro contra o port Rust da camada de entidades, **bit-exato** —
única exceção: as colunas de ângulo (derivadas de `atan2f`) comparam com
orçamento de 4096 ulps — o último bit do transcendental pertence ao build da
glibc que o replaya e o ruído integra pelo easing quadro a quadro (pior caso
medido: 672 ulps ~= 0,13°; política da Fase 3; ângulos são cosméticos —
nenhuma dinâmica os relê). O corpus fixa o toolchain para a aritmética e para o
Lemire do libstdc++ desde GCC 11, e o consumo é em ordem estrita — linha
faltante ou sobrando falha. `ent.stars.*` (Fatia 6) é **tolerância zero**: os
transcendentais (LUT de twinkle, curva do warp) são dados congelados nos dois
lados — corpus e `stars.rs` —, então nenhuma coluna admite orçamento de ulps.
`score.*` (Fatia 7) também é tolerância zero: inteiros + a divisão IEEE
`pts/step` fixada em bits, sem transcendentais — e o script de ops vive SÓ no
dumper (o teste Rust executa as próprias linhas do corpus). `ent.fs.*`
(Fatia 8) idem: o contexto de score da sessão (crédito → milestones →
ExtraShield) roda dentro do `EntitySession::tick` e o gate do dumper prova
que o replay de 900 quadros não drena nenhum milestone — as linhas
`ent.fleet.*` commitadas são intocadas pela fiação.
`level.*` (Fatia 9) idem — tolerância zero: inteiros + sqrt/IEEE apenas (o
catálogo não tem transcendentais: as factories `FlightPath::Loop` não são
referenciadas por nenhuma tabela e ficam fora do contrato) — e a identidade
das rotas é fixada por CONTEÚDO: o dumper numera os ponteiros `arrival` por
ordem de primeiro encontro e o `level_parity` casa cada entrada contra o
registro do Rust (ordem de declaração do fonte) comparando ponto a ponto.
`pt.*`/`ent.pt.*` (Fatia 10) também são tolerância zero em TODAS as
colunas: a fórmula do hold é max/ceil em f32 (IEEE, sem transcendentais) e
as colunas do arco vivo são inteiras — os ângulos (orçados em ulps nas
outras seções `ent.*`) nunca chegam às linhas `ent.pt.*`; a dinâmica que os
consome é a mesma já coberta pela política da Fase 3. As durações de
fanfare são INPUTS roteirizados (o WAV baked é dependente do dispositivo em
±1 sample): o corpus fixa a tabela do cenário e a fórmula que a transforma
em quadros de countdown.
`ent.fb.*` (Fatia 11) idem — tolerância zero: as colunas são inteiras
(contagens, máscaras, carimbos de quadro) + constantes IEEE; nenhum ângulo
chega às linhas. O dreno do sink respeita a amostragem de frames: como as
frame rows saem só a cada 60 quadros (+ eventos), linhas de sink de quadros
futuros (o heartbeat de tiro a cada 40) ficam legitimamente na fila — a
variante `drain_sink_fb` do teste só falha em carimbo velho (≤ quadro
corrente); evento a mais falha no carimbo/valor da linha consumida fora de
turno.
O gate estrutural
`entity_corpus_present` foi removido: a paridade completa o substitui.
