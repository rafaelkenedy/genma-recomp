# Runtime Linux/Vulkan experimental

Este é um alvo de desenvolvimento da issue #8. Ele executa o código recompilado do jogo em um ELF Linux e substitui o host Win32, XAudio2 e D3D12 por SDL3, SDL Audio e Vulkan. Não é o diagnóstico `genma_window_smoke`, não usa Wine e ainda não é um pacote para jogadores.

## Compilação

É necessário Linux x86-64, Clang/LLD, CMake 3.25 ou posterior, headers Vulkan, dependências SDL/X11 e um DXC Linux com suporte a SPIR-V. No Ubuntu 22.04, a saída WSLg também precisa de `libpulse-dev` na compilação. O desenvolvimento local usou CMake 3.31.6 portátil, Clang 17 e o [DXC oficial v1.8.2407](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2407). Os assets e `assets/default.xbe` devem ser fornecidos localmente pelo usuário.

```sh
cmake --preset linux-game
cmake --build --preset linux-game -j3
export XBE_DXC=/caminho/do/dxc/bin/dxc
./out/build/linux-game/GenmaOnimusha
```

O preset exige explicitamente `XBE_BUILD_RUNTIME=ON` e `XBE_EXPERIMENTAL_LINUX_RUNTIME=ON`. Os presets de ferramentas e de janela continuam sem carregar o jogo. A build Windows continua selecionando o renderer D3D12.

O DXC é executado por `posix_spawnp` com argumentos separados, sem shell. O runtime guarda o HLSL gerado, SPIR-V e mensagens do compilador em `shader-cache-vulkan/` ao lado do executável. O cache inclui no nome o hash do shader adaptado para Vulkan; uma falha de compilação encerra a execução e aponta para os arquivos de diagnóstico.

## Execução no WSL com Dozen local

O Dozen não é necessário no Linux nativo: usar o driver Vulkan da GPU. Para a instalação local isolada descrita em [linux-window-test.md](linux-window-test.md):

```sh
export VK_ICD_FILENAMES="$PWD/out/tooling/dozen-26.2.2/share/vulkan/icd.d/dzn_icd.x86_64.json"
export VK_DRIVER_FILES="$VK_ICD_FILENAMES"
export LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEODRIVER=x11
export XBE_DXC="$PWD/out/tooling/dxc-linux-1.8.2407/bin/dxc"
export XBE_CAPTURE_DIR="$PWD/out/linux-game-captures"
export XBE_RUN_SECONDS=60
./out/build/linux-game/GenmaOnimusha
```

`XBE_RUN_SECONDS` limita a duração; sem a variável, a janela continua aberta. F11 alterna tela cheia, F3 captura o render target do jogo e Esc/fechar encerra. F1 informa que o menu ainda não está implementado. Os gamepads usam SDL; `cmd` ao lado do executável mantém o autopilot existente. Saves e configurações desta build ficam em `out/build/linux-game/`, separados da build Windows. A seleção de assets verifica o SHA-1 do XBE e rejeita outra release.

O host Linux também fornece um controle de teclado no port 0: WASD é o analógico esquerdo; setas são o D-pad; Enter é Start; Backspace é Back; Z/X/C/V são A/B/X/Y; 1/2 são Black/White; Q/E são os gatilhos. Os comandos de teclado são zerados ao perder foco, e um gamepad físico continua utilizável. Isso permite testar no WSL mesmo sem encaminhar um controle USB.

### Controle do Windows no teste WSL

O WSL desta máquina não enumera o Xbox 360 USB conectado ao Windows e seu kernel não habilita `xpad`. Existe uma ponte opcional de desenvolvimento para esse caso: `tools/controller_bridge_win.cpp` lê um controle XInput no Windows e transmite seus estados ao host Linux por pipes de um subprocesso. O host cria um gamepad SDL virtual; o polling normal do jogo e os controles modernos continuam no caminho compartilhado. Não há serviço, porta de rede ou mudança de proprietário do dispositivo USB.

O helper Windows pode ser compilado com `XBE_BUILD_CONTROLLER_BRIDGE=ON`, pelo alvo `genma_controller_bridge`, usando a toolchain Windows. `genma_controller_bridge.exe --probe` identifica as portas XInput disponíveis. A sessão local usa o helper compilado em `out/tooling/genma_controller_bridge.exe`:

```sh
export XBE_CONTROLLER_BRIDGE="$PWD/out/tooling/genma_controller_bridge.exe"
# Demais variáveis Vulkan/DXC do exemplo anterior, então:
./out/build/linux-game/GenmaOnimusha
```

Sem essa variável, a ponte não é iniciada. Em Linux nativo, usar o controle diretamente pelo SDL. A ponte atende apenas dispositivos expostos por XInput no Windows; não é uma solução geral de encaminhamento USB. Ela transmite botões, os dois analógicos, gatilhos, conexão/desconexão e comandos de vibração. O helper permanece ativo enquanto o pipe do jogo estiver aberto; ao encerrar, zera os motores. O host remove o gamepad virtual se o fluxo parar por mais de um segundo.

