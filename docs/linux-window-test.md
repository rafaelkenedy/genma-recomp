# Janela Linux — teste isolado

Segunda entrega da base Linux da issue #8. O alvo `genma_window_smoke` não carrega ISO, código recompilado ou assets. Usa SDL3 3.4.16, já fixado no SDK. O modo padrão apresenta por **software**; `--backend vulkan` apresenta através de SDL GPU com backend Vulkan explícito. Nenhum dos modos carrega os shaders, sombras, áudio ou renderer 3D do jogo.

## Compilar e abrir

Linux x86-64 com Clang, CMake 3.25+, Make, Git e headers X11. No Ubuntu: `libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxss-dev libxtst-dev`. O preset inicial usa X11; Wayland e KMSDRM ficam desabilitados apenas neste diagnóstico, sem restringir a futura escolha do host do jogo.

```sh
cmake --preset linux-window
cmake --build --preset linux-window --target genma_window_smoke --parallel 4
./out/build/linux-window/genma_window_smoke
```

O preset habilita `XBE_BUILD_WINDOW_TEST=ON` e mantém `XBE_BUILD_RUNTIME=OFF`. SDL/renderer são habilitados para esse alvo; ImGui e o runtime do jogo continuam excluídos. O preset `linux-core` mantém seu build independente de SDL.

No WSLg, executar no Ubuntu com `DISPLAY` e o socket X11 disponíveis. Para escolher explicitamente o backend validado:

```sh
SDL_VIDEODRIVER=x11 ./out/build/linux-window/genma_window_smoke
```

Comandos:

| Comando | Efeito |
| --- | --- |
| F11 | Alterna janela/tela cheia |
| R | Redimensiona a janela para 960×720 |
| Setas | Move o marcador |
| Analógico esquerdo | Move o marcador |
| Esc ou fechar a janela | Encerra normalmente |

A janela começa em 1280×800, com imagem lógica 960×720 e letterbox para preservar 4:3. O círculo deve continuar redondo após redimensionar. O rodapé mostra dispositivos abertos e eventos de teclado. Logs vão ao stderr: driver, renderer, alterações de tamanho em pixels, teclas, conexões e botões de gamepad.

## Verificação automática

```sh
# CI: não abre uma janela visível; SDL dummy + software.
ctest --preset linux-window -R '^test_sdl_window$'

# Janela real: repete as operações através do driver X11/WSLg.
SDL_VIDEODRIVER=x11 ./out/build/linux-window/genma_window_smoke --self-test --screenshot out/window-x11.bmp
```

O self-test usa o mesmo loop de eventos da execução interativa. Conecta um joystick virtual pela API SDL, confirma sua abertura como gamepad, envia um eixo e um botão, verifica os eventos e desconecta o dispositivo. Injeta eventos de teclado para mover, redimensionar, alternar fullscreen e encerrar, conferindo o tamanho e as flags observadas depois. Confere ainda a proporção de apresentação e os pixels de uma amostra de cor. Falhas retornam código 1; opções inválidas retornam 2. O teste tem timeout e não depende de um controle físico.

Isso valida o caminho SDL de controle virtual; não comprova funcionamento de Steam Input ou de um controle físico no Deck. Os eventos de teclado injetados não substituem a verificação de entrega de teclas reais pela janela do desktop. O teste dummy também não comprova a apresentação no compositor — a janela real precisa ser verificada separadamente.

## Apresentação Vulkan

Dependências de execução no Ubuntu: `libvulkan1 mesa-vulkan-drivers`; `vulkan-tools` identifica os dispositivos, e `vulkan-validationlayers` fornece a camada de validação usada no diagnóstico. O preset compila SDL GPU, mas o modo Vulkan só é ativado por argumento explícito:

```sh
vulkaninfo --summary
SDL_VIDEODRIVER=x11 ./out/build/linux-window/genma_window_smoke --backend vulkan
SDL_VIDEODRIVER=x11 ./out/build/linux-window/genma_window_smoke --backend vulkan --self-test --screenshot out/window-vulkan.bmp
```

