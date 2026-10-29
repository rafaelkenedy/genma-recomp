# Issue #8 — plano do port Linux e Steam Deck

Data: 28/10/2026. Base examinada: `a844f89`, v0.5.2.
Issue: https://github.com/rafaelkenedy/genma-recomp/issues/8
Estado: ferramentas, testes e diagnóstico de janela implementados; runtime Linux/Vulkan experimental já inicia o jogo e mostra a tela de título. Gameplay, paridade gráfica e distribuição continuam pendentes.

Separação de CI e publicação Windows/Linux: [ci-release-multiplatform-plan.md](ci-release-multiplatform-plan.md).

## Entrega pretendida

Executável Linux x86-64 nativo, com uma distribuição inicial em pasta e posterior AppImage, utilizável no desktop Linux e no Steam Deck. A primeira execução recebe a ISO/XBE do jogador, recompila e compila localmente; as seguintes abrem o jogo. O pacote não inclui assets, XBE, código gerado do jogo ou saves.

Preservar o comportamento da v0.5.2: controles modernos e cancelamento de ações, D-pad, correção das flags do acos, áudio 3D, aspecto 4:3, texturas HD opcionais, sombras e temporização dos FMVs. O experimento local de acosf não faz parte do port.

Proton permanece como alternativa já relatada na #23, com instalação de d3dcompiler_47 pelo Protontricks. Esse relato não valida um port nativo nem encerra a #8. Não transformar a solução temporária em dependência do novo pacote.

## O que o código permite reutilizar

| Área | Estado observado | Trabalho planejado |
| --- | --- | --- |
| Recompilação x86 → C++ | Ferramentas C++ com Zydis e toml++; código gerado reutilizável | Compilar as ferramentas em Linux e verificar símbolos, weak overrides, ABI e exceções |
| Memória | `kernel/memory.cpp` já tem mmap/memfd e aliases de memória física | Compilar e testar reserva de 4 GiB virtuais, aliases, commit/decommit e limpeza em falhas; não confundir reserva virtual com RAM consumida |
| Threads | `kernel/threads.cpp` já tem pthreads | Validar pilhas grandes, TLS, sincronização, encerramento e temporização |
| Fibers | `xdk/fiber.cpp` depende de Windows fibers quando os símbolos estão habilitados | Auditar símbolos/caminhos realmente usados por Genma; implementar suporte portátil se necessário, sem stubs silenciosos |
| Controles | SDL3 já recebe gamepads | Compartilhar inicialização/eventos com a janela SDL; testar controle virtual Steam Input e hotplug |
| Janela/menus | Win32, HWND e ImGui Win32/DX12 | Host SDL3 Linux, backend de janela ImGui SDL3, diálogo de arquivos e menu utilizável no Deck |
| Gráficos | `gpu/backend.h` é uma interface, mas só há D3D12; `gpu/ext.h` também expõe D3D12 | Novo backend e separação das extensões dependentes da API gráfica |
| Shaders | Geração HLSL reaproveitável; compilação atual usa D3DCompile | Validar HLSL → SPIR-V com DXC; converter bindings e interfaces explicitamente |
| Áudio | Mixer fornece PCM float estéreo/48 kHz por `audio::StartOutput`; saída é XAudio2 | Implementar saída SDL3 Audio, mantendo o mixer e a atenuação existentes |
| Arquivos e imagens | Leitura de disco tem partes portáveis; seleção de arquivos, WIC e caminhos dependem de Windows | Auditar extração/ISO, maiúsculas/minúsculas, UTF-8, PNG/texturas, diretórios graváveis e diagnóstico |
| Launcher/distribuição | Win32, LLVM-MinGW, bibliotecas Windows e updater PowerShell | Implementações Linux e seleção de assets por plataforma; não reaproveitar o pacote Windows |

Arquivos centrais: `sdk/cmake/XbeRecomp.cmake`, `sdk/cmake/XbePackage.cmake`, `sdk/runtime/app/`, `sdk/runtime/gpu/d3d12.cpp`, `sdk/runtime/gpu/ext.h`, `sdk/runtime/audio/xaudio2.cpp`, `sdk/runtime/xdk/xnet.cpp` e `sdk/launcher/`. Os diagnósticos Win32 em `src/game/difftest.cpp` precisam ser separados do build normal Linux ou ganhar implementação portátil.

## Decisões técnicas

