# 🤖 SISTEMA DE AGENTES: ALIENS INVADERS (C++20 / SDL3)

```
                                ┌────────────────────────────────────────┐
                                │         AGENTE 1: ORQUESTRADOR         │
                                │       Clean Architecture & Design      │
                                └───────────────────┬────────────────────┘
                                                    │
      ┌──────────────┬──────────────┬───────────────┼───────────────┬──────────────┬──────────────┐
      ▼              ▼              ▼               ▼               ▼              ▼              ▼
┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐
│  AGENTE 2  │ │  AGENTE 3  │ │  AGENTE 4  │ │  AGENTE 5  │ │  AGENTE 6  │ │  AGENTE 7  │ │  AGENTE 8   │
│Super-Tabela│ │ Entidades  │ │Performance │ │ Deep DRY & │ │Concorrência│ │Anti-Stutter│ │  Maestro    │
│ Honeycomb  │ │ Especiais  │ │Compile-Time│ │ QA / Testes│ │ Telemetria │ │Frame Pacing│ │Síntese/Áudio│
└────────────┘ └────────────┘ └────────────┘ └────────────┘ └────────────┘ └────────────┘ └─────────────┘
```

---

## 🏛️ AGENTE 1: Orquestrador de Arquitetura & Código Limpo (Clean Architecture)

### Objetivo
Garantir código direto, modular, sem camadas desnecessárias de abstração, eliminando arquivos e classes obsoletas e preservando simetria rigorosa em todo o ecossistema.

### Mandamentos Inegociáveis
1. **Zero Aliases / Código Direto:**
   - Proibido manter wrappers ou funções de compatibilidade retroativa (ex.: nada de `HSpacing()` redirecionando para `SpacingX()`). Refatore os chamadores diretamente na fonte.
2. **Simetria Rigorosa de Nomes:**
   - Eixos X e Y devem possuir nomenclaturas estritamente paralelas:
     - `SpacingX()` e `SpacingY()`.
     - `offset_x` e `offset_y` (em `UnitCoord` e `FormationSlot`).
     - `base_cruise_.x` e `base_cruise_.y`.
3. **Eliminação de Código Morto:**
   - Remover scripts legados ou temporários (ex.: `fix-gemini.py`, `.bak`, `.tmp`).
   - Remover métodos não utilizados de iterações antigas (ex.: métodos legados de Life e Nuke duplicados em `score.h`).
   - Remover codigo velho, morto ou obsoleto.
   - Nao criar aliases de codigo velho para manter compatibilidade.
   - Nao manter compatibilidade com codigo velho, remova-os sempre.
4. **Isolamento de Subsistemas:**
   - O menu principal deve abrir sem inicializar subsistemas pesados de hardware (o áudio só é iniciado sob demanda com a tecla `S` / `Start`).
5. **Padrão de Comentários no Código (Inglês Didático & Sem Arqueologia):**
   - Todos os comentários de código inseridos ou modificados devem ser redigidos em inglês técnico didático explicando a motivação técnica (*why*), não apenas o óbvio (*what*).
   - Comentários legados em português tocados por deltas devem ser traduzidos para inglês didático.
   - **Proibido changelog arqueológico:** comentários não devem citar iterações antigas (`fix9`, `fix11`, "user revert", etc.). O comentário descreve a arquitetura no tempo presente.

---

## 🐝 AGENTE 2: Especialista em Cinemática & Super-Tabela Honeycomb

### Objetivo
Gerenciar a formação da frota inimiga em colmeia (*honeycomb*) pré-calculada, eliminando micro-teleportes, garantindo trajetórias contínuas e o salto relativístico determinístico do Alien Relativístico (Alien 14).

### Mandamentos Inegociáveis
1. **Super-Tabela Estática e Pré-Calculada:**
   - Dimensões fixas: `kMaxGridRows = 12`, `kMaxGridCols = 16`, `kNumStageModes = 5`.
   - Gerada em tempo de compilação (`constexpr`) em coordenadas unitárias normalizadas (`offset_x`, `offset_y`) e escalada uma única vez quando a resolução da janela for alterada.
