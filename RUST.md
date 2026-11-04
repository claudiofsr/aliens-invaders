# RUST.md — Migração C++ → Rust (strangler-fig, contratos dourados)

> Documento de processo (natureza do AGENTS.md/STEAM.md). Regra de ouro: o C++
> continua **canônico e shippable** até a Fase 7; o Rust prova paridade bit a bit
> contra os golden vectors antes de qualquer cutover. Sem big-bang.

## Por que é seguro migrar ESTE projeto

1. **Simulação determinística por seed** (RandomStream + 60 Hz fixo) → C++ e Rust
   rodam o mesmo seed e comparam hash/estado por frame.
2. **Núcleo puro headless** (Pacing, formação, catálogo, hash, ledger) → portável
   sem tocar em SDL.
3. **Formatos estáveis** (ledger com hash SHA-256, WAV export, CLI) → o port Rust
   precisa ser byte-compatível, e já existem KATs que provam isso.

## Fases e gates

| Fase | Conteúdo | Gate de validação | Status |
|---|---|---|---|
| **0** | Baseline C++ tagado; este documento | — | ✅ |
| **1** | `tools/dump_vectors.cc` → `golden/vectors-cpp.txt` (contratos) | corpus versionado e reproduzível | ✅ |
| **2a** | Kernel Rust: RandomStream, math, formação, pacing (**este scaffold**) | `cargo test` verde contra os vetores | ✅ |
| **2b** | StageCatalog, ScoreHash, HighScoreTable, bonus masks em Rust | seções `profile`/`score_hash`/`serialize` dos vetores | ✅ |
| **3** | Trajectory + splines + entidades/simulação (replay dump por frame) | replays longos bit-idênticos | ✅ (Fatias 4/4b: replay de 900 quadros bit-exato) |
| **4** | Casca SDL (wrapper seguro sobre `sdl3-sys`), renderer/placas, input, hitch monitor | sessão jogável com `-log` equivalente | 🚧 (Fatias 5–10: pré-requisitos do kernel prontos — `EntitySession` (tick + contexto de score + transição de fase), starfield, economia `Score`, catálogo de comboios; casca SDL não iniciada) |
| **5** | Áudio: synth procedural + transpilador das 15 partituras + tsf via bindgen | WAV export **bit-idêntico** (KATs já existem) | ⬜ |
| **6** | UI/menu/layout/telemetria/starfield | paridade visual | ⬜ |
| **7** | Paridade total + CI Rust (5 plataformas — cross mais fácil que C++) + depots Steam idênticos | 100% dos gates | ⬜ |
| **8** | Aposentadoria do C++ (tag/branch de arquivo); AGENTS-RS.md | — | ⬜ |

## Mapeamento C++ → Rust (decisões fixas)

| C++ | Rust | Nota |
|---|---|---|
| `constexpr` super-tabela | init de startup (`OnceLock`) ou codegen `build.rs` | float em `const fn` é instável no stable |
| `RandomStream` (xorshift) | `random_stream.rs` | bit-exato por construção |
| `std::jthread`/`stop_token`/`Portable::` | struct com `Drop` (join) + `AtomicBool` | o shim `Portable::` mapeia 1:1 |
| `std::atomic::wait/notify` | `std::sync::Condvar` ou crate `notify` | std Rust não tem atomic-wait |
| SPSC ring (`Hitch::SpscRing`) | `crossbeam-queue` ou port manual | mesmos atomics |
| SDL3 C API | `sdl3-sys` (FFI completo p/ SDL 3.2–3.4) + wrapper seguro fino | `sdl3-rs` idiomático ainda em progresso |
| stb_truetype | `ab_glyph` ou `fontdue` | upgrade, não port |
| tsf.h (TinySoundFont) | `bindgen` FFI (mantém o C, MIT) | avaliar `rustysynth` depois |
| `embedded_assets.cc` (88 MiB) | `include_bytes!` dos PNGs | fim do bake |
| SHA-256 próprio | crate `sha2` (FIPS idêntico) ou port do próprio | KATs garantem |
| `-Wconversion` discipline | `clippy::as_conversion`, `#[deny(warnings)]` | CI Rust |
| CMake/Make | Cargo workspace + `cargo-zigbuild` (Win/mac) | FreeBSD tier-2 nativo |