1. **Alvo inicial Linux x86-64.** Não incluir Android/ARM nesta issue. Manter a lógica recompilada e os fixes do jogo comuns entre plataformas.
2. **SDL3 para janela, entrada e saída de áudio.** O áudio já chega misturado como PCM: não é necessário migrar toda a lógica DirectSound para outra implementação de XAudio2.
3. **Prova gráfica antes do port completo.** Primeira candidata: SDL GPU com Vulkan e shaders gerados pelo nosso tradutor, compilados com DXC/SDL_shadercross. Só adotá-la após provar os recursos necessários. Se ela impedir equivalência de stencil, sincronização/readback ou recursos de textura, usar Vulkan diretamente atrás de `gpu::Backend`. A escolha final é a primeira entrega técnica, não uma suposição de compatibilidade.
4. **Windows continua com D3D12 durante a migração.** Selecionar fontes e bibliotecas por plataforma em CMake; não exigir a reescrita do backend Windows para entregar Linux.
5. **Toolchain Linux fixado e ABI consistente.** Runtime e código recompilado usam compilador, biblioteca C++ e flags compatíveis, ligados em um único executável. Definir a base glibc/Steam Runtime suportada após a prova de distribuição; não compilar na distro mais recente e presumir compatibilidade com SteamOS.
6. **Código e dados graváveis fora do AppImage.** Saves/configuração em diretórios XDG apropriados, código gerado/objetos/cache fora do ponto montado, ISO apenas lida. Disponibilizar importação explícita dos saves Windows e tratar cartões SD/caminhos com espaços.
7. **A issue permanece aberta até a entrega jogável.** Os PRs da base fazem referência à #8 e não a encerram automaticamente.

## Etapas e critérios de saída

### 1. Build Linux e base do runtime

- Criar presets de Linux e separar fontes comuns/Windows/Linux, incluindo backends ImGui e bibliotecas do launcher.
- Compilar `xbe_recomp` e `xbe_symbols` sem GUI e sem exigir assets no CI.
- Auditar portabilidade de intrinsics, exceções/continuações, weak symbols, `_wfopen`, strings largas, rede, diagnóstico e encerramento de processo.
- Exercitar memória física aliasada, arquivos/saves com diferenças de caixa e threads; auditar fibers habilitadas em `image_symbols.h`.
- Executar os testes de controles e cancelamento de ações em Linux; manter os testes Windows existentes.

Saída: ferramentas e núcleo compilando no Ubuntu, testes de memória/dispatch passando e um executável de teste Linux sem dependências Win32 inesperadas. Falhas de compilação são inventariadas, não mascaradas com stubs.

### 2. Prova gráfica com decisão de arquitetura

- Janela SDL com apresentação Vulkan e um quadro mínimo de UI/fundo/personagem.
- Compilar os shaders gerados pelo runtime, não apenas um triângulo de exemplo. Conferir matrizes, constantes, coordenadas, convenções de profundidade, formato dos vértices e roundings.
- Provar depth/stencil, sombra por stencil, alpha test/blend, viewport/scissor, filtros, pontos e primitivas expandidas.
- Provar cópias entre render targets, render-to-texture, amostragem de profundidade, feedback, CPU readback, fences e consultas de visibilidade realmente usadas pelo jogo.
- Auditar as interfaces de `gpu/ext.h`; não deixar funções exigidas retornando sucesso sem executar o trabalho.

Saída: comparação reproduzível contra capturas D3D12 e decisão registrada entre SDL GPU e Vulkan direto, com dependências fixadas e diferenças conhecidas. Um backend que mostra apenas o menu não satisfaz esta etapa.

### 3. Jogo executável de desenvolvimento

- Integrar boot/loop de eventos SDL e o backend escolhido, com controle claro de qual thread processa eventos e renderiza.
- Implementar saída SDL Audio, suspensão/retomada da saída, tamanho dos buffers e logs de underrun; preservar o mixer.
- Completar seleção de ISO/XBE, leitura de arquivos, caminhos graváveis, texturas/fontes e alternativas às APIs Windows ainda usadas.
- Integrar ImGui/F1, resolução, tela cheia, encerramento e controle Steam Input. Testar separadamente o polling do jogo e a navegação do menu.
- Validar FMVs com áudio sincronizado e encerramento/skip, carga de fases, save/load e reconexão de controle.

