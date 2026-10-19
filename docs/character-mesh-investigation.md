# Deformação da malha do personagem — investigação de 19/10/2026

Status: causa confirmada e correção validada na reprodução capturada e na cena inicial, em 640×480 (4:3).

## Correção confirmada

A cinemática inversa em `0xEF9D0`, chamada pela atualização de animação `0xF12F0`, usa `acos` em `0x17CBE0`. Esse código chama o classificador `0x17EC70` e depois `0x17CBFD`. O classificador compara o expoente com `0x7FF00000`; o segundo auxiliar consome imediatamente ZF. O recompilado mantém as flags em variáveis locais e não as publica no retorno do classificador. Portanto, um ZF antigo igual a 1 envia argumentos finitos para o caminho reservado aos valores especiais. No caso reproduzido, esse caminho retorna `1+x` em vez de `acos(x)`.

O hook em `src/game/math_flags.cpp` mantém a implementação original do classificador e publica as cinco flags do seu último CMP no contexto. Não modifica o renderer, os shaders, a malha ou os pesos.

### Validação

- Na primeira chamada capturada de `0x17CBE0`, o recompilado sem correção retornou **1,986844958493865**; o original em Unicorn retornou **0,16238217657956247**. Após a correção, ambos retornaram **0,16238217657956247**, e os registradores gerais também coincidiram.
- A reprodução da atualização completa `0xF12F0`, com retorno em `0x10E368`, passou de 133 divergências relevantes para **zero**. A comparação ignora a área temporária da pilha e diferenças finitas de float até `1e-5 * max(1, abs(original))`; portanto não afirma igualdade de todos os bits.
- Capturas após a correção: `out/build/release/captures/shots/fixed-moving.png` e `fixed-walk-side.png`. A perna alongada da captura `native-4x3-analysis.png` desapareceu; o personagem também foi observado virando durante o jogo.
- Compilação Release concluída. A instrumentação temporária de replay e os wrappers de rastreamento foram retirados da compilação normal.

Reprodução local preservada em `out/build/release/animation-repro-before.bin` e `animation-repro-after.bin`; saída corrigida em `fixed_after.bin`. A ferramenta exploratória `out/compare_replay.py F12F0 fixed` executa o código x86 original sobre o estado salvo e compara com essa saída. As dependências locais de diagnóstico são Unicorn e NumPy. Esses arquivos grandes são locais e ignorados pelo Git.

A skill REA foi lida de `.local/skills/reverse-engineer-anything/SKILL.md` (versão 42). O diagnóstico `rea-agents 6.7.0 doctor --client codex --json` apontou ausência de registro MCP e de provedor nativo configurado. Nenhuma configuração global foi alterada. A evidência acima vem de Capstone, Unicorn, snapshots e capturas locais; não foi emitida por um servidor REA.

Escopo: foi validado este defeito na cena inicial e no estado capturado, sem um teste completo de todas as fases do jogo.

## Evidências locais

- Captura do executável original desta sessão: `out/build/release/captures/shots/baseline.png`.
- Conversão de vértices em CPU: `out/build/release/captures/shots/cpu3.png`.
- Renderização síncrona: `out/build/release/captures/shots/sync7.png`.
- Reconstrução dos triângulos em CPU, usando os vértices e as constantes capturados: `out/build/release/captures/shots/software.png`.
- Registros completos de chamadas de desenho: `out/build/release/captures/122049/frame.txt` e `124020/frame.txt`.
- Capturas binárias de malhas: `out/build/release/mesh_*.bin`; análise exploratória: `out/inspect_mesh.py` e `out/mesh-report.txt`.

Esses arquivos são diagnósticos locais, ignorados pelo Git. Os resultados abaixo documentam a investigação anterior à correção.

## Resultados

1. A deformação permanece com `XBE_RAW_VERTICES=0`. A conversão de entrada da GPU, isoladamente, não explica o defeito observado.
2. A deformação permanece com `XBE_RENDER_THREAD=0`. Desabilitar a thread de renderização não resolveu o problema na cena testada.
3. Nas amostras analisadas, os índices dos ossos apontam para grupos de quatro constantes válidos. O salto sobre as constantes reservadas do viewport também aparece nos índices e no código do jogo; não deve ser removido sem evidência adicional.
4. A reconstrução em CPU reproduz a geometria deformada usando as matrizes capturadas. A investigação seguinte deve relacionar a paleta enviada para cada parte da malha com os nós e matrizes de origem.
5. Comparações pontuais com Unicorn das rotinas `0xEEC80`, `0xE3330`, `0xEEAD0` e `0xEF7B0` não mostraram divergências nas saídas de animação inspecionadas. Foram observadas diferenças em contadores/temporizadores atualizados pelo runtime. Isso não comprova a correção de todo o sistema de animação: cada teste cobre apenas uma chamada e seu estado de entrada.

## Hipótese investigada antes da localização da causa

Capturar, para o personagem controlado e para um quadro estável, a relação completa entre:

- os bytes de remapeamento usados no laço `0xE9A90`;
- os nós do esqueleto em `[actor + 0x8CC]`;
- as matrizes de skinning em `[actor + 0x8EC]`;
- as quatro constantes enviadas por `0xE9AC6` para cada entrada da paleta;
- os vértices que usam a entrada associada à perna deformada.

Isso permite distinguir dados já incorretos na origem de um remapeamento incorreto durante o envio da paleta. Não aplicar limites artificiais aos vértices ou substituir matrizes por identidade: essas medidas apenas ocultariam o defeito e alterariam a animação.

## Cuidados com o teste diferencial existente

Executar Python com `-P` evita que `tools/dis.py` oculte o módulo padrão `dis` importado pelo Unicorn. O formato atual do dump não salva todo o estado de x87/SSE; ele só é suficiente quando a rotina testada não depende desses registradores na entrada. A tolerância atual também pode ocultar diferenças de inteiros interpretados como floats muito pequenos; a inspeção desta sessão incluiu uma comparação mais estrita desses valores.

As alterações experimentais em shaders, conversão de vértices, renderização e hooks foram retiradas da compilação normal ao encerrar esta investigação.