## Estado do scaffold (Fases 1 + 2a/2b + 3 — completas; Fatias 4/4b/5/6/7/8/9/10/11)

- `tools/dump_vectors.cc` — dumper do kernel puro + camada de entidades (sem
  SDL): rng, math, unit_grid, slots (3 scales), slot_edge, profile (waves
  1–75), fleet/player, pacing, score_hash, serialize, splines, trajetórias,
  os replays seedados `ent.*` (Fatia 4; starfield na Fatia 6), o replay
  roteirizado `score.*` (Fatia 7), o contexto de score da sessão
  `ent.fs.*` (Fatia 8), o catálogo de níveis `level.*` (Fatia 9), a
  transição de fase `pt.*`/`ent.pt.*` (Fatia 10) e a economia de bônus
  `ent.fb.*` (Fatia 11). Floats como **bits IEEE hex**
  (`%08x`).
- `golden/vectors-cpp.txt` — corpus (gerado localmente; ver golden/README.md).
- `rust/kernel` — ports de RandomStream, mathx, formation (tabela em runtime),
  pacing, StageCatalog, ScoreHash (SHA-256), HighScoreTable (serialização
  byte-idêntica), path (Catmull-Rom + resampler), trajectory (máquina de
  estados arriving/joining/cruising/attacking, mergulho e kamikaze) e a camada
  de entidades (Fatia 4b): dist (uid Lemire do libstdc++), textures/sink
  headless, game_object, bullets (seeker/relativístico/exaustão), bonus,
  explosions, alien (warp/decay/kamikaze/spin), player, fleet (AliensManager:
  comboios, ocupação lazy, criação, drift, fire, wanderers e a economia de
  bônus com score vivo — Fatia 11) e session (Fatia 5:
  `EntitySession` — pipeline de tick da física fixa, a ordem canônica de
  produção atrás de um único `tick(dir, fire)` com `TickReport` por quadro;
  Fatia 8: dona do contexto de score — crédito por quadro, drenagem de
  milestones de escudo extra e `ExtraShield` no jogador, com
  `extra_shields` no `TickReport`; Fatia 10: dona da transição de fase —
  gatilho na armada finalizada, hold do fanfare, countdown que congela a
  armada e rebuild pelo catálogo com um draw de RNG por wave; Fatia 11:
  fia o `Score` vivo na kill chain e no pickup do nuke — `score_mut()`) e
  stars (Fatia 6: `StarsFields` — starfield de parallax com os transcendentais
  congelados como dados gravados) e score (Fatia 7: `Score` — economia de
  pontos: Add saturante, ledger fracionário de milestones de escudo extra,
  clamp de escudo, ciclo wave→stage e a máscara da economia de bônus) e
  level_data (Fatia 9: o catálogo de comboios — arquivo GERADO por
  `tools/extract_level.py`, 25 splines de chegada reamostradas pelo `path`
  golden-pinado e as 30 tabelas por wave com o mapeamento de produção do
  `GetConvoyData`).
- Política de tolerância (Fase 3): seções puramente aritméticas + sqrt são
  bit-exatas (splines, init, attacking/cruising); quadros do arco de docking
  (atan2/cos/sin) comparam posição com tol 1e-2 px e **sequência de estágios
  exata** — ulps de transcendentais variam entre versões de libm, lógica não.
- A leg `Rust kernel parity` do CI executa tudo a cada push.

### Desacoplamento da camada de entidades (pré-requisito da Fase 3+, fatias)

