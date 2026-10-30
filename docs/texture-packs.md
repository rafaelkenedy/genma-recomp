# Pacotes de texturas HD

Como converter um pacote de texturas feito para outra versão do jogo (por exemplo, o "4K Texture Pack AI-Enhanced" de
Onimusha: Warlords, de Evgeshajk) para a troca de texturas deste port. As texturas convertidas ficam no seu PC: nada
derivado de um pacote entra neste repositório ou nas releases.

A conversão pronta do pacote de Evgeshajk está em [GenmaOnimusha-HD-textures.zip](https://archive.org/details/genma-onimusha-hd-textures) (cerca de 9,9 GB): extraia na
pasta `game` da release, para os arquivos ficarem em `game/textures/`.

## Como o port acha uma textura HD

- Cada textura do jogo é identificada pelo hash dos bytes do nível 0 (`gpu::Hash`, `sdk/runtime/gpu/replace.h`).
- Ao lado do exe, `textures/<hash>.dds` (BC1/2/3/7, com mipmaps) ou `textures/<hash>.png` substitui a textura, em
  qualquer tamanho: as coordenadas são normalizadas.
- Uma textura P8 (paleta) usa `textures/<hash>.p<assinatura>.dds`, uma por paleta, porque o jogo desenha os mesmos
  índices com paletas diferentes (as almas, por exemplo).
- A pasta é lida quando o jogo abre: depois de converter, reinicie o jogo. *F1 → HD textures* liga e desliga ao vivo.

## Ferramentas

| Script | O que faz |
|---|---|
| `tools/dump_game_textures.py` | Decodifica as texturas dos cenários (`image/bgs/*.bgs`, `*.xpr`) para `<hash>.png`, sem precisar jogar até cada sala |
| `tools/xbe_hash.py` | O hash do runtime em Python, e a lista das texturas de um arquivo do jogo |
| `tools/convert_texture_pack.py` | Casa cada textura com a imagem mais parecida do pacote, ajusta as cores às do Xbox e grava DDS com o texconv |
| `tools/convert_anim_atlas.py` | Remonta as animações (fumaça, água) que o Xbox guarda como atlas de quadros, a partir dos quadros soltos do pacote |

## Passo a passo

```
python -I tools/dump_game_textures.py <pasta do jogo> <dump>
python -I tools/convert_texture_pack.py <dump> <pacote>/SK_Res/inject/textures <exe>/textures --texconv texconv.exe
python -I tools/convert_anim_atlas.py <dump> <pacote>/SK_Res/inject/textures <exe>/textures --texconv texconv.exe --game <pasta do jogo>
```

- `<pasta do jogo>` é a que tem `image/` (os arquivos extraídos do disco).
- O texconv é o do DirectXTex (Microsoft). Sem ele, o conversor grava PNG.
- Os relatórios ficam em `textures/pack_report.csv` e `textures/anim_report.csv`.
- Não rode a `dxgi.dll` que vem com pacotes do Special K: só os arquivos DDS são lidos.

## O que esperar

- **Cenários:** o Xbox repintou e reiluminou parte dos cenários do Warlords. Os que batem acima de `--min-score` são
  convertidos direto. Os parecidos (mesma geometria, outra pintura) precisam de um `--min-score` menor e de uma olhada
  lado a lado antes. Com o pacote de Evgeshajk, cerca de 1.180 dos 1.284 cenários ganham versão HD.
- **Animações:** o pacote guarda um quadro por arquivo, o Xbox junta vários quadros num atlas, às vezes dividido em
  vários atlas seguidos (a água do rio são 4). O script acha a grade, ordena os quadros do pacote no tempo e os encaixa
  nos do jogo. Quadros do pacote que destoam viram a média dos vizinhos, para não piscar.
  A detecção também cobre quedas isoladas menores de correspondência, com um
  limite robusto para preservar mudanças suaves. O caso do céu/fumaça da Room 001
  e os testes estão em [smoke-animation-frame-outliers.md](smoke-animation-frame-outliers.md).
- **Cores:** cada textura recebe um ajuste linear por canal para ficar com o tom da versão Xbox.