2. **Cálculo Simétrico de Cruzeiro:**
   - Em `Trajectory::CruiseTarget()`, as coordenadas devem ser 100% simétricas:
     - `alvo.x = base_cruise_.x + slot.offset_x`
     - `alvo.y = base_cruise_.y + slot.offset_y`
   - No eixo Y, `base_cruise_.y` recebe o horizonte estático `BaseCruiseY() = 82 px`.
   - No eixo X, `base_cruise_.x` oscila dinamicamente entre `min_cruise_x_` e `max_cruise_x_`.
3. **Salto Relativístico do Alien 14 (Albert Alienstein):**
   - **Tipos e Inicialização:**
     - Há dois tipos de Alien 14:
       a. Alien Relativístico (com poder de salto); e
       b. Alien 14 sem poder de salto.
     - Probabilidade de nascer como Alien Relativístico: `kAlien14WarpEvasionProbability`.
     - O Alien Relativístico inicia com exatamente 2 saltos (`kAlien14NumWarpEvasions = 2`).
   - **Gatilho de Salto:**
     - Passo 1 — Condições simultâneas para saltar:
       a. Um projétil penetra o raio de aproximação do Alien Relativístico (`kAlien14WarpTriggerRadiusFactor`); e
       b. O Alien Relativístico possui pelo menos 1 salto (`kAlien14NumWarpEvasions > 0`).
     - Passo 2 — Efeitos imediatos da ativação:
       a. Consome 1 salto da reserva (`-1`);
       b. Inicia recarga visual de 1.4 s (`kAlien14WarpCooldownSeconds`); e
       c. O Alien Relativístico entra em dobra (`is_warping_ = true`).
   - **Deslocamento Relativístico:**
     - Durante o trânsito de dobra ou deslocamento relativístico:
       a. Desativa os colisores físicos do Alien Relativístico;
       b. O projétil lançado pela nave terráquia segue sua trajetória normal.
   - **Condição Única de Morte:**
     - O Alien Relativístico morre se, e somente se:
       a. Bomba NukeAll;
       ou (
       b. Não estiver em trajetória de salto (`is_warping_ == false`); e
       c. For atingido por projétil com zero saltos restantes (`kAlien14NumWarpEvasions == 0`)
       ).
       Ou seja, NukeAll mata em dobra, porém projéteis nunca matam em dobra.
       Morte se: NukeAll ou (!is_warping_ && evasões==0);
   - **Geometria de Segurança (Zonas e Domínio):**
     - Métrica de Chebyshev:
       a. Mede pela maior diferença de colunas ou linhas: `max(|delta_col|, |delta_row|)`; e
       b. Opera com números inteiros, sem raiz quadrada (`sqrt`).
     - Zona de Exclusão (áreas proibidas para pouso):
       a. Zona 1: Favos com distância de Chebyshev <= N (`kAlien14WarpMinCellSeparation`) do Alien Relativístico; e
       b. Zona 2: Favos com distância de Chebyshev <= N da nave terráquia do jogador.
     - Domínio do Sorteio:
       a. Formação ativa menos as Zonas 1 e 2; e
       b. Garante distância > N por construção geométrica.
       c. Evitar sobreposição de todos os aliens em geral (aliens 1 ao 15) na colmeia:
       - na formação em esquadra ou 
       - na escolha da posiçao final do salto (after the jump) ou 
       - em retorno após descer e retornar na posição superior da tela (after the dive re-entry) ou
       - em todos os outros casos.
   - **Algoritmo de Seleção em O(1):**
     - Prioridade 1 (Sorteio Direto):
       a. Usa exclusivamente `RandomStream` determinístico; e
       b. Sorteia um índice no Domínio em tempo constante O(1), sem repetição.
     - Prioridade 2 (Verificação de Ocupação):
       a. Favo livre: o Alien Relativístico o ocupa; e
       b. Favo ocupado: varre horizontalmente afastando-se da nave terráquia até achar um livre fora da exclusão.
     - Prioridade 3 (Fallback):
       a. Sem favos válidos: o Alien Relativístico salta para o canto superior mais distante da nave terráquia.
     - Prioridade 4 (Fixação e Cache):
       a. Contém as coordenadas na tela via `std::clamp`;
       b. Salva o destino em cache único durante o percurso; e
       c. Torna-se o novo favo permanente do Alien Relativístico na colmeia.
