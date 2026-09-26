# 🤖 SISTEMA DE AGENTES: ALIENS INVADERS (C++20 / SDL3)

```
                          ┌────────────────────────────────────────┐
                          │         AGENTE 1: ORQUESTRADOR         │
                          │       Clean Architecture & Design      │
                          └───────────────────┬────────────────────┘
                                              │
    ┌────────────────────┬────────────────────┼────────────────────┬────────────────────┐
    ▼                    ▼                    ▼                    ▼                    ▼
┌──────────────┐  ┌──────────────┐    ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│   AGENTE 2   │  │   AGENTE 3   │    │   AGENTE 4   │     │   AGENTE 5   │     │   AGENTE 6   │
│ Super-Tabela │  │   Entidades  │    │ Performance  │     │  Deep DRY &  │     │ Concorrência │
│  Honeycomb   │  │   Especiais  │    │ & Compile-Time│    │ QA / Testes  │     │ & Profiling  │
└──────────────┘  └──────────────┘    └──────────────┘     └──────────────┘     └──────────────┘
```

---

## 🏛️ AGENTE 1: Orquestrador de Arquitetura & Código Limpo (Clean Architecture)

### Objetivo
Garantir código direto, modular, sem camadas desnecessárias de abstração, eliminando arquivos e classes obsoletas.

### Mandamentos Inegociáveis
1. **Zero Aliases / Código Direto:**
   - Proibido manter wrappers ou funções de compatibilidade retroativa (ex.: nada de `HSpacing()` redirecionando para `SpacingX()`). Refatore os chamadores diretamente na fonte.
2. **Simetria Rigorosa de Nomes:**
   - Eixos X e Y devem ter nomes estritamente paralelos:
     - `SpacingX()` e `SpacingY()`.
     - `offset_x` e `offset_y` (em `UnitCoord` e `FormationSlot`).
     - `base_cruise_.x` e `base_cruise_.y`.
3. **Eliminação de Código Morto:**
   - Remover scripts legados ou temporários (ex.: `fix-gemini.py`, `.bak`, `.tmp`).
   - Remover métodos não utilizados de iterações antigas (ex.: métodos legados de Life e Nuke duplicados em `score.h`).
4. **Isolamento de Subsistemas:**
   - O menu principal deve abrir sem inicializar subsistemas pesados de hardware (o áudio só é iniciado sob demanda com a tecla `S` / `Start`).

---

## 🐝 AGENTE 2: Especialista em Cinemática & Super-Tabela Honeycomb

### Objetivo
Gerenciar a formação da frota inimiga em colmeia (hive/honeycomb) pré-calculada, eliminando micro-teleportes e garantindo trajetórias suaves.

### Mandamentos Inegociáveis
1. **Super-Tabela Estática e Pré-Calculada:**
   - Dimensões fixas: `kMaxGridRows = 12`, `kMaxGridCols = 16`, `kNumStageModes = 3`.
   - Gerada em tempo de compilação (`constexpr`) em coordenadas unitárias normalizadas (`offset_x`, `offset_y`) e escalada uma única vez quando a resolução da janela for alterada.
2. **Cálculo Simétrico de Cruzeiro:**
   - Em `Trajectory::CruiseTarget()`, as coordenadas devem ser 100% simétricas:
     - `alvo.x = base_cruise_.x + slot.offset_x`
     - `alvo.y = base_cruise_.y + slot.offset_y`
   - No eixo Y, `base_cruise_.y` recebe o horizonte estático `BaseCruiseY() = 82 px`.
   - No eixo X, `base_cruise_.x` oscila dinamicamente entre `min_cruise_x_` e `max_cruise_x_`.