O desenho de referência continua sendo gerado em uma superfície RGBA de 960×720 pela CPU. O modo Vulkan faz upload por transfer buffer, executa um blit para uma textura de saída com letterbox 4:3 e apresenta essa textura na swapchain da janela. A imagem Vulkan pode ser lida de volta por transfer buffer, sincronizada com fence; a captura salva vem dessa textura final, não da superfície CPU. Quando a saída tem 960×720, o self-test compara os 691.200 pixels com o padrão de referência. Também confere a proporção e amostras de cor/barras pretas antes e depois dos redimensionamentos.

O teste confirma criação de dispositivo, texturas, transferências, blit, swapchain, sincronização de readback e eventos. Usa os shaders internos de blit do SDL; não comprova compatibilidade dos shaders gerados pelo nosso tradutor, depth/stencil, skinning ou sombras. O dispositivo é registrado no log: Vulkan sobre llvmpipe/lavapipe continua sendo uma implementação CPU da API, não uma medição de GPU física. Falha no Vulkan retorna erro, sem fallback silencioso para o renderer software.

No CI, o teste Vulkan usa uma janela X11 em Xvfb e o driver Mesa disponível no runner. Isso complementa, sem substituir, WSLg e Steam Deck real.

### Resultado Vulkan local — 28/10/2026

- Loader Vulkan, Mesa e camadas de validação instalados no Ubuntu. `vulkaninfo --summary` identificou apenas `llvmpipe (LLVM 15.0.7, 256 bits)`, Mesa 23.2.1, dispositivo do tipo CPU e API Vulkan 1.3.255.
- Build com SDL GPU/Vulkan concluído. O self-test software continuou passando depois dessa alteração (3,15 segundos).
- Self-test Vulkan/X11 terminou com código 0, 95 quadros apresentados e duas leituras de volta sincronizadas por fence. Tamanhos observados: 1280×800 → 960×720 → 3840×2160 → 960×720.
- Proporção 4:3, amostra de cor e barras pretas conferidas. Na leitura final 960×720, os **691.200 pixels RGBA coincidiram exatamente** com a superfície de referência: zero pixels diferentes.
- Controle virtual e teclado injetado passaram pelos mesmos handlers de eventos. Não foram registradas mensagens de erro/validação no log.
- Captura da textura de saída Vulkan inspecionada: `out/linux-window-vulkan.bmp`. Logs: `out/linux-window-vulkan.log` e `out/linux-vulkan-devices.log`.

O resultado valida a apresentação pela API Vulkan sobre llvmpipe; não demonstra aceleração pela GPU física, desempenho no Deck ou compatibilidade do renderer do jogo. A comparação é com a textura final utilizada na apresentação, não uma leitura direta da imagem da swapchain após composição pelo desktop.

### Dozen no WSL — 28/10/2026

