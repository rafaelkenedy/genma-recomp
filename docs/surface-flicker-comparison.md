# Comparação das superfícies dos personagens — 19/10/2026

O usuário confirmou que a oscilação permanece visível nas superfícies dos personagens, após a correção da importação de profundidade. Portanto, essa correção não deve ser apresentada como resolução confirmada deste efeito residual.

## Capturas

Foram capturadas sete imagens em sequência, sem reiniciar o jogo ou alterar seus materiais:

| Imagem | Pasta da captura |
| --- | --- |
| surface-00.png | 140810 |
| surface-01.png | 140821 |
| surface-02.png | 140823 |
| surface-03.png | 140825 |
| surface-04.png | 140827 |
| surface-05.png | 140828 |
| surface-06.png | 140830 |

Imagens completas: `out/build/release/captures/shots/`. Ampliações de quatro vezes, por vizinho mais próximo: `out/surface-comparison/hero-sequence.png` e `npc-sequence.png`. Os recortes preservam a mesma região da tela em todas as amostras, sem alinhar ou interpolar o movimento dos modelos.

Estas são amostras separadas por segundos, não uma gravação de todos os frames a 60 Hz. A captura interna sincroniza a GPU e pode alterar o timing do defeito. A tentativa de captura da janela via Windows.Graphics.Capture falhou por timeout, incluindo a repetição após atualizar/ativar a janela.

## Comparação

- As sete capturas têm 75 chamadas de desenho. As texturas dos personagens e todos os shaders capturados têm conteúdo idêntico entre as amostras.
- Os estados de desenho permanecem iguais, exceto pela contagem dos índices do volume de sombra: 1.650 para 1.656 nas últimas duas amostras. Isso também pode ocorrer com a animação, conforme a silhueta muda.
- Os modelos permanecem presentes e proporcionais. As imagens mostram alterações de tons e de pose em repouso, sem perda integral da geometria.
- No recorte do protagonista, 562 pixels variam mais de 20 níveis RGB ao longo da sequência; no outro personagem, 1.033. Esses números incluem movimento e iluminação e **não são uma medida isolada do flicker**.
- As amostras não demonstram troca de textura ou shader. Conflito de profundidade entre sombra e superfície e sincronização do buffer dinâmico de sombra continuam como hipóteses, sem correção adicional aplicada neste levantamento.

Dados: `out/surface-comparison/report.json` e `states.json`. Scripts exploratórios: `out/compare_surfaces.py` e `compare_surface_states.py`.

## Limites das hipóteses

A revisão da sincronização encontrou espera de recurso no lock normal dos buffers; não foi demonstrada uma corrida entre escrita e renderização. Uma projeção exploratória encontrou profundidades do volume de sombra e do corpo separadas por poucos níveis D24, compatíveis com conflito de profundidade, porém as matrizes de `frame.txt` são impressas com seis dígitos significativos. Essa perda de precisão impede usar essa proximidade como prova. Nenhum novo ajuste de bias ou sincronização foi aplicado com base nessas hipóteses.

## Vídeo enviado pelo usuário

Arquivo: `C:/Users/krafa/Downloads/WhatsApp Video 2026-10-19 at 14.14.55.mp4`. Foram decodificados 251 quadros, a 30 fps, em 1024×576, com duração de 8,367 s. A imagem foi girada 90 graus para a comparação, pois a filmagem está lateral.

Foram examinadas duas sequências de 16 quadros consecutivos, espaçados em 33,3 ms: NPC de 1,500 a 2,000 s e protagonista de 6,667 a 7,167 s. Painéis em `out/video-flicker/consecutive-npc.png` e `consecutive-hero.png`.

Uma análise independente compensou translação, ganho/offset de brilho e padrões finos com filtro gaussiano. O resíduo mediano no torso ficou em 2,59/255 contra 0,77/255 no fundo para o NPC; no protagonista, 2,21/255 contra 0,87/255. A variação local não é explicada apenas por uma mudança global de exposição. Contudo, ela acompanha também detalhes, bordas e movimento da animação. A filmagem da tela e a compressão limitam a atribuição: não foi isolado um triângulo inteiro alternando de estado ou uma causa específica no renderer.

Os dados estão em `out/video_math_residuals.json`; script `out/video_math_residuals.py`. Esta análise do vídeo não modificou o jogo.