4. **Cinemática e Erradicação de Micro-Teleportes:**
   - **Trânsito de Dobra:**
     a. Dura no máximo `kAlien14WarpMaxFrames` quadros;
     b. Gera rastro de fantasmas (`kAlien14WarpGhostCapacity`); e
     c. Ao aterrissar, encerra a dobra e reativa os colisores do Alien Relativístico.
   - **Movimento Regular (fora do salto):**
     a. O Alien Relativístico persegue o alvo com velocidade limitada por quadro; e
     b. Nunca atribui a posição instantaneamente.
   - **Trava de Salto Brusco (*Delta Jump Guard*):**
     a. Em dobra (`is_warping_ == true`): salto rápido liberado; e
     b. Fora da dobra (`is_warping_ == false`): qualquer deslocamento além da velocidade permitida é travado.

---

## ⚡ AGENTE 3: Especialista em Entidades & Regras Especiais de Combate

### Objetivo
Manter o comportamento determinístico e balanceado dos inimigos, bônus e projéteis com zero alocação de memória no loop principal.

### Mandamentos Inegociáveis
1. **Alien 4 (Madame Curie) & Decaimento Alfa:**
   - Sorteada probabilisticamente no nascimento para possuir ou não uma eletrosfera (`kAlien4ElectrosphereProbability = 0.20f`).
   - **Com eletrosfera (2 impactos):** O primeiro tiro causa Alpha Decay:
     - Encolhe para 50% de tamanho (`kAlien4AlphaDecayScale = 0.50f`).
     - Triplica a velocidade orbital dos elétrons (`kAlien4DecayedOrbitSpeedMult = 3.0f`).
     - Acelera a velocidade de voo da nave em +30% (`kAlien4DecayedSpeedMult = 1.30f`).
     - Gera partículas e aura cintilante; apenas o segundo tiro é fatal.
   - **Sem eletrosfera (1 impacto):** Neutralizada em um único tiro convencional.
2. **Inimigos Kamikaze (Aura Pulsante Vermelha):**
   - Alertam visualmente pulsando em vermelho e emitindo sirene sonora antes de mergulhar.
   - Raio de destruição por proximidade é letal (1.4 vezes a largura da nave).
3. **Mísseis Teleguiados & Fair-Play:**
   - Projéteis inimigos com deflexão vetorial de até +/-20 graus.
   - **Corredor de Evasão Obrigatório:** O jogo nunca cria armadilhas em tesoura sem saída; sempre garante um espaço seguro de no mínimo `kSafeEvasionCorridorPixels = 118 px` para esquiva do jogador.
4. **Economia de Bônus (Supply Pods):**
   - No máximo 5 bônus por ciclo de estágio de 15 fases (Speed, Fire, Multi, Shield, Nuke).
   - Zero drops duplicados do mesmo tipo dentro do mesmo estágio.

---

## 🚀 AGENTE 4: Otimização de Performance, Memória & Tempo de Compilação

### Objetivo
Minimizar o consumo de CPU e RAM, movendo estruturas e cálculos dinâmicos para `constexpr` e seção `.rodata`.

### Mandamentos Inegociáveis
1. **Zero Alocação Dinâmica no Loop Quente:**
   - Terminantemente proibido usar `new`, `delete`, `malloc`, `std::vector::push_back` (com redimensionamento dinâmico) ou instanciação de `std::string` no loop de simulação e renderização (60 Hz).
   - Utilizar arrays estáticos contíguos (`std::array`) com listas densas de índices ativos gerenciadas por Swap-and-Pop O(1).
2. **Tabelas Pré-Calculadas em Tempo de Compilação:**
   - Trigonometria de anéis de órbita, rotações de giro (*spin*), decaimento de explosão e posições da colmeia devem residir na seção `.rodata` via funções `constexpr` imediatas.
3. **Render Targets em Texturas Compartilhadas (GPU):**
   - Menus estáticos, debriefing e telas de pausa devem ser renderizados **uma única vez** para uma textura de GPU (`SDL_Texture` com `SDL_TEXTUREACCESS_TARGET`) e reapresentados em um único blit, mantendo a CPU abaixo de 0.35% nessas telas.
4. **Layout de Dados Cache-Friendly:**
   - Estruturas de dados organizadas para alinhamento de linha de cache, evitando saltos indiretos de ponteiro nos loops de alta frequência.

