# Análise do quadro nativo 4:3

Captura: `out/build/release/captures/125659/` (19/10/2026).
Print: `out/build/release/captures/shots/native-4x3-analysis.png`.
Configuração: janela de 640×480, renderização a 100%.

Atualização: a causa foi localizada no cálculo de `acos` da cinemática inversa e corrigida em `src/game/math_flags.cpp`. A reprodução completa da animação passou a coincidir com o código original dentro da tolerância de comparação. Ver `character-mesh-investigation.md` para a evidência e a validação após a correção. Os dados abaixo descrevem a captura anterior à correção.

## Resultado visual

A deformação persiste em 4:3: uma perna forma uma faixa alongada, e há separações aparentes entre partes do corpo. Mudar a proporção da saída não resolveu o problema.

## Programas gráficos capturados

O quadro registra 81 chamadas de desenho, seis fontes de vertex shaders e dez de pixel shaders. Os arquivos HLSL representam a tradução do runtime; não são uma leitura dos resultados efetivamente calculados pela GPU.

| Programa | Função observada |
| --- | --- |
| `vs_B307B6C86A2EB9DD.hlsl` | Skinning com até três influências, seguido de projeção e iluminação. |
| `vs_C4E58081A1E2BA6F.hlsl` | Transformação por uma matriz de osso, seguida de projeção e iluminação. |
| `vs_22C19F7F3845C23A.hlsl` | Projeção de posições e geração de coordenadas para uma passagem auxiliar. |
| Demais vertex shaders | Vértices pré-transformados, interface e composição do quadro. |
| Pixel shaders | Amostragem de texturas, combinação de cores, transparência e neblina; os programas capturados escrevem cor, sem saída `SV_Depth`. |

O shader de skinning usa `v0` para posição, `v1.xyz` para pesos, `v2.xyz` para índices normalizados, `v3` para normal e `v4` para coordenadas de textura. O índice é multiplicado por 1024, convertido pelo ARL e usado em acessos relativos a `c[16 + a0]`.

## Aresta deformada localizada

Na chamada **0006**, shader `vs_B307B6C86A2EB9DD.hlsl`, uma aresta efetivamente referenciada pela lista de triângulos conecta os vértices 4 e 30 do buffer capturado:

| Dado | Vértice 4 | Vértice 30 |
| --- | --- | --- |
| Posição original | (-127,977; -985,119; 25,654) | (-143,156; -1034,323; -6,762) |
| Índice de osso em byte | 1 | 2 |
| Pesos | (1; 0; 0) | (1; 0; 0) |
| Constantes da matriz, em índices físicos | 20–23 | 24–27 |
| Constantes na nomenclatura D3D | c-76 a c-73 | c-72 a c-69 |

Recalculando em CPU as fórmulas de skinning e projeção do HLSL com esses dados:

- Comprimento original da aresta: **60,85 unidades**.
- Comprimento após cada vértice usar sua matriz: **819,67 unidades**, aproximadamente **13,47 vezes maior**.
- Comprimento projetado: **117,74 pixels** em uma imagem de 640×480.
- Aplicando a mesma matriz aos dois vértices, o comprimento permanece próximo de 60,85 unidades. Isso é um teste de diagnóstico, não uma correção proposta.

As duas matrizes são finitas e têm determinantes de rotação próximos de 1. A evidência aponta para uma incompatibilidade entre as transformações associadas a vértices vizinhos; ela não determina, sozinha, se o erro está no remapeamento da paleta, na construção da pose, nos dados de vínculo da malha ou em uma etapa anterior.

## Verificações nos buffers

Foram analisadas 45 chamadas com os dois programas de malha reconhecidos. Nessas chamadas, não foram encontrados índices de vértices fora dos buffers nem endereços de ossos fora da faixa/alinhamento esperados. Os pesos e posições de entrada inspecionados são finitos. Na aresta problemática, os pesos somam exatamente 1.

Dados detalhados: `out/build/release/captures/125659/mesh-analysis.json`.
Script utilizado: `out/analyze_native.py`.

Não foi aplicada uma alteração ao renderer nesta análise. A próxima verificação deve rastrear as duas matrizes da chamada 0006 até os nós originais do esqueleto e o remapeamento da paleta.
