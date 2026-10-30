# Quadro destoante no céu/fumaça da Room 001

Investigação de 30/10/2026, USA retail Xbox, pack HD local de Evgeshajk.
O relato foi feito na Room 001, câmera 4. A captura do renderer identificou o
atlas convidado em `0x82180000`, hash `101C6E396FD08F41`, e o fundo estático
`64B9E69FD534BD08`. A captura reduz texturas HD ao tamanho convidado para salvar
os BMPs: o tamanho desses BMPs não comprova que o replacement esteja desligado.

## Relação com a correção anterior

O conversor já possui uma correção documentada no próprio `frame_images`: um
quadro do pack que não corresponde ao momento esperado da animação vira a média
dos dois vizinhos. O exemplo anterior é o quadro 24 da fumaça Warlords. O caso
atual tem o mesmo tipo de discrepância entre fonte e sequência nativa, mas a
queda de correspondência era menor que o limite anterior.

`xmov0014.xpr` (também presente em `xmov001.xpr`) armazena cinco atlas:

```text
0B7DD3CCB2100020
0282EB0BB82A3A80
046F45509B81D300
101C6E396FD08F41
5B76157820CEB790
```

Cada atlas tem 2048×256, com seis células úteis de 544×128. Os cinco formam uma
sequência de 30 quadros. Os replacements têm escala 6, 12288×1536 e 14 mipmaps.
O agrupamento, a ordem temporal e o ajuste de cor são do conversor existente.

## Evidência

No índice 18 da sequência reconstruída (base zero), a correspondência com o
quadro nativo caiu para `0,88753`; seus vizinhos ficaram em `0,95278/0,95211` e
a mediana da sequência em aproximadamente `0,953`. O critério anterior, mediana
menos `0,1`, não sinalizava nenhum quadro. Reconstruir os atlas sem mudar esse
critério continuava produzindo `weak_frames = 0`.

Foi registrada também uma sequência de 120 apresentações no jogo, sem quadros
perdidos pelo gravador. Essa gravação é evidência anterior à alteração; não
constitui uma comparação visual do jogo depois da correção.

## Ajuste no conversor

`tools/convert_anim_atlas.py::frame_images` mantém o critério anterior para
discrepâncias grandes. Para quedas menores, exige simultaneamente:

- desvio abaixo da mediana maior que `max(0,04; 6 × 1,4826 × MAD)`;
- queda em relação à média dos dois vizinhos maior que o mesmo limite.

A verificação robusta evita tratar uma sequência de correspondências ruidosas
como vários quadros defeituosos. A condição local evita confundir uma mudança
suave de fase com uma queda isolada. O tratamento continua restrito ao modo
`matched`; o modo `sequence` mantém a sequência de movimentos própria do Xbox.
Quadros adjacentes sinalizados continuam sem interpolação, pois os vizinhos não
são referências confiáveis. O contador `weak_frames` agora informa quantos
quadros foram efetivamente substituídos, em vez de contar candidatos que foram
deixados intactos por essa proteção.

## Validação

`python -I tools/test_anim_atlas.py` passou seis testes sem assets:
queda isolada da Room 001, discrepância grande anterior, fase suave, quadros
ruins adjacentes, fechamento do ciclo sem overflow de bytes e preservação dos
modos/entradas sem referências suficientes. Os testes também verificam que as
imagens de origem não são modificadas.

Os cinco DDS foram reconstruídos; o conversor passou a substituir somente o
índice 18. A comparação após decodificação e redução ao tamanho convidado mediu
diferença média absoluta RGB, na escala 0–255, sobre cada célula inteira:

| Transição | Instalado antes | Corrigido |
| --- | ---: | ---: |
| 17 → 18 | 0,53147 | 0,26611 |
| 18 → 19 | 0,51814 | 0,27082 |
| Máximo entre quaisquer quadros adjacentes | 0,53147 | 0,46499 |

A mudança média dos pixels dos outros 29 quadros foi `0,000049/255`. A pequena
diferença decorre do ajuste de cor compartilhado e da compressão; a sequência,
a quantidade de células e a cadência original não foram alteradas. Esses valores
avaliam os assets; não medem frametime nem demonstram por si só a percepção de
suavidade no jogo. O novo critério foi validado nesta sequência e nos casos
sintéticos, não em todas as animações do pack.

## Artefatos locais e instalação

Em `out/room001-sky/`: `analysis.json`, `rebuild-plan.json`,
`rebuild-result.json`, `validation.json` e `frame18-before-after.png` preservam
os parâmetros, métricas e hashes antes/depois. Os cinco DDS anteriores estão em
`backup-before-fix/`; os reconstruídos em `rebuilt/`. Os DDS corrigidos foram
copiados para `out/build/release/textures`, com validação de dimensões e mipmaps.

Reiniciar o jogo é necessário para recarregar arquivos que já estão nos caches
CPU/GPU. O ZIP público de texturas não foi atualizado por esta investigação.