---

## 🔍 AGENTE 5: Inspetor de Qualidade, Deep DRY & Auditoria de Testes

### Missão
Eliminar duplicidades em constants.h e em todo o código, centralizar métricas e garantir que a suíte de testes unitários passe sem regressões, inclusive a cada nova funcionalidade essencial.

### Mandamentos
1. SSOT em `constants.h`: cada constante física, de gameplay ou layout existe em um único ponto canônico.
2. Proibido número mágico em regras de jogo, física, layout ou colisão.
3. Os testes devem aferir nomes simétricos (`SpacingX`, `SpacingY`) e propriedades físicas reais. 
4. Proibido criar constantes artificiais para eixos dinâmicos, como `kFleetBaseCruiseXPixels`.
5. Toda funcionalidade essencial deve ter teste automatizado.
6. Toda correção de bug deve incluir teste de regressão.
7. Toda mudança estrutural deve compilar e passar em `tests/unit_tests.cc`.
8. Testes devem validar nomes simétricos, propriedades físicas reais e contratos essenciais.
9. Deep DRY: nenhuma duplicidade de classe, método, função, constante ou alias.
10. Áudio pertence ao AGENTE 8; nenhuma alteração pode regredir performance ou introduzir alocação dinâmica no callback.

### Critério de Aprovação
- Build OK.
- Testes 100% passando.
- Zero constantes duplicadas.
- Zero números mágicos críticos.
- Zero aliases semânticos.
- Zero regressões.
- Zero riscos indevidos ao áudio.

---

## 🧵 AGENTE 6: Especialista em Concorrência Segura, Watchdog & Telemetria de Hitches

### Objetivo
Monitorar a integridade temporal do jogo, registrar telemetria com mínima intrusão e identificar com precisão cirúrgica a causa-raiz de travamentos (*hitches*), gargalos e descompassos entre threads.

### Mandamentos Inegociáveis
1. **Concorrência Segura em C++20 (`std::jthread` & `std::stop_token`):**
   - Toda rotina assíncrona paralela (áudio em segundo plano, persistência de disco, telemetria) deve obrigatoriamente usar **`std::jthread`** com cancelamento cooperativo via **`std::stop_token`**.
   - Proibido o uso de raw threads (`std::thread`), laços de espera ocupada (*busy-wait*) travando núcleos de CPU ou joins bloqueantes no laço principal.
2. **Diagnóstico Cirúrgico de Hitches (`--log`, `-log` ou `ALIENS_HITCH_LOG=1`):**
   - Ao executar com a flag `--log` (ou alias `-log`), o flight recorder de frames lentos deve decompor qualquer quadro que exceda o limiar de orçamento temporal (configurável por `ALIENS_HITCH_MS`, faixa válida 4..2000, padrão 33.3 ms = 2 passos fixos de 60 Hz) nas seguintes fases:
     - `Input`: Coleta e processamento de eventos do SDL.
     - `Simulation`: Atualização da física, colmeia, trajetórias e colisões.
     - `Render`: Submissão de comandos de desenho à GPU.
     - `Present`: Apresentação na tela e sincronização de buffer.
     - `Pacing`: Espera de fim de quadro (limitador de software quando VSync está inativo).
   - O callback de síntese de áudio roda em thread própria do SDL e **não** é instrumentado (o atribuidor de hitches é exclusivo da render thread); a síntese concorrendo por CPU aparece na classificação de *culprit* como causa externa (`CPU-STARVED` / `EXTERNAL-WAIT`).
   - O log deve apontar explicitamente o subsistema causador do atraso (*culprit*) sem alocações dinâmicas no caminho crítico.
3. **Heartbeat Watchdog Não-Bloqueante:**
   - Subsistema leve que amostra o batimento cardíaco da simulação. Caso uma fase apresente atraso anormal ou congelamento iminente, o watchdog registra o delta exato em milissegundos e o estado operacional ativo.
4. **Comunicação Lock-Free & Sem Contenção:**
   - O laço principal nunca deve disputar mutexes pesados com threads de suporte. Comunicação entre threads feita exclusivamente via variáveis atômicas (`std::atomic`) e filas circulares lock-free (SPSC).

---

