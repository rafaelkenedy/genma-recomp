# Fontes e texto do jogo — 19/10/2026

Levantamento de como o Genma Onimusha desenha texto e onde estão as fontes. As imagens extraídas são da Capcom e ficam só no PC de quem tem o jogo; este documento descreve os formatos e o que há em cada arquivo.

## Tela de título: texto pronto em imagem

Captura `out/build/release/captures/215226` (5 desenhos):

| Desenho | Textura | Conteúdo |
| --- | --- | --- |
| 1 | `82E1D000`, 1024×512, DXT1 (formato `C`), filtro linear | logo **e** créditos/copyright desenhados dentro da imagem de fundo |
| 2 | `82ECF000`, 256×512, com transparência, filtro linear | folha de sprites com frases prontas: PRESS START BUTTON, NEW GAME, LOAD GAME, OPTION, Normal/Ultimate/Easy/Hard Mode, Samanosuke, Kaede, Special Feature… |
| 3–4 | `833CC000`, 16×16 | quads de cor (escurecimento/fade) |

O quadro é copiado para um alvo de 640×480 e desenhado de volta na tela (`copy` e desenho 7).

Em 4K o texto do título é a textura ampliada cerca de 4,2×:

- **macio**: filtro linear sobre uma textura de resolução fixa;
- **com chuvisco em blocos** em volta das letras: a compressão DXT1 (blocos 4×4) do fundo. Em 640×480 quase não aparecia; ampliado fica visível. O defeito é do asset original.

O fundo de 1024×512 também é esticado de forma desigual para a tela (cerca de 1024 → 600 na largura e 512 → 448 na altura).

## Fontes de verdade (`image/etc`)

Os arquivos `.ldt` são contêineres **LINK** com imagens **OIM** (formato em `tools/oim.py`):

```
LINK  'LINK', número de entradas, ?, entradas
OIM   +0x00 'OIM '   +0x04 tamanho do bloco
      +0x10 log2 da largura, log2 da altura (u8), formato (u16, 0x14 nos vistos)
      +0x14 deslocamento e tamanho dos pixels
      +0x20 formato, deslocamento e tamanho da paleta (4 bytes por cor: B G R A, alfa 0..0x80)
pixels: 4 bits (nibble baixo primeiro) ou 8 bits por pixel, lineares
```

| Arquivo | Imagens | Conteúdo |
| --- | --- | --- |
| `mojist_e.ldt` | 2 × 256×256, 4 bpp | **fonte principal, inglês.** Página 0: ASCII completo em células de 16×16 com 16 tons de cinza (borda suavizada), símbolos do controle (R1 R2 L1 L2, ○ × △ □, setas), ∞, LvMAX; no canto, uma cópia da fonte pequena. Página 1: kanji e katakana (青緑忍魂…, ステレオ, モノラル), para telas de opções e nomes |
| `mojist_j.ldt` | 2 × 256×256 | fonte japonesa: hiragana, kanji e o mesmo bloco ASCII |
| `mojist_p.ldt` | 2 × 256×256 | terceira variante (provavelmente a europeia) |
| `moji.ldt` | 1 × 256×256, 4 bpp | fonte pequena de ~8×8, só ASCII, paleta colorida: provavelmente texto de depuração |
| `e_bun0..3.ldt` (`f_`, `g_`, `mon_`) | 512×512, 4 bpp | **frases prontas em imagem**, giradas 90° em colunas: descrições de itens ("A Dragon Orb, which had been enhanced with a demon's soul, may help the one who owns it.") com o alfabeto dos Oni ao lado; uma por idioma |
| `FILE**E/F/G.ldt` | 295 KB cada | documentos do jogo por idioma (ainda não decodificados) |
| `e_title`, `e_tegami`, `e_gm_ini`… | | títulos, cartas (*tegami*) e telas de menu por idioma (ainda não decodificados) |

Conclusão: o jogo tem **uma fonte de verdade** (`mojist_*`, glifos 16×16 compostos letra a letra) para o texto dinâmico (status, opções, contadores), e usa **imagens prontas** para títulos, menus do título e descrições de itens.

## O que dá para fazer

1. **Filtro melhor nos elementos 2D** (quads de tela: texto e HUD), por exemplo bicúbico ou "sharp bilinear": tira o aspecto borrado sem inventar detalhe. Pequeno, no renderer.
2. **Fonte em alta resolução para o `mojist_*`**: como os glifos estão numa grade fixa de 16×16, dá para gerar um atlas 4× ou 8× maior **a partir de uma fonte livre** (licença aberta), com o mesmo layout, e o runtime trocar a textura ao reconhecê-la. Como as coordenadas são normalizadas, o atlas maior serve sem mudar o jogo. O atlas gerado não deriva da arte da Capcom, então pode ir no repositório.
3. **Troca de texturas em geral** (como o Dolphin): o runtime exporta cada textura com um código, o usuário põe versões em alta resolução numa pasta. Serve para os títulos e frases prontas; os pacotes de texturas ficam fora do repositório.

## Pendente

- Capturar o texto **durante o jogo** (status, diálogo, inventário) para confirmar onde o `mojist_*` é desenhado, com qual filtro e tamanho de tela.
- Decodificar `FILE**`, `*_title`, `*_tegami` e `*_gm_ini`.
- Achar no código a função que desenha os glifos (texturas do `mojist`), e como mapeia caractere → célula.