- **Fatia 1 (fix18)**: `PresentationSink` (fronteira core→shell) injetado em
  AliensManager/Alien; Fire/DoBulletsCollisions lêem o snapshot `metrics_` em
  vez de `Gfx::Inst()` ao vivo. Gameplay byte-idêntico; 49 testes + 6 legs validam.
- **Fatia 2 (fix19)**: métricas injetadas em ConvoySize, ProjectileSlot/
  BonusSlot (Out/Move), Bullets/Bonus/Explosions e Player; Player também emite
  cosméticos pelo sink.
- **Fatia 3 (fix20)**: `Pix` com escala global desacoplada da `Gfx` (espelhada
  nos 4 sítios de recálculo) + `CreateHeadless()`; `TextureProvider` injetado em
  AliensManager/BonusManager/Player; `RefreshMetrics` aposentado (`SetMetrics` +
  `DistributeMetrics`); Draw* fisicamente movidos para `actors_draw.cc` /
  `managers_draw.cc` / `explosion_draw.cc`. actors/managers/explosion agora
  linkam sem SDL — pré-requisito dos vetores de entidades.
- **Fatia 4 (fix21)**: `dump_vectors` linka os TUs puros de entidades
  (actors/managers/explosion) e grava replays seedados com RecordingSink +
  texturas/Config headless (`Pix::CreateHeadless`, `Config::CreateHeadless`):
  frota sintética na ordem de tick de produção com seed buscado para garantir
  cobertura (warp relativístico usado, alpha-decay nascido, bônus coletado),
  micro-contratos por alien (warp, decaimento, mergulho kamikaze),
  jogador/bônus/escudo, projéteis (seeker/relativístico/tabela-verdade do
  trap) e explosões. Seções `ent.*` no corpus (replay da frota auto-contido:
  tabela de comboios, arrival paths e inputs por frame são linhas); teste 50
  (`TestHeadlessEntitySimulation`) guarda o determinismo headless no CI.
- **Fatia 4b (fix22)**: ports Rust da camada de entidades (10 módulos novos +
  superfície de entidade em trajectory/formation) e `tests/entity_parity.rs`:
  os 8 cenários `ent.*` replays quadro a quadro **bit-exatos**, com uma única
  exceção: as colunas de ângulo (derivadas de `atan2f`) admitem 4096 ulps —
  o último bit de transcendental pertence ao build da glibc que o replaya e
  o ruído de 1–2 ulps do alvo integra pelo easing quadro a quadro até um
  offset estacionário (pior caso medido no replay completo: 672 ulps
  ~= 0,13°), e ângulos são cosméticos — nenhuma dinâmica relê `angle_`,
  então o orçamento não mascara divergência de estado. Todo o resto é mais estrito
  que a política da Fase 3; o corpus fixa o toolchain para a aritmética e
  para o Lemire do libstdc++ desde GCC 11 (ver `src/dist.rs`). Consumo do corpus em ordem estrita: linha
  faltante OU sobrando falha (`Rows::next`/`Rows::done`), então um corpus
  truncado não passa despercebido. Duas disciplinas de RNG que o port
  cristaliza: (1) `RandomStream::UniformInt` nativo (rejeição por módulo) e
  `std::uniform_int_distribution` (Lemire) são algoritmos DIFERENTES — cada
  sítio C++ usa o seu e o twin espelha o sítio (exaustão do seeker = nativo;
  `wave_phase` = Lemire); (2) paridade de ponteiro vivo — `Trajectory` em C++
  lê o `base_cruise_` do manager por ponteiro; o twin Rust recebe o base
  re-empurrado a cada tick, após o drift e antes do loop de aliens, o que
  importa para o destino absoluto do warp. O gate estrutural
  `entity_corpus_present` (Fatia 4) foi removido: a paridade completa o
  substitui. O replay da frota usa a seed buscada do corpus e mantém o
  contrato de cobertura (warp usado, decay nascido, bônus coletado, 900
  quadros na ordem de tick de produção).
