# Trocas de câmera: pesquisa e diagnóstico de streaming

Pesquisa de 29/10/2026. Escopo: explicar as pausas nas trocas de câmera dentro
de uma sala e escolher soluções documentadas aplicáveis ao port. Esta nota não
implementa um novo carregador nem modifica o tempo das transições do jogo.

## Evidência do teste real

Foram examinados `out/build/release/logs/genma_115.log` e
`frames-original-20261029-232519.csv`, gravados no teste do usuário com o cálculo
original. O log identifica saída 3840×2160, escala 100%, V-Sync ligado e HD ligado.
O renderer preparou 44 pipelines antes de jogar.

No trecho posterior a 45 segundos da captura:

| Intervalo do frame | Caminho de texturas no CPU | Upload total do frame | Compilação HLSL / criação de PSO |
| --- | --- | --- | --- |
| 87,74 ms | 58,61 ms | 56,17 MiB | 0 / 0 |
| 83,36 ms | 51,70 ms | 57,88 MiB | 0 / 0 |
| 78,86 ms | 48,31 ms | 45,94 MiB | 0 / 0 |
| 64,34 ms | 47,39 ms | 50,11 MiB | 0 / 0 |

`texture_ms` abrange leitura/preparo/upload no caminho do renderer, não apenas
tempo de disco nem tempo de execução da GPU. `upload_mib` inclui todos os uploads
do frame, não só fundos HD. A captura mede intervalos entre apresentações
concluídas no backend, não frames exibidos medidos por PresentMon. O relógio do
CSV começa na primeira apresentação e difere do relógio do log.

Há aberturas repetidas de `ROOM001.BGS` e de `xmov001*.xpr`. O log também mostra
replacements BC1 de 6144×6144, 12288×6144 e 12288×1536. Arquivos verificados:

- `3D597680438DA812.dds`: 50.331.808 bytes, aproximadamente 48 MiB.
- `3419F7F56850196A.dds`: 25.165.976 bytes, aproximadamente 24 MiB.

Parte dessas imagens são **atlases de animação**, não fotografias únicas de
câmera. `tools/convert_anim_atlas.py` documenta a reconstrução de vários quadros
em uma textura. Portanto, limitar todos os fundos ao tamanho da janela sem
considerar as células do atlas pode reduzir indevidamente a qualidade.

O utilitário existente `tools/xbe_hash.py` identificou 11 texturas DXT1 em
`ROOM001.BGS`. As 11 têm DDS instalados, somando **72,19 MiB**. Para
`xmov0012.xpr`, os três hashes encontrados correspondem aos atlases HD de
24 MiB registrados no log. Isso confirma a viabilidade de gerar um manifesto
local de arquivos de sala → hashes de texturas. Esse total não inclui todas as
animações, personagens, efeitos, profundidade ou overhead da GPU.

## Onde o código acrescenta trabalho ao corte

1. `BindGuestTexture` chama `LoadReplacement` quando detecta alteração nos dados
   convidados; esse caminho roda no renderer.
2. `ReadDds` abre e lê o arquivo inteiro, depois copia os mipmaps para vetores
   separados. DDS BCn permanece comprimido; não há decodificação RGBA nesse caso.
3. Não há cache CPU desses replacements: a própria descrição em `replace.h`
   informa que são lidos no upload e descartados.
4. `UploadReplacement` pode criar um novo recurso e copiar todos os mipmaps no
   mesmo frame. O cache de texturas está associado ao endereço convidado e à
   paleta; não é um cache global de imagens HD imutáveis por conteúdo.
5. O ring de upload tem 64 MiB por frame. Excesso gera buffers próprios e novas
   alocações. O tamanho dos assets torna isso um ponto a medir, mas os dados
   acima não provam que todo pico passou pelo caminho de overflow.

`kernel/files.cpp::Transfer` também executa as leituras convidadas de forma
síncrona. Ainda não temos duração por arquivo/offset para separar seu custo do
carregamento HD. Abrir um BGS no log não prova que essa abertura seja a causa.

## Soluções verificadas em outros projetos

### Dolphin: pré-carregar replacements antes do primeiro uso

