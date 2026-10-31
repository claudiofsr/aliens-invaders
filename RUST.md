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

| Fase | Conteúdo | Gate de validação |
|---|---|---|
| **0** | Baseline C++ tagado; este documento | — |
| **1** | `tools/dump_vectors.cc` → `golden/vectors-cpp.txt` (contratos) | corpus versionado e reproduzível |
| **2a** | Kernel Rust: RandomStream, math, formação, pacing (**este scaffold**) | `cargo test` verde contra os vetores |
| **2b** | StageCatalog, ScoreHash, HighScoreTable, bonus masks em Rust | seções `profile`/`score_hash`/`serialize` dos vetores |
| **3** | Trajectory + splines + entidades/simulação (replay dump por frame) | replays longos bit-idênticos |
| **4** | Casca SDL (wrapper seguro sobre `sdl3-sys`), renderer/placas, input, hitch monitor | sessão jogável com `-log` equivalente |
| **5** | Áudio: synth procedural + transpilador das 15 partituras + tsf via bindgen | WAV export **bit-idêntico** (KATs já existem) |
| **6** | UI/menu/layout/telemetria/starfield | paridade visual |
| **7** | Paridade total + CI Rust (5 plataformas — cross mais fácil que C++) + depots Steam idênticos | 100% dos gates |
| **8** | Aposentadoria do C++ (tag/branch de arquivo); AGENTS-RS.md | — |

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

## Estado do scaffold (Fase 1 + 2a — este commit)

- `tools/dump_vectors.cc` — dumper do kernel puro (sem SDL): rng, math, unit_grid,
  slots (3 scales), slot_edge, profile (waves 1–75), fleet/player functions,
  pacing, score_hash, serialize. Floats como **bits IEEE hex** (`%08x`).
- `golden/vectors-cpp.txt` — corpus (gerado localmente; ver golden/README.md).
- `rust/kernel` — ports de RandomStream, mathx, formation (tabela em runtime),
  pacing + `tests/golden.rs` (paridade bit a bit das seções 2a; seções 2b/3 são
  contadas mas ainda não assertadas).

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