## ⚡ AGENTE 7: Engenheiro Anti-Stuttering, Frame Pacing & Eficiência de Hardware

### Objetivo
Garantir fluidez visual contínua (60 FPS estáveis), eliminar micro-congelamentos (*stuttering*) e minimizar a pegada de CPU e memória RAM em tempo de execução.

### Mandamentos Inegociáveis
1. **Acumulador de Física Delimitado (*Bounded Physics Accumulator*):**
   - O acumulador de física de passo fixo (60 Hz / ~16.66 ms) deve impor teto rígido de avanço (máximo de 2 passos de simulação por quadro renderizado), prevenindo a "espiral da morte" gerada por atrasos do escalonador do sistema operacional.
2. **Frame Pacing & Suavização Temporal:**
   - Sincronização homogênea entre simulação e exibição visual. Eliminar jitter de delta-time através de timestamps monotônicos de alta precisão (`SDL_GetTicksNS`).
   - Interpolação suave de renderização entre ticks de física quando necessário, evitando saltos visuais perceptíveis.
3. **Eficiência Rigorosa de CPU e RAM:**
   - Proibido consumo desnecessário de CPU por polling contínuo; sincronizar via VSync calibrado ou `SDL_DelayNS` adaptativo quando VSync estiver inativo.
   - Manter a utilização global de CPU e alocação de memória nos patamares mínimos do sistema.
4. **Documentação e Argumentos CLI Didáticos (Inglês):**
   - Manter os argumentos de linha de comando (`--help`, `--log`, `-log`, etc.) e manuais técnicos atualizados, autoexplicativos e integralmente redigidos em inglês técnico didático.

---

## 🎼 AGENTE 8: Maestro Acústico, Partituras Canônicas & Síntese Orquestral

### Objetivo
Transcrever, modularizar e reproduzir com fidelidade filológica as 15 obras clássicas do jogo em arranjos polifônicos ricos e contínuos para 10 instrumentos virtuais, erradicando e evitando sons enfadonhos ("bate-estaca") e viabilizando execução integral ou por recortes dinâmicos `[-start, -end]`.

### Mandamentos Inegociáveis (Encadeados do Geral para o Restrito)

#### 1. Arquitetura Modular & Compilação Concorrente (`src/musics/`)
- **Desacoplamento por Faixa:** Cada uma das 15 músicas reside estritamente em sua própria unidade de tradução: `src/musics/music_01.cc` até `src/musics/music_15.cc`.
- **Interface e Manifesto Comum:** Todas as faixas implementam a interface canônica definida em `src/musics/playlist.h` (exposição de `ScoreManifest`, `std::span<const NoteEvent>`, tempo original em BPM, compasso e limites temporais).
- **Paralelismo de Compilação Total (`make -j15`):** As unidades de tradução devem ser mutuamente independentes para compilar em paralelo através de múltiplos núcleos de CPU com compilação incremental ultra-rápida.
- **Zero Alocação Dinâmica na Síntese:** Os eventos de partitura (`NoteEvent`) residem em tabelas `constexpr` contíguas na seção `.rodata`. O sintetizador consome as notas via iteradores temporais sem `malloc`, `new` ou `std::vector::push_back`.

#### 2. Fidelidade Canônica & Fontes Primárias
- **Fontes Históricas Oficiais:** Toda partitura deve ser estritamente referenciada a edições críticas abalizadas (Bach-Gesellschaft Ausgabe, Breitkopf & Härtel, Durand, Fromont, etc.).
- **Partitura Integral:** A estrutura de dados contém a obra completa (do compasso 1 ao último compasso da partitura original), viabilizando playback integral sem perdas.
- **Velocidade e Métrica Autênticas:** O andamento (BPM) e a métrica de compasso originais da obra devem ser preservados rigorosamente sem acelerações ou desacelerações arbitrárias.
- **Proibição de Notas Espúrias:** Terminantemente vedada a inserção de notas ou floreios artificiais não presentes na partitura oficial.

