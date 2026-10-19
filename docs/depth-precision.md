# Cintilação nos personagens: precisão da profundidade — 19/10/2026

## Sintoma

Com o personagem parado (no pátio, por exemplo), pontos do rosto, do pescoço, das bordas da armadura, da mão e da espada alternam entre claro e escuro de um quadro para outro. No xemu o mesmo ponto apenas cintila de leve.

## Causa

**Não é a sombra.** Foram testados vários caminhos, e nenhum mudou a cintilação:

- reescrever o construtor do volume de sombra com histerese (`docs/shadow-volume-algorithm.md`);
- aplicar um bias grande no volume;
- desligar a sombra inteira (`XBE_NO_SHADOW`, `skip_draws`).

Também foram revisados e estão corretos:

- as constantes dos ossos, que chegam pelas HLE e são copiadas por inteiro pela thread de render;
- o skinning de 3 ossos;
- a decodificação de NORMPACKED3;
- as texturas e os estados, que não mudam entre quadros.

**A causa é a projeção do jogo.** A linha z da matriz é quase igual à linha w, ou seja, z ≈ w − 1:

```
c-82  0.870505 0.0554569 -0.489029 13193.5   (z)
c-81  0.870503 0.0554568 -0.489028 13194.5   (w)
```

z e w saem de dois `dp4` com parcelas da ordem de 10⁴, que se anulam. Em float32, cada um carrega um erro de uns 10⁻³. Por isso z/w tem algo como ±10 degraus de 24 bits de ruído, e nessa distância 1 unidade de mundo vale cerca de 2 degraus. Camadas do mesmo modelo a poucas unidades uma da outra (gola e pescoço, armadura e corpo, mão e cabo) trocam de vencedor no teste LEQUAL conforme a respiração da animação muda os arredondamentos. O Xbox faz a mesma conta, daí a cintilação leve no original. No port ela fica mais visível.

## Correção (runtime, `gpu/shaders.cpp`, `VertexProgramHlsl`)

1. `FindDepthRows` acha, no programa, os `dp4` com constante que escrevem `oPos.z` e `oPos.w` (direto ou por R12).
2. A tradução guarda as fontes desses `dp4` (`dsz`, `dsw`).
3. Antes do epílogo, se as fontes são iguais e o programa usa o mapeamento c-38/c-37 do XDK, a profundidade é recalculada assim:

   ```
   dd = dot(fonte, c[w] - c[z])                  // w - z; a diferença das linhas é exata (Sterbenz)
   oPos.z = (1 - dd / w) * c[58].z + c[59].z     // o mesmo que (z / w) * escala + offset
   ```

O ruído cai para cerca de um degrau. Os shaders de skinning do herói e o do volume de sombra entram nessa regra.

**Chave:** `DrawConstants::dfix.x`. Fica ligada por padrão (`g_precise_depth` em `d3d12.cpp`). `XBE_PRECISE_DEPTH=0` desliga, e um arquivo `precise_depth` ao lado do exe, contendo `1` ou `0`, troca ao vivo.

## Medição

O gravador (`record` → `record.raw`) salvou 90 quadros consecutivos com o herói parado no pátio. O script exploratório conta os pixels cujo brilho sobe e desce mais de 12 níveis em quadros seguidos.

| | Pixels que alternam ≥ 3× | Alternâncias por quadro |
| --- | --- | --- |
| Desligada | 220 | 20,8 |
| Ligada | 29 | 2,8 |

O usuário confirmou visualmente que a cintilação foi corrigida.

## Risco

Desenhos que dependem de profundidade igual entre passadas (EQUAL/LEQUAL na mesma geometria) continuam iguais entre si quando todos usam um programa com o mesmo padrão. Uma passada por função fixa (FVF) sobre a mesma geometria de um programa ficaria com profundidade calculada de outro jeito. Não foi observado no jogo, mas, se algo sumir ou piscar em passadas múltiplas, compare com `precise_depth` = 0.