3. **Salto Relativístico do Alien 14 (Albert Alienstein):**
Regras de Ativação (preservadas): O Alien14 com poder de salto (probabilidade kAlien14WarpEvasionProbability de possuir o poder de salto) sempre inicia exatamente com 2 saltos (kAlien14NumWarpEvasions). A qualquer momento de sua trajetoria, caso a bala penetre seu raio de proximidade(kAlien14WarpTriggerRadiusFactor), realiza o salto, com reduçao (-1) de kAlien14NumWarpEvasions. Como o Alien14 salta para outro favo da colmeia, a bala nao o atinge e ele nao é anaquilado neste momento. O Alien 14 é aniquilado apenas se atingido pela bala desde que kAlien14NumWarpEvasions seja Zero.
   - Prioridade 1 (Sorteio restrito único por salto): No gatilho de cada salto, o RandomStream do gameplay (seed determinístico) sorteia uma única vez um favo (col, row) da super-tabela ou colmeia. 
   Domínio do sorteio = favos dentro das margens da tela e fora da zona de exclusão ao redor da posição atual do alien14 e da nave terráquia.
   - Prioridade 2 (Zona de exclusão = vizinhança de N favos): Zona de exclusão = todos os favos a distância < N favos do favo atual & todos os favos a distância < N favos da nave terráquia, com N = kAlien14WarpMinCellSeparation. 
   Métrica: Chebyshev max(|Δcol|, |Δrow|) — inteira, sem sqrt. (Como a métrica é Chebyshev, a zona de exclusão é um quadrado de lado 2N−1, o que permite o sorteio restrito O(1) da Prioridade 3.)
   - Prioridade 3 (Distância mínima por construção): Como o domínio já exclui a vizinhança, o destino está sempre a ≥ N favos de onde o alien saltou. Garantia estrutural — sem correção pós-sorteio para a distância e sem loop de rejeição. Mecanismo O(1): sortear um único índice no conjunto válido (retângulo de margens − quadrado de exclusão) e mapeá-lo ao favo por decomposição em faixas (acima / laterais / abaixo do quadrado), em aritmética pura, sem laço.
   - Prioridade 4 (Verificação de ocupação — livre/ocupado): No gatilho, monta-se o mapa de ocupação da armada (favo de cada alien), excluindo o próprio alien14 que está saltando. Livre → o alien14 ocupa o favo sorteado. Ocupado → avança para o próximo favo vizinho desocupado, na direção horizontal que aumenta a distância da nave terráquia (afasta-se do jogador: col+1 se o destino está à direita do jogador, col−1 caso contrário). Varredura determinística limitada (≤ largura da linha), considerando apenas favos fora da zona de exclusão (preserva a Prioridade 3).
   - Prioridade 5 (Mudança permanente de favo): O favo destino torna-se a nova estação do alien14 (grid_col_/grid_row_ são atualizados); ele acompanha a varredura da colmeia junto com a formação. Não existe regra automática de retorno. O 2º salto aplica exatamente as mesmas regras (P1–P4) tomando esse novo favo como referência. Como a posição original fica fora da zona de exclusão do 2º sorteio, o retorno à estação inicial pode ou não ocorrer, dependendo exclusivamente do sorteio aleatório (P1), podendo o sorteio, por acaso, reconduzir o alien à estação original — um evento possível, jamais determinístico.
   - Prioridade 6 (Contenção de margem — passo único): O destino final é contido dentro das margens da tela por um único clamp. Sem loops.
   - Prioridade 7 (Cache único do destino): O destino é sorteado uma única vez no gatilho e cacheado; o laço de movimento por frame consome apenas o cache. Nunca re-sortear por frame (evita jitter).
   - Prioridade 8 (Determinismo absoluto): Somente RandomStream seedado; nunca std::random_device.
Caso degenerado (fallback): Se a varredura de ocupação não encontrar favo livre na colmeia, saltar para um dos dois cantos superiores da tela (escolher o canto mais distante da nave terráquia), sempre dentro dos limites da tela.
4. **Erradicação de Micro-Teleportes:**
   - No estado `cruising`, a transição entre posições deve ser uma **perseguição contínua** com velocidade delimitada, nunca atribuindo `target.x/y` instantaneamente se a distância for maior que a velocidade do quadro.
   - A guarda de interpolação (delta jump) deve verificar saltos nos eixos X e Y contra `previous_position`, ignorando saltos intencionais de dobra (`is_warping_`).

---

## ⚡ AGENTE 3: Especialista em Entidades & Regras Especiais de Combate

### Objetivo
Manter o comportamento determinístico e justo dos inimigos, bônus e projéteis com zero alocação de memória no loop principal.

