# Sombra do personagem piscando — 19/10/2026

O usuário identificou oscilação na sombra do personagem, na ponte (ROOM001). Capturas antes da correção: `out/build/release/captures/135503`, `135504`, `135604` e a sequência `135655` a `135708`.

## Falha confirmada

Cada quadro copia o cenário pré-renderizado de `03A14000` para o buffer D24S8 `03B40000`. A leitura do processo na ponte confirmou 640×480 pixels, profundidade variável com 5.785 valores distintos e stencil zero nos 307.200 pixels. Origem e destino na memória do jogo coincidem.

No renderer, porém, `Copy()` só procurava o destino como alvo de **cor** depois da cópia em CPU. `UploadTarget()` também rejeitava formatos de profundidade. Assim, o buffer GPU não recebia a profundidade do cenário copiada pelo jogo. Isso deixa o teste de profundidade dos volumes de sombra sem os dados do cenário.

As duas passagens do volume usam incremento/decremento de stencil com wrap e teste de profundidade LEQUAL. A composição escurece pixels conforme o stencil. Os estados estavam consistentes entre os quadros; os vértices e as projeções eram finitos, sem cruzamentos com near/far. O clear final F3 inclui profundidade e stencil (bits 1 e 2); não há evidência de que esse clear estivesse omitindo stencil.

As primeiras imagens não capturaram uma alternância grande da sombra, portanto a oscilação temporal relatada não foi quantificada nessas amostras. A falha da importação de profundidade foi demonstrada independentemente.

## Correção aplicada

Em `sdk/runtime/gpu/d3d12.cpp` (destino real `H:/dev/xbox/XboxXBERecomp/runtime/gpu/d3d12.cpp`), a cópia em memória agora reconhece um destino de profundidade e importa seus dados para a GPU. O formato Xbox é decodificado para uma textura R32_FLOAT, desenhada em SV_Depth pela passagem de profundidade já existente, com amostragem pontual. A textura é reutilizada enquanto os dados e o formato não mudam. O stencil uniforme da origem é restaurado, incluindo zero no caso do cenário capturado.

Esta implementação não importa stencil não uniforme; esse caso é identificado e mantém o caminho anterior com registro no log. Não foi forçada a opacidade da sombra nem aplicada uma alteração arbitrária de depth bias.

## Verificação

- Release recompilado e jogo reiniciado em tela sem bordas, resolução automática.
- O log `genma_038.log` confirma `CPU depth imported 83B40000, 640x480`.
- Captura posterior `140139/frame.txt` registra `CPU depth upload 83B40000 stencil 0` antes dos desenhos do personagem.
- O gerador de alpha da correção anterior continua aprovado pelo teste `tools/test_alpha_premodulate.cpp`.
- `tools/test_scene_depth.cpp` verifica o formato D24S8 consumido pela nova passagem: normalização, precisão e pitch com preenchimento.

A execução posterior foi verificada na cena inicial. Ainda falta confirmar visualmente, com o usuário, que a oscilação na ponte desapareceu.

Comando do teste de decodificação:

```powershell
cmd /c "dev.bat clang++ -std=c++20 -O2 -ffunction-sections -fdata-sections -Isdk/runtime tools/test_scene_depth.cpp sdk/runtime/gpu/texture.cpp -Wl,/OPT:REF -o out/test_scene_depth.exe"
.\out\test_scene_depth.exe
```