- **Fatia 5 (fix25)**: `rust/kernel/src/session.rs` — `EntitySession`, o
  pipeline de tick da física fixa da camada de entidades (o subconjunto de
  entidades de `CombatSession::TickFixedPhysics`) atrás de um único
  `tick(dir, fire) -> TickReport` (colisões, score, proximidade kamikaze).
  A orquestração sai do harness de teste e entra no kernel: `fleet_parity`
  consome o corpus via `session.tick`. C++ e corpus inalterados — os 900
  quadros `ent.fleet.*` já eram o contrato golden dessa ordem de tick. Os
  ramos do shell (cinemática de supernova/destruição, estrelas, contexto de
  score, transições de fase) permanecem em C++ até a Fase 4: a sessão
  REPORTA a proximidade kamikaze, não a encena.
- **Fatia 6 (fix26)**: starfield no kernel + split físico do starfield C++.
  `src/starfield/stars.cc` fica com a lógica pura (sem SDL, linkável no
  `dump_vectors`); `Draw`, o flare, o construtor default e o poll de janela
  `Gfx` (`CheckWindowResize`) mudam para `stars_draw.cc` — a mesma receita
  dos `*_draw.cc` da Fatia 3. As leituras vivas de `Gfx::Inst()` viram o
  espelho de viewport rastreado (produção byte-idêntica: o espelho sincroniza
  no `CheckWindowResize` que os call sites agora rodam logo antes do
  `Scroll()`); costuras headless: construtor `StarsFields(int,int)`,
  `SetViewport`, hook de release do flare (contrato do `Pix`) e
  `AddDetailsLevel` inline no `config.h`. O corpus cresce 7.378 linhas
  `ent.stars.*`: replay de 640 quadros com warp hiperespacial de produção
  (360 quadros) em f60, crescimento 1600×900 em f480 (expansão de densidade)
  e details 2→1 em f560 (encolhimento) — hash FNV-1a do pool inteiro por
  quadro + 8 estrelas amostradas + snapshots completos. **Tolerância zero**:
  a LUT de twinkle (sinf) e o easing do warp (cosf) são transcendentais
  GRAVADAS, congeladas como dados no corpus e em `rust/kernel/src/stars.rs`
  (política do `textures::HEADLESS_DIMS`) — o replay é aritmética IEEE pura +
  RandomStream + sqrt corretamente arredondado, bit-exato em todas as
  colunas; regenerar o corpus em outra glibc re-grava as tabelas e o teste
  aponta a linha driftada (re-congelar as consts). Rust: módulo `stars` +
  teste `stars_parity` (18º da suíte).
- **Fatia 7 (fix27)**: economia de score no kernel + destrave headless do
  `Score`. `src/ui/score.h` não inclui mais `gfxinterface.h`: `SDL_Texture`
  vira forward declaration, o construtor vira inline (`ReInit()`) e o
  destrutor passa pelo hook de release das placas HUD (contrato do
  star-flare/`Pix` — binder estático em `score.cc`, a casca gráfica), então a
  classe pura compila e linka no `dump_vectors` sem SDL. Zero mudança de
  comportamento nos call sites: a API é a mesma e os accessors novos são
  apenas observabilidade golden (`ShieldProgress`, `Cheated`,
  `StageBonusMask`, `StageBonusCycle`). O corpus cresce 87 linhas `score.*`:
  replay ROTEIRIZADO (84 ops) com eco de estado completo por linha — Add
  saturante (UINT64_MAX), ledger de milestones de escudo extra com bits
  double fixados (drenagem subtrativa: multi-milestone, restos fracionários e
  a divisão IEEE `pts/1000`), clamp de escudo (incl. o sentinela negativo de
  destruição), dois rollovers de ciclo wave→stage (máscara da economia de
  bônus: lockout de duplicatas, contagem 5/5, reset no ciclo novo), cheat
  flag e ReInit. As linhas são auto-descritivas (`score.op <i> <op> <arg>
  …estado…`): o script existe SÓ no dumper — o teste Rust replaya a partir do
  corpus. **Tolerância zero**: inteiros + uma divisão IEEE fixada, nenhum
  transcendental. A máscara de bônus é o gate que o
  `AliensManager::RollSmartBonusType` consulta via `Score*` (o corpus de
  frota o liga nulo; um replay de frota score-wired é trabalho futuro).
  Rust: módulo `score` + teste `score_parity` (19º da suíte).