O teste sem hardware `test_wsl_controller_bridge` usa mensagens deliberadamente fragmentadas e verifica botões, sinais/extremos dos eixos, extremos dos gatilhos, retorno do comando de vibração e desconexão. Os gatilhos são convertidos para a faixa bruta de joystick virtual SDL (−32768 a 32767), para que o gamepad reporte 0 a 32767. Esse teste não comprova a sensação ou a intensidade dos motores do controle físico.

Verificação local em 28/10/2026: o probe Windows identificou o Xbox 360 na porta XInput 0; o teste da ponte passou. Na execução real, `out/linux-game-play.log` registrou a conexão da ponte e `controller 1 plugged in: Windows XInput controller (WSL test bridge)`, seguidos pelo reconhecimento do port 0 pelo jogo. A resposta física dos botões e a sensação da vibração ainda dependem do teste interativo.

`XBE_VK_VALIDATION=1` habilita a camada `VK_LAYER_KHRONOS_validation`, se instalada. A camada antiga do Ubuntu 22.04 pode desconhecer extensões recentes do driver Dozen. O próprio Dozen avisa que não é uma implementação Vulkan conforme; seu uso aqui é experimental.

## Escopo técnico inicial

- Host/eventos/hotplug SDL no thread principal e mixer float estéreo de 48 kHz por SDL Audio/PulseAudio.
- Fibers do XAPI com `ucontext`, pilha host separada e páginas de guarda. Teste sem assets verifica mil trocas, os valores locais e o retorno ao contexto original.
- Vértices convertidos para float4 pela montagem existente. Gerador HLSL compartilhado com Windows, bindings Vulkan explícitos por classe de recurso e localizações de interface preservadas entre os estágios. DXC inverte Y no vertex shader.
- Texturas decodificadas pelo runtime existente; render targets na resolução convidada; render passes, blend, depth/stencil, scissor e consultas de oclusão. Amostragem do próprio attachment usa uma cópia intermediária.
- Cópias CPU de superfícies e importação de depth/stencil para a GPU; cópias GPU de attachments compatíveis. Readback de cor para a memória convidada e captura BMP do render target. As submissões são esperadas sincronicamente nesta primeira implementação. NOOVERWRITE também usa readback síncrono e registra essa limitação.
- Apresentação com letterbox 4:3, overscan e gamma; gamma não identitário usa uma passagem pela CPU nesta versão inicial. Não usar essa implementação para comparar desempenho com D3D12.

Ainda faltam paridade visual, menu ImGui, texturas HD, render scale e validação no Steam Deck. O launcher CLI e o empacotamento experimental estão descritos em [linux-release.md](linux-release.md) e [ci-release-multiplatform-plan.md](ci-release-multiplatform-plan.md). Readback de profundidade, cópias GPU de profundidade para cor, clears parciais de canais e algumas operações de rasterização ainda encerram com diagnóstico explícito. Rede XNet não é compilada neste alvo; os símbolos ausentes do XBE conservam seus traps, em vez de simular uma rede funcional. Não considerar a issue #8 concluída com base apenas no build ou no boot.

## Resultado local de 28/10/2026

- O ELF Linux carregou o XBE e resolveu os imports do kernel sem ausências. O backend identificou `Microsoft Direct3D12 (NVIDIA GeForce RTX 3050)` pelo Dozen/Mesa 26.2.2. O mixer abriu saída SDL estéreo float de 48 kHz.
- Foram exercitados o boot, as fibers reais do jogo, os shaders HLSL gerados pelo runtime e a importação CPU de profundidade/stencil usada nas telas iniciais. Corrigido o caso observado de `CopyRects(count=1, rects=nullptr)`: copiar toda a superfície, sem ler a página convidada zero.
- Uma execução de 60 segundos sob GDB com `XBE_VK_VALIDATION=1` terminou com código 0. O último contador antes do encerramento foi 900 quadros, 4.347 draws e dez pipelines. Não foram registradas mensagens `Validation Error`, VUIDs ou sinais fatais nessa rodada. O aviso de não conformidade do Dozen continuou presente.
- As capturas `out/linux-game-captures/frame-120.bmp` (logo Capcom) e `frame-600.bmp` (tela de título Genma Onimusha) foram inspecionadas. São leituras do render target, não screenshots do compositor. Log completo: `out/linux-game-debug.log`.
- Os sete testes CTest do Linux passaram, incluindo mil trocas de fiber, memória, disco, ABI, controles modernos e atenuação de áudio. O runtime de release Windows LLVM-MinGW também compilou após as mudanças compartilhadas de CMake, entrada e diagnóstico.

Esta rodada comprova o boot e as telas iniciais. Gameplay 3D, combate, sombras, save/load, sincronismo audiovisual e paridade visual/desempenho continuam sem validação. A contagem de quadros em modo diagnóstico não é um benchmark. A sessão interativa seguinte usa `out/linux-game-play.log` e capturas separadas em `out/linux-game-play-captures/`.