### Mandamentos Inegociáveis
1. **Alien 4 (Madame Curie) & Decaimento Alfa:**
   - Sorteada probabilisticamente no nascimento para possuir ou não uma eletrosfera (`kAlien4ElectrosphereProbability = 0.10f`).
   - **Com eletrosfera (2 impactos):** O primeiro tiro causa Alpha Decay:
     - Encolhe para 50% de tamanho (`kAlien4AlphaDecayScale = 0.50f`).
     - Dobra a velocidade orbital dos elétrons (`kAlien4DecayedOrbitSpeedMult = 2.0f`).
     - Acelera a velocidade de voo da nave em +30% (`kAlien4DecayedSpeedMult = 1.30f`).
     - Gera partículas e aura cintilante; apenas o segundo tiro é fatal.
   - **Sem eletrosfera (1 impacto):** Neutralizada em um único tiro convencional.
2. **Inimigos Kamikaze (Aura Pulsante Vermelha):**
   - Alertam visualmente pulsando em vermelho e emitindo sirene antes de mergulhar.
   - Raio de destruição por proximidade é letal (1.4× a largura da nave).
3. **Mísseis Teleguiados & Fair-Play:**
   - Projéteis inimigos com deflexão vetorial de até ±15°.
   - **Corredor de Evasão Obrigatório:** O jogo nunca cria armadilhas em tesoura sem saída; sempre garante um espaço seguro de no mínimo `kSafeEvasionCorridorPixels = 118 px` para esquiva do jogador.
4. **Economia de Bônus (Supply Pods):**
   - No máximo 5 bônus por estágio de 15 fases (Speed, Fire, Multi, Shield, Nuke).
   - Zero drops duplicados do mesmo tipo dentro do mesmo estágio.

---

## 🚀 AGENTE 4: Otimização de Performance, Memória & Tempo de Compilação

### Objetivo
Minimizar o consumo de CPU e RAM, movendo cálculos dinâmicos para `constexpr` e `.rodata`.

### Mandamentos Inegociáveis
1. **Zero Alocação Dinâmica no Loop Quente:**
   - É terminantemente proibido usar `new`, `delete`, `malloc`, `std::vector::push_back` (com redimensionamento dinâmico) ou alocações de `std::string` no loop de simulação/renderização (60 Hz).
   - Usar arrays estáticos contíguos (`std::array`) com listas densas de índices ativos (Swap-and-Pop O(1)).
2. **Tabelas Pré-Calculadas em Tempo de Compilação:**
   - Trigonometria de anéis de órbita, rotações de giro (spin), decaimento de explosão e posições da colmeia devem residir na seção `.rodata` via funções `constexpr` imediatas.
3. **Render Targets em Texturas Compartilhadas (GPU):**
   - Menus estáticos, debriefing e telas de pausa devem ser renderizados **uma única vez** para uma textura de GPU (`SDL_Texture` de acesso target) e reapresentados em um único blit (reduzindo milhares de chamadas de desenho a uma só e derrubando a CPU para menos de 0.35%).
4. **Concorrência Segura (C++20):**
   - Threads assíncronas (áudio, persistência de scores e telemetria) devem utilizar `std::jthread` com interrupção cooperativa via `std::stop_token` (sem busy-waits e sem deadlocks).

---

## 🔍 AGENTE 5: Inspetor de Qualidade, Deep DRY & Auditoria de Testes

### Objetivo
Eliminar duplicidades em `constants.h`, manter métricas centralizadas e garantir que todos os testes unitários passem sem regressão.

### Mandamentos Inegociáveis
1. **Fonte Única da Verdade (SSOT) em `constants.h`:**
   - Cada constante existe em um único lugar. Proibido espalhar números mágicos pelo código (ex.: pisos de velocidade como `1.8f`, `1.5f`, `0.90f` pertencem a `GameRules::Fleet`; frações como `65 / 100` pertencem a `kCollisionHitboxScale`).
   - Não inventar constantes artificiais para eixos dinâmicos (ex.: não existe `kFleetBaseCruiseXPixels`, pois o cruzeiro em X é centralizado e oscilado dinamicamente).
2. **Validação do Test Runner (40 Testes):**
   - Qualquer modificação estrutural deve ser validada contra a suíte de testes (`tests/unit_tests.cc`).
   - Os testes devem aferir nomes simétricos (`SpacingX`, `SpacingY`, `BaseCruiseY`) e propriedades físicas reais.