- **Fatia 8 (fix28)**: contexto de score dentro do `EntitySession`. O bloco
  de produção entre `DoBulletsCollisions` e `DoBombsCollisions`
  (`score.Add(score_earned)` → `CheckExtraShieldMilestones` →
  `player_.ExtraShield()` por milestone, com SFX) entra no tick do kernel: a
  sessão passa a possuir um `Score` (Fatia 7), o `TickReport` carrega
  `extra_shields` por quadro (o SFX_SHIELD_RESTORE por milestone continua
  casca), o contador `score_total` sai (a economia vive em `score()` — sem
  aliases) e `award_points` expõe o mesmo bloco para quantias sintéticas. O
  harness do dumper replica o bloco na posição exata e o corpus cresce 14
  linhas `ent.fs.*`: setup, uma linha por quadro creditado (9 quadros, total
  10→130, bits do progresso e escudo no fim do quadro), a cascata de prêmios
  pós-replay (1000/2000/5000 → exatamente 1/2/5 milestones, determinística
  para qualquer resíduo) e done. O resíduo importa: 0,13 acumulado crédito a
  crédito tem bits diferentes de 130/1000 de uma vez, e as linhas os fixam.
  Gates duros: zero milestones durante o replay — a prova de que as 900
  linhas `ent.fleet.*` commitadas não são tocadas pela fiação do Score (130
  pontos < step 1000; a busca de semente continua escolhendo a seed 2 e a
  seção saiu byte-idêntica) —, ≥5 quadros creditados, cascata 1+2+5 e escudo
  final clampado em 8. Corpus: 32.568 linhas. Rust: `session` +
  `entity_parity` atualizados; a suíte segue com 19 testes (`fleet_parity`
  absorve o `ent.fs.*`).
- **Fatia 9 (fix29)**: catálogo de comboios no kernel
  (`rust/kernel/src/level_data.rs`, arquivo GERADO — regenerar com
  `tools/extract_level.py` após qualquer mudança em
  `src/entities/level_data.cc`; o fonte C++ segue intocado). Duas metades
  com o contrato exato de produção: `paths()` — as 25 splines de chegada
  reamostradas pelo `path::generate_smooth_spline` já golden-pinado (o
  catálogo não tem transcendental: as duas factories `FlightPath::Loop` não
  são referenciadas por nenhuma tabela e ficam fora do contrato) — e
  `get_convoy_data(level)` / `get_total_levels()` — as 30 tabelas (15 por
  estágio) com o mapeamento de produção (`level==0→1`, `(level−1)%15`,
  `WaveToStage≥2` → estágio 2; o `size_t` do C++ espelhado como `usize`,
  mesmos wraps). O corpus cresce 12.534 linhas `level.*` (total: 45.102):
  uma linha por tabela, todos os campos dos 136 comboios vivos, o
  mapeamento de resolução em 39 níveis (ciclo 1 completo, fronteiras de
  estágio/wrap e níveis profundos) e todos os 12.301 pontos reamostrados
  das 25 rotas em bits IEEE. A identidade de rota é fixada por CONTEÚDO: o
  dumper registra os ponteiros `arrival` por ordem de primeiro encontro e o
  teste casa cada `path_id` do Rust comparando ponto a ponto, não a
  ordenação. **Tolerância zero** (inteiros + sqrt/IEEE, como `spline.*`).
  Gates duros no dumper: ≥20 rotas, 30 tabelas, ≥100 comboios, ≥3.000
  pontos. É o órgão que a Fatia 10 (transição de fase — fanfare, rebuild da
  frota no nível seguinte, warp hiperespacial) consome. Rust: módulo
  `level_data` + `level_parity` (20º teste da suíte).