Saída: título e gameplay no Linux com imagem, áudio e controles, mais uma rota de teste por caverna e ROOM100 e carregamento de save. Uma build experimental para desenvolvedores pode usar assets extraídos fornecidos localmente.

### 4. Distribuição Linux sem assets

- Portar launcher e execução de subprocessos para Linux; primeiro uma CLI utilizável para diagnóstico, depois interface adequada para desktop/Deck.
- Empacotar compilador/linker/sysroot Linux redistribuíveis e o runtime estático; não presumir clang ou headers instalados no SteamOS.
- Recompilar/compilar a partir da cópia do jogador em pasta limpa, com progresso, cancelamento e logs. Limitar paralelismo por recursos disponíveis e medir pico de RAM, tempo e espaço livre.
- Gerar primeiro `linux-x86_64.tar.gz`; só então montar AppImage com o mesmo conteúdo e diretórios XDG externos. Testar extração/execução alternativa onde FUSE não estiver disponível.
- Separar assets de atualização Windows/Linux. A alpha pode declarar atualização manual; antes de habilitar auto-update Linux, implementar confirmação, verificação de hash/tamanho, escrita temporária, troca atômica, permissões executáveis, cancelamento e recuperação. Não executar `update.ps1` no Linux.
- Testar preservação de saves/configuração/texturas e invalidar apenas o cache de compilação quando a versão/ABI exigir.

Saída: máquina limpa abre o pacote, seleciona uma ISO externa, compila e joga sem Wine, Proton, pacotes de desenvolvimento ou escrita dentro do AppImage. O conteúdo publicado continua sem código/assets do jogo.

### 5. Validação no Steam Deck e release

- Executar no SteamOS real em Desktop Mode e Gaming Mode, com 1280×800 e conteúdo 4:3 corretamente apresentado. Verificar controles integrados, Steam Input, menu, teclado virtual quando necessário e retorno ao jogo após perder foco.
- Testar sleep/resume, áudio, acesso à ISO no SD, save/load e atualização manual/automática conforme o estágio anunciado.
- Medir frametimes, FPS, CPU, RAM, VRAM e tempo da primeira compilação; comparar o mesmo trecho com Windows/Proton no mesmo Deck, sem prometer FPS/bateria antes da medição.
- Capturas de regressão: fundos, personagens/skinning, sombra stencil, transparências, partículas, texto, texturas HD ligadas/desligadas e FMVs.
- Sessões de idle, corrida, combate, absorção/cancelamento e mudanças de câmera; testar Kaede com save adequado. Guardar configuração, versões de driver/build e roteiro junto aos resultados.
- CI Linux/Windows: testes de núcleo, ferramentas, geração do pacote e inspeção de dependências. Validação gráfica por software é complementar; não substitui o Deck.

Saída: pacote Linux nativo validado no Deck e em desktop Linux, com instruções e limitações conhecidas. Só então considerar a #8 atendida; uma AppImage que apenas envolve Wine não atende ao objetivo definido aqui.

## Divisão sugerida em PRs

| PR | Entrega revisável |
| --- | --- |
| 1 | CMake por plataforma, ferramentas Linux e testes do núcleo |
| 2 | Host SDL, saída de áudio e demais adaptações de sistema |
| 3 | Prova gráfica e decisão de backend, sem prometer port completo |
| 4+ | Backend escolhido em fatias: draw/shaders; depth/stencil; cópias/sync; paridade visual e ImGui |
| Seguinte | Launcher/toolchain Linux, primeiro pacote tar e instalação limpa |
| Seguinte | AppImage, integração/validação Steam Deck e documentação de release |
| Separado | Auto-update Linux, se ainda não concluído durante o empacotamento |

O trabalho gráfico é o maior risco e pode exigir vários PRs. Não fixar uma data de entrega antes de concluir a prova gráfica. Revisões de base não devem retirar correções Windows ou incluir os experimentos locais ignorados/não rastreados.

## Ambiente e limites desta análise

Há `Ubuntu-22.04` e `docker-desktop` registrados no WSL desta máquina. A primeira entrega foi compilada e testada no Ubuntu 22.04.5 com Clang 17.0.0 e CMake 3.31.6 portátil, baixado de Kitware e conferido pelo manifesto SHA-256 oficial, dentro de `out/tooling`. O comando `/usr/local/bin/cmake` já existente era um script diferente e não foi modificado. Não foi instalada ferramenta global.