O pacote Mesa 23.2.1 do Ubuntu 22.04 instalado não fornece o ICD Dozen. Para experimentar a GPU física sem trocar os drivers do sistema, foi compilado o Mesa 26.2.2 oficial apenas com `vulkan-drivers=microsoft-experimental`, `platforms=x11`, Gallium/OpenGL/EGL/GBM/LLVM desativados. O SHA-256 do tarball foi conferido com as [notas oficiais da versão](https://docs.mesa3d.org/relnotes/26.2.2.html): `eeb29ca7e56cfaa8e8a79538dcf834e3b18e501c31bef5145e959ea437cc4216`. O build utilizou Meson 1.9.1, GCC 11.4 e o subprojeto DirectX-Headers 1.619.1. Instalação local: `out/tooling/dozen-26.2.2`; os arquivos em `out/` não fazem parte da distribuição do projeto.

Com esse build local disponível, selecionar o ICD só para uma execução:

```sh
export VK_ICD_FILENAMES="$PWD/out/tooling/dozen-26.2.2/share/vulkan/icd.d/dzn_icd.x86_64.json"
export VK_DRIVER_FILES="$VK_ICD_FILENAMES"
export LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
vulkaninfo --summary
SDL_VIDEODRIVER=x11 ./out/build/linux-window/genma_window_smoke --backend vulkan --self-test --screenshot out/linux-window-dozen.bmp
```

- `vulkaninfo` identificou `Microsoft Direct3D12 (NVIDIA GeForce RTX 3050)`, tipo discrete GPU, driver Dozen/Mesa 26.2.2 e API 1.2.354. O caminho é Vulkan → Dozen → D3D12 → GPU-PV do WSL → GPU NVIDIA; não é passthrough PCI.
- Self-test terminou com código 0, 136 quadros e dois readbacks. Resize, fullscreen 3840×2160, retorno a 960×720 e controle virtual passaram. Os 691.200 pixels RGBA finais coincidiram exatamente com a referência CPU. Captura da textura inspecionada: `out/linux-window-dozen.bmp`.
- Logs: `out/dozen-vulkaninfo.log`, `out/dozen-nvidia-smi.log` e `out/linux-window-dozen.log`.
- O Dozen avisa que não é uma implementação Vulkan conforme e deve ser usado para testes. A camada de validação do Ubuntu (1.3.204) também registrou duas mensagens `VUID-VkPhysicalDeviceProperties2-pNext-pNext` por desconhecer `VkPhysicalDeviceLayeredDriverPropertiesMSFT` (`sType=1000530000`), estrutura presente nos headers atuais usados pelo SDL. Portanto, esta execução passou na verificação funcional e de pixels, mas não teve um log de validação limpo; uma camada atualizada ainda precisa ser testada.

Isso confirma apresentação e transferências na RTX pelo WSL neste ambiente. Não mede desempenho, não valida os shaders do jogo e não substitui o teste com Vulkan nativo no Linux/Steam Deck.

`--seconds N` limita uma execução interativa a até 3600 segundos. `--screenshot caminho.bmp` salva o conteúdo renderizado no encerramento. A captura SDL comprova o conteúdo produzido pelo renderer; uma captura da janela no desktop é evidência adicional da apresentação pelo WSLg.

## Estado da validação

Validado em 28/10/2026 no Ubuntu 22.04.5/WSLg, com Clang 17 e SDL 3.4.16:

- Build Linux nativo concluído; checagem de sintaxe também passou com o Clang Windows.
- `test_sdl_window` passou com `SDL_VIDEODRIVER=dummy`, em aproximadamente 3,3 segundos.
- Self-test real com `SDL_VIDEODRIVER=x11` passou, incluindo janela inicial 1280×800, redimensionamento para 960×720, fullscreen observado em 3840×2160 e retorno a 960×720.
- Gamepad virtual: conexão, eixo, botão e desconexão confirmados pelo loop de eventos. Teclado: eventos injetados Right/R/F11/Escape processados. Proporção 4:3 e pixels da amostra de cor conferidos.
- Captura SDL inspecionada: `out/linux-window-x11.bmp`. Log: `out/linux-window-x11.log`; resultado headless em `out/build/linux-window/Testing/Temporary/LastTest.log`. Encerramento limpo, código 0.
- Na execução interativa, sem injeção do self-test, `out/linux-window-interactive.log` também registrou F11, mudança real do tamanho reportado para 3840×2160 e as quatro setas. Isso acrescenta evidência de entrega de eventos ao host pelo desktop; não é uma confirmação completa de toda a navegação ou de um controle físico.

A ferramenta de inspeção do desktop Windows não iniciou nesta sessão (`failed to write kernel assets`, caminho não encontrado, inclusive após reset). Portanto, não foi feita validação adicional de screenshot do compositor ou entrega de teclas físicas via automação do desktop. A captura inspecionada é a imagem produzida pelo renderer SDL.

Não há validação de Steam Deck, controle físico ou Wayland nesta etapa. O renderer do jogo continua Windows/D3D12. A janela interativa usa os mesmos handlers exercitados pelo self-test e pode ser aberta com os comandos acima.
