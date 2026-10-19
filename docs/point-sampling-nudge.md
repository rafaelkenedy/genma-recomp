# Cintilação ao lado da ponte: amostragem pontual ampliada — 19/10/2026

## Sintoma

Na ponte (ROOM001), a faixa esquerda com os postes cintila de leve, como se dois cenários pré-renderizados estivessem descolando um do outro.

## Como a cena é montada

- Desenho 1: o fundo estático, um quad de tela (`XYZRHW`) com a textura 1024×512 e filtro linear.
- Desenho 2: por cima, a faixa esquerda animada (água e postes), outro quad, com o **atlas** `81AC0000` (1024×1024, 4×2 células de 208×416 texels) e **filtro pontual**. A célula troca a cada 2 quadros.

As 8 células estão alinhadas: o melhor deslocamento entre elas é (0, 0) e a diferença média na região dos postes fica entre 0,03 e 0,09 níveis. O asset não tem defeito.

## Causa

Em 4K, a escala é de 4,5 pixels de tela por pixel do console. A cada 2 pixels do console, o centro de uma coluna da tela cai **exatamente** na divisa entre dois texels. A amostragem pontual (`floor(u·W)`) escolhe o texel pelo arredondamento da coordenada interpolada, e esse arredondamento muda com o deslocamento da célula do quadro. O resultado são colunas dos postes pulando um texel a cada troca de célula: listras verticais regulares no mapa de variação.

## Correção (runtime, `gpu/d3d12.cpp`, `FillTextureConstants`)

Nas etapas com filtro pontual (mag e min < LINEAR), `texsize[i].zw`, que o shader já soma a toda coordenada, recebe um empurrão de `1/1024` texel. Esse valor fica acima do erro de interpolação (~10⁻⁴ texel) e muito abaixo de um texel. Os casos de divisa passam a pegar sempre o mesmo texel.

**Chave:** `g_point_nudge`. Fica ligada por padrão. `XBE_POINT_NUDGE=0` desliga, e um arquivo `point_nudge` ao lado do exe, contendo `1` ou `0`, troca ao vivo.

## Medição

90 quadros com o gravador, contando os pixels cuja faixa de brilho no tempo passa de 4 níveis na faixa dos postes (x 120–215, y 30–440, na escala do console):

| | > 4 níveis | > 8 níveis |
| --- | --- | --- |
| Desligada | 1.902 | 803 |
| Ligada | 219 | 123 |

As listras sumiram do mapa de variação. Sobram a água, o personagem e o HUD.
