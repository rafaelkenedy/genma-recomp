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

## A fonte em jogo: tela de Opções

Captura `out/build/release/captures/222443` (15 eventos).

- **Todo o texto da tela vem da `mojist_e` página 0**, carregada como a textura `833E5000`, idêntica ao arquivo decodificado.
- **Um único desenho (0009) escreve todo o texto:** 552 vértices em quads, ou seja, 138 quads para 69 caracteres. Cada caractere é desenhado duas vezes, uma como **sombra** deslocada e outra como letra; daí o contorno escuro.
- **Estado do desenho:**
  - formato `B` = **P8**: a fonte de 4 bits vira 8 bits com paleta;
  - **filtro linear** (mag, min e mip);
  - **blend desligado**, com a borda das letras cortada pelo **teste de alfa** (bordas duras, sem suavização);
  - vértices pré-transformados (FVF `0x144`).
- **Tamanho:** linhas de 17 a 20 px de altura no quadro de 640×480, a cada ~29 px. As células de 16×16 são desenhadas cerca de 1,25× maiores. Em 4K, cada texel da fonte vira ~5,6 px.
- Os outros elementos: `82EDD000` (256×256) é a folha da interface (marcadores, setas, barra de volume). O título "Options" e a moldura são imagens prontas.

Como o corte é por teste de alfa sobre uma textura ampliada com filtro linear, em 4K a borda das letras fica em degraus, e não macia.

## No código

| Endereço | O quê |
| --- | --- |
| `0x1E5860` | tabela com os nomes dos 1.588 arquivos do jogo: `MOJI` = 434 (0x1B2), `MOJIST_P` = 435, `MOJIST_E` = 436 (0x1B4), `MOJIST_J` = 437 (0x1B5) |
| `0x164D0(índice)` | carrega um arquivo da tabela no buffer compartilhado (`[0x55482C]`) e chama `0x15F60`, que interpreta o `LINK`/`OIM` e cria as texturas; é o carregador genérico de `.ldt` |
| `0xE36C0` | inicialização do jogo: carrega o `MOJIST_E` (índice 0x1B4) |
| `0xDB1A0` | troca a fonte conforme o idioma: `MOJIST_J` se o campo `[obj+0x7BD0]` for 0, senão `MOJIST_E` |

Falta: a função que monta os quads dos caracteres (caractere → célula, sombra, escala).

## O que outros projetos já fizeram (pesquisa de 19/10/2026)

- **Genma Onimusha (Xbox):** nenhum mod, tradução, pacote de texturas ou fonte encontrado.
- **Formato `.ldt`/`OIM`:** nenhuma ferramenta ou documentação pública encontrada (GitHub, ZenHAX, XeNTaX). O `tools/oim.py` parece ser o primeiro leitor.
- **Onimusha: Warlords, remaster de 2019:** existe um pacote de texturas 4K feito com IA (Evgeshajk, ~12 GB) que refaz os fundos pré-renderizados e as texturas dos modelos. É para outro executável e outros arquivos, então não serve aqui, mas mostra que a comunidade aceita esse tipo de upscale.
- **Emuladores de Xbox (xemu, Cxbx-Reloaded):** sem troca de texturas.
- **Referências de troca de texturas que valem copiar:**
  - **RT64**, o renderer do Zelda 64: Recompiled (D3D12/Vulkan): identifica cada textura pelo XXH3 dos bytes da memória de textura e usa um banco `rt64.json` com hash e caminho. Prefere DDS/BC7 (PNG só durante o desenvolvimento) e carrega em segundo plano, com um cache de mipmaps baixos. Uma textura maior corrige as coordenadas pela razão entre os tamanhos. Os pacotes `.rtz` são um zip.
  - **Dolphin:** textura identificada por hash, exportação e carga de texturas, e pacotes de fontes HD feitos pela comunidade.
  - **Ship of Harkinian** e **Zelda 64: Recompiled:** recebem pacotes da comunidade, inclusive fontes e HUD em alta resolução, sem mudar o código do jogo.

Para este port, isso indica:

1. **Fonte:** gerar um atlas `mojist_e` 4× a partir de uma fonte livre não encontra concorrente nem conflito, e cabe no repositório.
2. **Troca de texturas:** seguir o modelo do RT64. Hash (XXH3) dos dados da textura do Xbox, banco JSON, DDS/BC7 e coordenadas escaladas pela razão dos tamanhos. Assim os pacotes da comunidade, inclusive de upscale por IA, ficam possíveis sem nada da Capcom no repositório.

Fontes: [RT64](https://github.com/rt64/rt64), [RT64 texture packs](https://github.com/rt64/rt64/blob/main/TEXTURE-PACKS.md), [pacote 4K do remaster](https://www.dsogaming.com/mods/onimusha-warlords-just-got-a-must-have-4k-texture-pack), [issues do xemu](https://github.com/xemu-project/xemu/issues?q=texture+replacement), [Dolphin #108](https://bugs.dolphin-emu.org/issues/108), [mod de HUD do Zelda 64: Recompiled](https://thunderstore.io/c/zelda-64-recompiled/p/Linkzenic/HD_Style_Controller_HUD/v/0.9.0/), [Ship of Harkinian](https://gamingonlinux.com/2023/03/ship-of-harkinian-a-pc-port-of-ocarina-of-time-has-a-feature-filled-upgrade).

## Pendente

- Capturar outras telas com texto (diálogo, inventário, status) para ver se usam a mesma fonte e o mesmo desenho.
- Decodificar `FILE**`, `*_title`, `*_tegami` e `*_gm_ini`.
- Achar a função que monta os quads dos caracteres (caractere → célula, sombra, escala).