WSL ajuda no build/testes, mas não comprova compatibilidade com Gamescope, drivers AMD do Deck, hardware de entrada ou sleep/resume. A validação final depende de acesso a um Steam Deck real ou resultados reproduzíveis de um testador.

### Primeira entrega: resultados

- `XBE_BUILD_RUNTIME=OFF` separa ferramentas de SDL/ImGui/runtime Win32, inclusive quando o checkout local contém um XBE. Configurações que peçam o runtime gráfico Linux falham com uma mensagem explícita.
- Preset `linux-core`, módulo CTest e job Ubuntu 22.04 no CI. Os testes não usam assets nem código gerado do jogo; o cabeçalho do hook é produzido por `--symbols-only` a partir da configuração.
- `xbe_recomp` e `xbe_symbols` compilados como ELF x86-64 nativos. O cabeçalho `image_symbols.h` gerado em Linux e Windows coincidiu byte a byte.
- Núcleo compilado: memória, arquivos, disco, criptografia, objetos, threads, RTL, dispatch e logging. Isso verifica a compilação desses componentes; não equivale a um boot completo do kernel convidado ou a um teste de todas as suas APIs.
- Seis testes passaram em Linux e Windows: aliases de memória física/commit/decommit/guard; ISO sintética com nome Unicode e espaços, lookup sem distinção de caixa e leituras concorrentes; strong/weak hooks e exceções de continuação entre unidades de compilação; política de controles; hook de produção dos controles; atenuação de áudio.
- Corrigidos o fechamento do descritor `memfd` e a liberação da reserva virtual em falhas de inicialização Linux; o teste também verifica ausência de descritor residual após sucesso.
- O runtime Windows LLVM-MinGW continuou compilando após a separação de dependências.

Pendências da etapa 1: exercitar mais APIs do kernel quando houver um host Linux de boot e auditar fibers/símbolos usados. Nenhuma validação de GPU, áudio Linux, Steam Deck, launcher Linux ou AppImage foi feita nesta primeira entrega. O experimento local de acosf e os diretórios de assets de trabalho ficam fora das alterações publicáveis.

### Diagnóstico de janela iniciado

O alvo opcional `genma_window_smoke` e o preset `linux-window` validam a janela SDL3 com apresentação por software e, por argumento explícito, Vulkan. O self-test passou com SDL dummy e com X11/WSLg, incluindo resize, fullscreen, eventos de teclado, controle virtual e imagem 4:3. A apresentação Vulkan sobre llvmpipe passou com 95 quadros e comparação exata dos 691.200 pixels da leitura final contra a referência. O Dozen/Mesa 26.2.2, compilado separadamente, também passou na RTX 3050 via D3D12/GPU-PV: 136 quadros, dois readbacks e zero pixels diferentes. Houve avisos de não conformidade do Dozen e de estrutura desconhecida pela camada de validação antiga. A prova dos shaders/operações do jogo e a migração do host continuam pendentes; esse diagnóstico não cria um executável Linux do jogo. Detalhes, comandos e limitações em [linux-window-test.md](linux-window-test.md).

## Referências técnicas verificadas

A integração experimental posterior escolheu Vulkan direto atrás de `gpu::Backend` e manteve D3D12 no Windows. O boot real e as telas iniciais passaram na RTX 3050 via Dozen, com shaders do tradutor compilados por DXC e áudio SDL. Isso não satisfaz ainda os critérios de gameplay e paridade das etapas 2/3. Build, controles, resultados e limitações estão em [linux-runtime-test.md](linux-runtime-test.md).

- [SDL GPU](https://wiki.libsdl.org/SDL3/CategoryGPU): API gráfica multiplataforma; a cobertura das operações do nosso backend ainda exige a prova proposta.
- [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross): tradução de shaders HLSL/SPIR-V para SDL GPU.
- [DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler): compilação HLSL em DXIL/SPIR-V; não é substituição direta de D3DCompile com perfis Shader Model 5.
- [SDL_OpenAudioDeviceStream](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream): saída PCM com callback, adequada à fronteira atual `audio::StartOutput`.
- [vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton): permite build Linux nativo, mas o próprio projeto descreve esse uso como voltado principalmente a desenvolvimento e não fornece sozinho DXGI. Não tratá-lo como solução pronta para portar o host atual.