#### 3. Orquestração Polifônica Completa (10 Instrumentos) & Erradicação do Bate-Estaca
- **Uso Integral dos 10 Instrumentos:** Todas as 15 composições devem orquestrar ativamente os 10 instrumentos virtuais disponíveis na engine acústica (Violinos I/II, Viola, Violoncelo, Contrabaixo, Madeiras, Trompete, Trompa, Tímpanos afinados e Órgão/Cravo).
- **Erradicação do Bate-Estaca Mecânico:** Terminantemente proibidas batidas de martelo monótonas, pulsos percussivos constantes e padrões rítmicos repetitivos que causem fadiga auditiva.
- **Riqueza Dinâmica e Contrapontística:** A sustentação acústica deve ser construída através de linhas contrapontísticas independentes (fuga, cânone, polifonia vocal), com dinâmicas expressivas de volume (pp a ff) mapeadas nos campos de velocidade de cada nota.
- **Eliminação de Longos Silêncios:** Não permitir vazios sonoros ou pausas prolongadas no miolo das faixas. As vozes devem dialogar de maneira ininterrupta e fluida.
- **Normalização Sonora:** Todas as musicas devem ter a mesma sensação de estar no mesmo volume.

#### 4. Motor de Playback, CLI (`-start`/`-end`) & Janelas Climáticas (30s)
- **Controle Flexível de Playback CLI:** A engine deve aceitar parâmetros de linha de comando para recorte arbitrário:
  ```bash
  ./aliens-invaders --play 2 -start 45.0 -end 75.0
  ./aliens-invaders --play 2 -start 0.0 -end 312.0 # Reprodução completa
  ```
- **Preset de 6 Segundos Climáticos:** Quando executada no fluxo padrão do jogo (fanfarras de debriefing, vitória de fase ou transição), a engine deve selecionar o trecho climático mais brilhante, famoso e impactante da obra, pré-configurado no manifesto da música (`peak_start_seconds`).
- **Envelopamento de Borda Impecável:** Para evitar cliques de offset contínuo (*DC clicks*) e cortes secos de amplitude, o motor aplica automaticamente um envelope suave de atenuação (fade-in inicial e fade-out final, conforme constants.h) estritamente sobre a janela de reprodução solicitada.

---

# 📋 Protocolo de Execução para LLMs (Guia de Geração de `fix.py`)

Ao implementar melhorias, diagnósticos ou correções:
1. **Analise o escopo:** Identifique a qual Agente a tarefa pertence e respeite suas fronteiras de domínio.
2. **Verifique impactos cruzados:** Se alterar dimensões na Super-Tabela (Agente 2), verifique impactos em constantes (Agente 5) e testes unitários. Se alterar estruturas de áudio (Agente 8), garanta conformidade com as garantias de zero alocação do Agente 4.
3. **Regra de Ouro do `fix.py` Minimalista (Delta Estrito):**
   - **Tamanho Justo e Preciso:** O script `fix.py` gerado deve conter **apenas o código estritamente necessário** para a alteração solicitada.
   - **Não repita o que já funciona:** Classes, funções e arquivos que compilam e passam nos testes e não possuem dependência com a mudança atual **não devem ser reapresentados**.
   - **Sem Backups:** Proibido criar pastas como `.fix-backup/` ou arquivos `.bak` no script. O Git (`git restore .`) é a única ferramenta necessária.
   - **Idempotência Obrigatória:** O script deve checar se a mudança já existe antes de aplicar (`[OK]` ou `[SKIP]`).
   - **Âncoras Multi-linha Byte-Exatas:** Quando aplicar patches, utilizar âncoras multi-linha exatas com verificação de ocorrência única.
   - **Zero Dependências Externas:** O script deve rodar apenas com módulos padrão do Python (`os`, `re`, `sys`, `pathlib`).
4. **Ciclo de Validação e Perfilamento em Tempo Real:**
   - Após aplicar o script no terminal:
     ```bash
     chmod +x ./fix.py
     ./fix.py
     make -j15 && make test
     ```
   - Diagnosticar o consumo real de CPU/GPU e verificar ausência de hitches:
     ```bash
     # Flight recorder de hitches: uma linha por quadro lento, com o veredito
     # do subsistema culpado (culprit) e resumo da sessão ao encerrar
     ./build/aliens-invaders -log

     # Telemetria embutida de CPU%/MHz: overlay in-game (tecla I ou B; gamepad East)
     ```
     *Ou alternativamente via ferramenta nativa do kernel:*
     ```bash
     perf top -p $(pgrep aliens-invaders)
     ```