- **Fatia 10 (fix30)**: transição de fase no `EntitySession`. O bloco de
  produção do `CombatSession::TickFixedPhysics` entra no tick do kernel na
  ordem exata: sync `score.set_shield` + reporte de destruição
  (`player_destroyed` — produção encerra o quadro com a cinemática; o
  kernel reporta e continua, o precedente da proximidade kamikaze), o
  gatilho na armada finalizada (a duração do fanfare é INPUT roteirizado —
  em produção ela vem do WAV baked via `PlayStageFanfare`, dependente do
  dispositivo em ±1 sample — indexada por `wave_in_stage`;
  `hold = max(6 s, duração)` → `max(1, ceil(hold·60))` quadros; o
  `IncLevel` aterrissa antes do countdown), o countdown que congela a
  armada (o early-return do Move/Fire de produção) e o rebuild consumindo o
  catálogo da Fatia 9 — `Fleet::new` é o twin bit-idêntico documentado do
  `Rebuild` para a mesma seed — com um draw do `RandomStream` por wave (o
  papel do `ctx_.rng`) e um `TickReport` estendido que a casca lê (fanfare,
  warp, progresso do overlay, destruição). No C++, sai o
  `fast_star_scrolling_time_` write-only (o mecanismo vivo do warp é o
  `stars.TriggerHyperspaceWarp` — o membro era atribuído e decrementado sem
  nenhuma leitura). O corpus cresce 1.009 linhas: 10 casos `pt.hold`
  fixando a fórmula nas bordas de bake (6,0 s + 1 sample a 44,1/48 kHz →
  **361 quadros**; 1 ulp acima de 6,0; 7,25 s; 12,5 s; 0; 5,5; 1e-30),
  `pt.warp` (FastRound(6·60) = 360 = `WARP_DURATION_FRAMES`) e o arco
  `ent.pt.*` completo — wave 1 com 5 comboios sintéticos de uma nave
  (honrando o invariante do catálogo "tamanho da tabela == convoy_count do
  profile"), jogador estacionário compartilhando a coluna de balas com os
  mergulhadores, trigger no f285, hold de 361, rebuild no f645 a partir do
  catálogo REAL de nível 2 (cd2: 6 comboios, seed 2023342831 do draw), 10
  quadros de cauda com o delay pós-fanfare e o schedule de spawn, e o
  caminho de destruição exercitado (221 quadros com escudo < 0). Gate duro
  na busca de 64 seeds: arco completo (5 kills, rebuild == trigger+360,
  nível final 2). **Tolerância zero em todas as colunas** (inteiros +
  max/ceil em f32; nenhum ângulo chega às linhas). Corpus: 46.111 linhas.
  Rust: `session` (5 campos novos + `phase_transition_frames` pub),
  `stage_catalog` (a constante do fanfare) e `phase_transition_parity`
  (21º teste da suíte).
- **Fatia 11 (fix31)**: economia de bônus na dinâmica viva da frota. O
  `RollSmartBonusType` de produção com o `Score*` vivo — cap de estágio
  (5 tipos gravados → `none` SEM consumir draw) e a máscara de ciclo
  (`can(t)` zera o peso dos tipos já sorteados na wave) — e os gates por
  kill do `CreateBonusMaybe` (`bonus_allowed_this_level` && !spawned &&
  `--bonus_wait > 0`; o tipo escolhido é gravado ANTES do pod cair) entram
  na kill chain do Rust: `do_bullets_collisions(…, score: Option<&mut
  Score>)` threada até `create_bonus_maybe` → `roll_smart_bonus_type`, e o
  `EntitySession::tick` passa o membro de produção (`Some(&mut
  self.score)`) com `score_mut()` exposto (prelúdio golden). O harness do
  `ent.fleet.*` mantém o `nullptr`: as 900 linhas commitadas não são
  tocadas pela fiação — `fleet_parity` verde pós-fiação é a prova
  executável. O C++ recebe só accessors de observabilidade read-only no
  `managers.h` (`BonusAllowedThisLevel`/`BonusWait`/`BonusSpawnedThisLevel`,
  precedente do `score.h` na Fatia 7) para as linhas distinguirem rolls
  capped (economia no cap, sem draw) de waves que nunca rolaram. O cenário
  também pegou um bug real de port: o `NukeAll` credita
  `kScorePerAlienNuked` por alien via `score_` e o `nuke_all` do Rust não
  conhecia score (nunca exercitado em paridade antes) — corrigido com o
  mesmo threading (`player.do_bonus_collisions(…, score)`), inerte nas
  seções anteriores (o banner `+20` do nuke só existe no `ent.fb.*`). O
  corpus cresce 627 linhas `ent.fb.*` (total: 46.738): cenário de
  catálogo REAL (cd1 → cd2 → cd3, nenhuma tabela sintética), Score vivo,
  prelúdio pontuado (4 tipos gravados → o nuke é o único sorteável — spawn
  determinístico sem gating de jogador), jogador estacionário na coluna das
  balas, fanfares planos de 6,0 s (holds de exatos 360). Arco da seed 1
  (busca 1..256, budget de 10k quadros): spawn do nuke no f975 (máscara
  30→62, count 5) → pickup → `NukeAll` (+300, 15 aliens) → trigger f1416 →
  rebuild f1775 (cd2 real, seed 133652670 do draw) → wave 2 com Bernoulli
  false (nenhum drop; grind com mortes do jogador — destruição report-only,
  o harness nunca responde ao flag) → trigger f8021 → rebuild f8380 (cd3
  real, seed 2549775502) → wave 3 com Bernoulli true, wait=8 esgotado
  pelos kills → roll capped no f9683 (sem draw, sem spawn) → done f9694
  (55 kills, 1 spawn, máscara 62 preservada pelos rebuilds). Linhas: setup
  (13 cols), 4× prelude, 219 frame (amostragem: cada 60 quadros + quadros
  de evento; 15 cols com count/mask/allowed/wait), 397 sink (dreno TODO
  quadro — linhas de sink caem em quadros sem frame row; a variante
  `drain_sink_fb` do Rust só recusa carimbo velho), spawn, 2× trigger, 2×
  rebuild, done. Gate duro: exatamente 1 spawn (nuke), count 5, máscara 62,
  ≥1 roll capped, ≥1 rebuild, nível final ≥2. **Tolerância zero em todas as
  colunas** (inteiros + constantes IEEE — nenhum ângulo chega às linhas).
  Rust: `fleet`, `player`, `session` + `fleet_bonus_parity` (22º teste da
  suíte).

## Comandos

```bash
# 1) gerar os golden vectors (uma vez por mudança no kernel C++):
cmake -B build -DBUILD_TESTS=OFF && cmake --build build --target dump_vectors
./build/dump_vectors > golden/vectors-cpp.txt        # ou: make dump_vectors

# 2) portabilidade Rust:
sudo pacman -S rustup && rustup default stable
cd rust && cargo test                                 # paridade contra os vetores
```

## Regras do período strangler

1. C++ é a fonte da verdade: mudou o kernel puro → regenerar vetores → revisar o
   diff (os vetores SÃO a especificação executável).
2. Nenhum comportamento novo entra no Rust antes de existir no C++ com vetor.
3. O jogo lançado continua sendo o C++ até a Fase 7 completar todos os gates.
4. Constantes duplicadas durante a migração são aceitas **somente** porque os
   golden vectors as comparam a cada `cargo test` (SSOT provisório vigiado).
