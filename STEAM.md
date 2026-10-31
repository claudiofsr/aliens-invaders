# STEAM.md — Roteiro de Lançamento na Steam (checklist + pipeline de depots)

> Documento de processo (mesma natureza do AGENTS.md). Placeholders de IDs:
> AppID `XXXXXXX`, depots `XXXXXXX1` (Linux), `XXXXXXX2` (Windows), `XXXXXXX3` (macOS).

## 0. Estado atual (pronto — não refazer)

- [x] CI verde em 5 plataformas a cada push (Linux GCC/Clang, macOS AppleClang, Windows MSVC, FreeBSD 14)
- [x] Release por tag: bundles auto-contidos (SDL3 + FluidR3 GM SoundFont MIT, atribuição no pacote)
- [x] Validação real: tarball Linux em máquina física; exe Windows sob wine (áudio + render)
- [x] MIT + notices de terceiros (SDL3 zlib, TinySoundFont MIT, stb MIT/PD, FluidR3 MIT © Frank Wen)
- [x] Privacidade: zero coleta de dados (scores locais com hash anti-fraude, sem rede) — LGPD trivial

## 1. Steam Direct (partner.steampowered.com)

- [ ] Conta no Steamworks + taxa Steam Direct de US$ 100 por título (reembolsável após US$ 1.000 de receita bruta ajustada)
- [ ] Dados fiscais: W-8BEN (pessoa física) ou entidade/CNPJ — **sem tratado EUA–Brasil**, esperar retenção americana sobre renda de fonte US; contador com experiência em royalties internacionais (carnê-leão/DIRPF ou Simples Nacional, CNAE 58.11-5-00 / 62.01-5-00)
- [ ] Conta bancária para recebimento em USD
- [ ] Criar o App "Aliens Invaders" → anotar o **AppID**
- [ ] A verificação bancária/fiscal leva dias: **iniciar já** (item de maior lead time do processo)

## 2. Disclosure de IA (obrigatório desde jan/2024)

- [ ] No formulário do App, seção *Generative AI*: declarar **Pre-Generated → Code**
      (desenvolvimento assistido por LLM sob o protocolo AGENTS.md)
- [ ] A declaração cobre o uso de ferramentas de IA na criação do conteúdo; a
      responsabilidade pelos direitos de tudo que é distribuído permanece do dev
- [ ] A store page pode exibir o aviso de disclosure aos jogadores — esperado e normal
- [ ] Não há conteúdo Live-Generated no jogo (nada de IA em runtime)

## 3. Marca (antes de publicar a página)

- [ ] Busca de anterioridade: **INPI** (BR), **USPTO** (US), **EUIPO** — "Aliens Invaders" nas classes 9 (software/jogos) e 41 (entretenimento)
- [ ] Auditoria visual dos 15 aliens contra o xenomorfo de H.R. Giger (franquia ALIEN, Disney/Fox): qualquer semelhança → redesenhar antes da store page
- [ ] "Space Invaders" é marca da Taito: clonar **mecânica** é legal (gameplay não é protegível por copyright), mas nunca usar o termo em loja/marketing
- [ ] (Recomendado) Depósito no INPI: marca nominativa + figurativa (~R$ 355/classe);
      registro US via TEAS opcional (US$ 250–350/classe) — depositar **antes** do lançamento protege contra squatters

## 4. Store page (assets e textos)

- [ ] Cápsulas (dimensões vigentes — confirmar no Steamworks ao criar): header 460×216, main 616×353, small 231×87, vertical 374×448, library 600×900, background 1438×810
- [ ] ≥ 5 screenshots 1920×1080 de gameplay real: honeycomb em voo, salto relativístico do Alien 14, mergulho kamikaze, debriefing, jukebox clássica
- [ ] Trailer 1080p, 60–90 s (hook 10 s → mecânicas → orquestra → clímax com nuke)
- [ ] Descrição curta (~150 chars) + longa; **tags**: Arcade, Space Shooter, Retro, Bullet Hell, Classical Music, Indie, Difficult, Singleplayer
- [ ] Questionário IARC: violência cartoon → faixa etária baixa
- [ ] Idiomas: English (UI); pt-BR como localização futura
- [ ] Regra das 2 semanas: página *coming soon* pública ≥ 14 dias antes do lançamento (acumular wishlists); submeter à review com 2–5 dias de folga

## 5. Depots steamcmd (usando exatamente os bundles da release)

### 5.1 Setup
```bash
sudo pacman -S steamcmd                       # Arch/Manjaro (repo multilib)
steamcmd +login SEU_USUARIO +quit             # 1ª vez: código Steam Guard
```

### 5.2 Layout de staging
```bash
mkdir -p steam-build/{scripts,output} steam-build/content/{linux,windows,macos}
gh release download v0.10.0 --dir release-test/ --clobber
tar xf release-test/aliens-invaders-linux-x86_64.tar.xz -C steam-build/content/linux --strip-components=1
unzip -q release-test/aliens-invaders-windows-x64.zip -d steam-build/content/windows-tmp
mv steam-build/content/windows-tmp/aliens-invaders-windows-x64/* steam-build/content/windows/
unzip -q release-test/aliens-invaders-macos-arm64.zip -d steam-build/content/macos-tmp
mv steam-build/content/macos-tmp/aliens-invaders-macos-arm64/* steam-build/content/macos/
```

