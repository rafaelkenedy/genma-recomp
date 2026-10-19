# Como o jogo monta o volume de sombra — 19/10/2026

Análise estática do código recompilado (`generated/game`, comentários de assembly). O REA/Ghidra recusa o XBE ("unsupported binary format"), então o assembly foi lido pelos comentários do gerador (script exploratório no scratchpad: índice endereço → instrução).

## Funções

| Endereço | Papel |
| --- | --- |
| `0x0DD920` → `0x15BAC0` | Início do quadro: `Lock(VB 0x4EC688, flags 0)` (chama `D3D_BlockOnResource`), zera contadores `0x4EC79C` (triângulos) e `0x4EC7C8` (vértices), aponta `0x4EC7BC` para os dados do index buffer `0x4EC7A8`. |
| `0x15B6C0` | Por personagem: calcula a direção da luz e chama o construtor para cada parte da malha de sombra. |
| `0x15AFC0` | Construtor do volume (CPU). |
| `0x0E3B30` → `0x15B390` | Desenho: duas passadas `DrawIndexedVertices(TRIANGLELIST)` e depois o escurecimento em tela cheia (`0x15ADC0`). |

## Direção da luz (`0x15B6C0`)

- Percorre a tabela de luzes `0x41CFA8` (32 entradas de 32 bytes). Cada luz ativa e próxima da posição do personagem (`entidade+0x890`) entra na direção, com peso pela distância (`0x17BD36` normaliza, `0x147E0`/`0x168C0` acumulam).
- Uma luz do tipo 1 substitui todas. A componente Y é limitada por `0x546800`. Com a flag `entidade+0x8B4 & 0x4000000`, a direção fica fixa em (0, −1, 0).
- `0x159CB0` monta a matriz de extrusão: projeção no plano do chão, com altura de `0x5467F8` + `entidade+0x894`.

## Construtor (`0x15AFC0`)

1. A malha de sombra é **própria e de baixa resolução**: até 255 vértices e 410 triângulos, cada vértice com um índice de osso em `+0x0C`.
2. Cada vértice é transformado na CPU pela matriz do seu osso (`entidade+0x8EC + osso*64`), gerando o anel "perto". O resultado passa pela matriz de extrusão, gerando o anel "longe".
3. Por triângulo: normal = produto vetorial das arestas, `dot(normal, luz)`. Comparado com 0.0 (`0x1D10B8`): **≥ 0** → aceso (flag 1 em `0x4EC7B0`) e as 3 arestas, com o triângulo vizinho de cada uma, entram numa lista.
4. Silhueta: uma aresta de triângulo aceso cujo vizinho é `0xFFFF` ou não aceso → um quad (2 triângulos) entre o anel perto e o longe.
5. **Sem tampas** (z-pass puro). O jogo não usa polygon offset em lugar nenhum: nem os métodos NV2A `0x330/0x334/0x338/0x384/0x388`, nem o setter de ZBias.

## Desenho (`0x15B390`)

Os estados são gravados direto como métodos NV2A (`0x182C40`): shade FLAT, `DEPTH_MASK 0`, stencil ALWAYS, blend ZERO/ONE (sem cor), matriz view×proj em c0–c3.

- Passada 1: `CULL_FACE 0x901`, `STENCIL_OP_ZPASS = INCR_WRAP (0x8507)`.
- Passada 2: `CULL_FACE 0x900`, `ZPASS = DECR_WRAP (0x8508)`.
- Passada opcional, que não ocorreu nas capturas: quads de `0x4EC690` com `ZPASS = ZERO`, se `0x4EC7C4 ≠ 0`.
- Depois, `0x15ADC0` escurece onde stencil ≥ 1.

## Atualização

A cintilação no rosto **não vinha da sombra**: desligar a sombra inteira não a removia. A causa era a precisão da profundidade dos modelos (`docs/depth-precision.md`). Uma reescrita do construtor com histerese foi testada, verificada como bit a bit igual ao original sem histerese (200 chamadas, `XBE_SHADOW_CHECK`), não mudou nada visível e foi removida.

## Conclusões

- **A sincronização está certa.** Os índices são escritos direto na memória do IB, sem Lock, a cada quadro. Isso é seguro no Xbox, porque `DrawIndexedVertices` os copia para o pushbuffer na chamada. O `render_thread.cpp` faz o mesmo: `Put(indices)` no `Draw`. Os vértices passam por Lock com espera (`BlockOnResource` → `WaitResource`).
- **O x87 da classificação está certo** (`fcomp` / `fnstsw` / `test ah,1`), e o limiar é 0.0.
- **O auto-sombreamento no rosto é do próprio algoritmo.** Os lados do volume saem da silhueta de uma malha simplificada, que fica perto ou dentro do corpo detalhado. Sem tampas e sem bias, todo lado que passa na frente da pele, vista da câmera, incrementa o stencil. Os triângulos perto do terminador trocam de lado a cada pequena mudança de pose (animação parada) ou da direção da luz, que muda com a posição do personagem. Isso liga e desliga quads da silhueta. As contagens de índices variando entre capturas (1.650 → 1.656) confirmam essa troca.
- **Hipótese em aberto:** o hardware original pisca igual? Só uma comparação com o xemu ou com o console responde. Se lá não pisca, as diferenças possíveis são a precisão x87 (80 bits contra double) perto de dot ≈ 0, a cadência de animação (30 contra 60 Hz) e a profundidade (`D24` vs. reconstrução `z/w`) nos lados quase tangentes à pele.