3. **Preservação Sagrada do Áudio:**
   - Proibido alterar síntese procedural, durações de notas, timbres ou fanfarras sinfônicas sem solicitação expressa.

---

## 🧵 AGENTE 6: Especialista em Concorrência, Anti-Stuttering & Engenheiro de Desempenho

### Papel (Role)
Responsável pela segurança em multithreading, erradicação de congelamentos (stuttering), perfilamento em tempo real no Linux (via `perf`) e pela geração de scripts `fix.py` estritamente minimalistas e incrementais.

---

### Mandamentos Inegociáveis

1. **Concorrência Segura em C++20 (`std::jthread` & `std::stop_token`):**
   - Toda rotina assíncrona paralela (áudio em segundo plano, persistência de disco, amostragem de telemetria) deve obrigatoriamente usar **`std::jthread`** com cancelamento cooperativo via **`std::stop_token`**.
   - Proibido uso de raw threads (`std::thread`), busy-waits travando CPU ou joins bloqueantes no laço de renderização.

2. **Prevenção Ativa Contra Stuttering & Race Conditions:**
   - O laço principal nunca deve disputar mutexes pesados com a renderização.
   - Comunicação entre threads feita preferencialmente via buffers circulares lock-free atômicos (`std::atomic`).
   - O acumulador de física de 60 Hz deve manter teto rígido (bounded accumulator, máx. 2 passos) para evitar a "espiral da morte" por atraso de escalonador.

3. **Documentação Didática no Código (em Inglês):**
   - Todo código inserido ou ajustado deve conter comentários didáticos, elegantes e explicativos em **inglês**, detalhando o motivo da escolha técnica (why, não apenas what), aderindo às melhores práticas do C++20.

4. **Regra de Ouro do `fix.py` Minimalista (Delta Estrito):**
   - **Tamanho Justo e Preciso:** O script `fix.py` gerado deve conter **apenas o código estritamente necessário** para a alteração solicitada.
   - **Não repita o que já funciona:** Classes, funções e arquivos que já compilaram perfeitamente após `make -j15` e não possuem dependência com a mudança atual **não devem ser reapresentados**.
   - **Sem Backups:** Proibido criar pastas como `.fix-backup/` ou arquivos `.bak` no script. O versionamento do Git (`git restore .`) é a única ferramenta de restauração necessária.
   - **Idempotência:** O script deve checar se a mudança já existe antes de aplicar (`[OK]` ou `[SKIP]`).

5. **Ciclo de Validação e Perfilamento em Tempo Real:**
   - Após rodar o script no terminal:
     ```bash
     chmod +x ./fix.py
     ./fix.py
     make -j15 && make test
     ```
   - Diagnosticar o consumo real de CPU e GPU rodando o binário e inspecionando pontos quentes (hotspots):
     ```bash
     # Terminal 1: Inicia o monitor de telemetria dedicado
     ./perf-monitor.py

     # Terminal 2: Executa o jogo para captura de métricas
     ./build/aliens-invaders
     ```
     *Ou alternativamente via ferramenta nativa do kernel:*
     ```bash
     perf top -p $(pgrep aliens-invaders)
     ```

---

# 📋 Protocolo de Execução para LLMs (Guia de Geração de `fix.py`)

Ao ser solicitado a implementar melhorias ou correções:
1. **Analise o escopo:** Identifique a qual Agente a tarefa pertence.
2. **Verifique impactos cruzados:** Se alterar um nome na Super-Tabela (Agente 2), verifique onde o Agente 1 (Arquitetura) e o Agente 5 (Testes) são impactados.
3. **Produza um `fix.py` cirúrgico:**
   - Não utilize busca de strings frágeis se houver risco de quebra; utilize substituições exatas ou regravação limpa dos arquivos-chave.
   - O script deve ser idempotente (se executado duas vezes seguidas, não quebra o código).
   - O script deve executar sem dependências externas além do Python padrão (`os`, `re`, `sys`).

---

## 🧵 AGENTE 7: Evitar stattering e congelamentos.

Analisar com minucia todo o projeto do game aliens-invaders. 

Propor melhorias para aumento de eficiencia: menor uso de CPU e memoria RAM. 
Evitar a todo custo stattering e congelamentos ao executar o game.

------------------------------------------------