### 5.3 `scripts/app_build_XXXXXXX.vdf`
```
"AppBuild"
{
	"AppID" "XXXXXXX"
	"Desc" "v0.10.0 - five-platform CI verified, SoundFont bundled"
	"BuildOutput" "../output/"
	"ContentRoot" "../content/"
	"Depots"
	{
		"XXXXXXX1" "depot_linux.vdf"
		"XXXXXXX2" "depot_windows.vdf"
		"XXXXXXX3" "depot_macos.vdf"
	}
}
```

### 5.4 `scripts/depot_linux.vdf` (windows/macos: idêntico, trocando DepotID e ContentRoot)
```
"DepotBuildConfig"
{
	"DepotID" "XXXXXXX1"
	"ContentRoot" "linux/"
	"FileMapping"
	{
		"LocalPath" "*"
		"DepotPath" "."
		"Recursive" "1"
	}
}
```

### 5.5 Upload
```bash
steamcmd +login SEU_USUARIO \
  +run_app_build /caminho/absoluto/steam-build/scripts/app_build_XXXXXXX.vdf \
  +quit
```

### 5.6 Steamworks (configuração do App)
- [ ] Criar os 3 depots com as plataformas corretas (Linux / Windows / macOS)
- [ ] Launch options por SO: `aliens-invaders` · `aliens-invaders.exe` · `aliens-invaders`
- [ ] Gerar *testing key* e instalar o AppID num cliente Steam real — jogar nos 3 SO
      (o CWD do Steam = pasta do jogo → `assets/soundfonts/` do bundle é encontrado)

### 5.7 Específico macOS para depot
- [ ] Apple Developer (US$ 99/ano): desde Catalina o macOS exige **notarização** — a Valve espera build notarizado no depot
- [ ] Evoluir o job `package-macos` (fix futuro): `.app` bundle (Info.plist + ícone .icns) → `xcrun notarytool submit` → `xcrun stapler staple` → zip
- [ ] O zip ad-hoc atual serve para distribuição direta/testes, **não** para o depot macOS

## 6. Runbook pré-lançamento (ordem)

1. [ ] Steam Direct completo; AppID anotado neste documento
2. [ ] Depots subidos + testados via cliente Steam (3 SO)
3. [ ] Store page draft completa (assets, disclosure de IA, IARC) → submeter review
4. [ ] Busca de marca concluída; depósito INPI protocolado (se optado)
5. [ ] Página *coming soon* pública ≥ 2 semanas antes do lançamento
6. [ ] Demo registrada (AppID separado); avaliar janela do Steam Next Fest
7. [ ] Pricing: base USD (a Valve sugere preços regionais, BR inclusive); faixa arcade premium US$ 4,99–9,99 com 10–15% de desconto de lançamento
8. [ ] Dia D: mudar branch dos depots para `default`, publicar página, anunciar
9. [ ] Pós-lançamento: reviews e relatórios de crash no Steamworks (o jogo não envia
       telemetria — por design); patches fluem como `git tag v0.10.x` → release → `run_app_build`

## 7. Pós-lançamento opcional (Steamworks SDK)

- [ ] Achievements: casam com o plano de retenção (waves sem dano, kills em warp, nukes virgens, medalhas de par-time)
- [ ] Steam Cloud (Auto-Cloud) para o ledger de scores — caminho por SO:
      `~/.local/share/aliens-invaders/` (Linux) · `%APPDATA%\aliens-invaders\` (Windows) ·
      `~/Library/Application Support/aliens-invaders/` (macOS); arquivo pequeno, hash anti-fraude já embarcado
- [ ] Steam Input: gamepad SDL já funciona; adicionar configuration file para glyphs
- [ ] Rich Presence: "Wave 12 • Stage 1 • 26,330 pts"

## Apêndice A — mapeamento bundles v0.10.0 → depots

| Asset da release | Depot | Conteúdo |
|---|---|---|
| `aliens-invaders-linux-x86_64.tar.xz` | `XXXXXXX1` | binário + `lib/libSDL3.so.0.4.16` (rpath `$ORIGIN/lib`) + `assets/soundfonts/` |
| `aliens-invaders-windows-x64.zip` | `XXXXXXX2` | exe + `SDL3.dll` (applocal vcpkg) + `assets/soundfonts/` |
| `aliens-invaders-macos-arm64.zip` | `XXXXXXX3` | binário + dylib `@executable_path` + `assets/soundfonts/` — **pendente .app + notarização (§5.7)** |

## Apêndice B — decisões fiscais/jurídicas em aberto

- [ ] Pessoa física (CPF + W-8BEN) vs. CNPJ (Simples Nacional) para receber da Valve
- [ ] Contador para retenção US (sem tratado) + conversão USD/BRL + NF
- [ ] Depósito de marca no INPI: antes do lançamento (prioridade) — recomendado