O [relatório oficial de maio de 2015](https://dolphin-emu.org/blog/2015/06/01/dolphin-progress-report-may-2015/#4.0-6415-preload-custom-textures-by-degasus)
descreve travadas ao carregar texturas personalizadas e a introdução de
`Prefetch Custom Textures`, que antecipa carregamento/preparo em RAM.
Aplicação proposta: antecipar os fundos da sala, com limite de memória; o pack
local de aproximadamente 23 GB torna inadequado presumir que todo ele cabe em RAM.

### PCSX2: cache CPU, worker e publicação posterior

O [código oficial de GSTextureReplacements](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSTextureReplacements.cpp)
consulta um cache antes do disco. Em modo assíncrono, enfileira a leitura e retorna
um estado pendente; `ProcessAsyncLoadedTextures` publica os replacements prontos
no cache de texturas. Também há precache. Para nós, isso é referência para separar
I/O de renderização e notificar o renderer quando uma imagem fica pronta.

### Unity: buffer reutilizável e orçamento de upload por frame

A [documentação do pipeline assíncrono](https://docs.unity.com/en-us/engine/6000.7/manual/analysis/graphics-performance-profiling/loading-texture-mesh-data-asynchronously/configure-asynchronous-upload-pipeline)
explica um ring buffer reutilizado e um limite de tempo de upload por frame.
Aplicação proposta: distribuir cópias grandes, inclusive por faixas de linhas;
apenas testar o orçamento antes de copiar uma textura inteira não limita um
upload individual de dezenas de MiB.

### Unreal: resolução necessária, prioridades e orçamento de memória

A [visão geral oficial de texture streaming](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine)
descreve seleção de mipmaps conforme a visão, prioridades de retenção e um pool
de memória. Para nós: manter a câmera atual e suas prováveis vizinhas prontas,
com cache limitado em bytes, e avaliar mipmaps adequados ao tamanho real das
células de atlas. RAM e VRAM precisam de limites separados.

### Godot: background loading só ajuda quando existe antecedência

A [documentação oficial](https://docs.godotengine.org/en/stable/tutorials/io/background_loading.html)
alerta que obter um recurso ainda pendente bloqueia o chamador. Assim, iniciar
uma thread no instante do corte e esperar imediatamente por ela preservaria a
travada. Precisamos solicitar antes ou usar o fundo nativo correto enquanto o
HD termina.

### DirectStorage: opção posterior, não substituto da organização do streaming

A [orientação oficial da Microsoft](https://github.com/microsoft/DirectStorage/blob/main/Docs/DeveloperGuidance.md)
documenta filas, layouts de upload e buffers de staging; filas cheias podem
bloquear. Avaliar depois de medir I/O. Mudar apenas a API de leitura não elimina
releituras, cópias, alocações e uploads concentrados no corte, e exigiria um
caminho alternativo para o Linux.

## Proposta de implementação, por prioridade

1. **Medir o corte completo.** Registrar mudança de camera ID e sala, arquivo/
   offset/bytes/duração de leitura, tempo de parse e cópia, criação de recurso,
   espera e primeiro frame com o novo fundo. Capturar os IDs no batch convidado,
   antes de enviá-lo ao renderer, para não associar uma câmera nova a um frame
   antigo que ainda estava na fila.
2. **Cache CPU limitado e prefetch por sala.** Usar os hashes existentes para
   antecipar BGS; mapear também os XPR de animação com referências verificadas ou
   associações aprendidas no runtime, sem presumir nomes por prefixo. Priorizar
   câmera atual, vizinhas e próximos atlases. Um cache evita releitura ao voltar;
   prefetch com antecedência ajuda no primeiro corte.
3. **Worker e publicação sem espera no draw.** Ler/preparar fora do renderer.
   Se o HD não estiver pronto, desenhar o fundo nativo **da câmera nova**, nunca
   manter o fundo de outra câmera com geometria/profundidade novas. A conclusão
   deve invalidar/publicar a textura: hoje gravar `tx.sig` após fallback pode
   impedir uma nova tentativa até que o conteúdo convidado mude.
4. **Upload parcelado e cache GPU por conteúdo.** Reutilizar recursos HD prontos
   por hash + assinatura de paleta + geração do pack + nível de qualidade. Manter
   o recurso em preparo até a conclusão, respeitar fences e só então publicá-lo.
   Reutilizar buffers e limitar bytes e trabalho por frame, em vez de aumentar
   indiscriminadamente o ring.

Cache CPU sozinho não resolve um primeiro upload grande. Worker sozinho não
resolve se o render thread esperar imediatamente. Ambos precisam de antecedência
e de uma política de publicação. O cache deve tratar troca de pack, alteração de
paleta, HD desligado, cancelamento de sala e falhas de arquivo/alocação, sem
permitir que um job antigo substitua uma câmera mais nova.

## Validação proposta

Comparar o mesmo trajeto entre duas câmeras da Room 001, ida e volta, incluindo
as animações. Repetir com HD desligado, HD síncrono atual e HD com prefetch/cache;
separar primeira visita de repetição. Fixar resolução, V-Sync e limite de FPS.
Medir tempo camera ID → primeiro frame correto, p95/p99/máximo de frametime,
leituras repetidas, bytes por upload, RAM/VRAM e acertos de cache. Verificar
oclusão, profundidade, transparência, paletas e fase das animações.

A meta inicial é remover I/O e uploads grandes do instante do corte, não encurtar
arbitrariamente a transição original. Se o atraso permanecer com HD desligado e
sem stalls, investigar separadamente os gatilhos, fades e esperas do código
convidado. Os registros atuais sustentam a prioridade do streaming, mas ainda
não medem toda a latência da mudança de câmera nem descartam outras causas.

## Sessão interativa adicional: 23h50–23h54

O usuário jogou até encerrar normalmente a execução (`Exit (0)` em 224,144 s).
Arquivos: `genma_116.log` e `frames-original-20261029-235002.csv`, no mesmo
diretório de logs/capturas da build local. A configuração manteve acos original,
3840×2160, escala 100%, V-Sync e HD ligados. A captura contém 13.340 frames e
chega a aproximadamente 224,131 s no relógio do log. O alinhamento usou o primeiro
resumo de performance e o primeiro frame que cruzou 1000 ms no CSV, resultando
em um deslocamento estimado de 590,639 ms; não é sincronização de hardware.

Após a primeira referência a BGS (21,517 s): média de 59,78 FPS, p99 de 20,33 ms,
28 intervalos acima de 33,334 ms e máximo de 127,62 ms. Não houve compilação HLSL
nem criação de PSO nesse trecho. A média próxima de 60 não elimina as pausas.

| Acesso ao BGS | Maior intervalo observado | Tempo de texturas no frame desse pico |
| --- | --- | --- |
| Primeira entrada na Room 002, 90,384 s | 117,09 ms | 74,81 ms |
| Acessos seguintes à Room 002 | 25,14–30,25 ms | variável |
| Primeira entrada na Room 003, 140,134 s | 127,62 ms | 82,67 ms |
| Acessos seguintes à Room 003 | 25,43–26,42 ms | variável |

Esses valores são máximos em janelas de até 750 ms após grupos de aberturas do
BGS, agrupando aberturas da mesma sala separadas por menos de 500 ms. São
comparações entre acessos a fundos da mesma sala, **não entre IDs de câmera
iguais**: a instrumentação atual não registra o ID por frame nem mede o atraso
exato do gatilho da câmera até o primeiro frame correto.

O pico de 127,62 ms fez 69,22 MiB de uploads totais, com 82,67 ms no caminho CPU
de texturas. Houve também um pico de 92,20 ms perto da abertura de item/inventário,
fora das janelas de BGS, com 74,87 ms de texturas e apenas 1,53 MiB de uploads;
portanto, bytes grandes não explicam todos os casos e o tempo de cada etapa
ainda precisa ser separado.

Releituras pelo loader foram verificadas para dois fundos de 3840×2688, cada
um com aproximadamente 6,56 MiB: `E7DB0D9C07F95B01.dds` apareceu aos 95,873,
101,193, 109,457 e 123,392 s; `78D2B7FFD834046C.dds` aos 99,924, 103,009,
115,007 e 137,040 s. O log prova repetição do caminho de carregamento; não prova
quatro acessos físicos ao disco, pois o sistema operacional pode servir cache.

Esta sessão reforça o plano de cache por conteúdo e prefetch, com upload
parcelado para as primeiras visitas. Não permite prometer eliminação dos stalls
nem atribuir todo atraso percebido à leitura HD sem o teste A/B e a marcação
explícita dos cortes propostos acima. A análise reproduzível local está em
`out/analyze-camera-session.cjs` e `out/camera-session-analysis.json`.
