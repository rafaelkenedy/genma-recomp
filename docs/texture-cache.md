# Cache e pré-carregamento de texturas HD

Implementação de 30/10/2026, a partir da investigação em
[camera-transition-streaming.md](camera-transition-streaming.md).

## Comportamento implementado

- Cache CPU de até **512 MiB de payload**, indexado pelo arquivo escolhido do
  pack. As imagens são imutáveis; leitores simultâneos do mesmo arquivo
  compartilham a carga. LRU remove entradas antigas, mas leases ativos mantêm
  os dados vivos durante um upload. Falhas e imagens maiores que o limite não
  ficam permanentemente no cache.
- Cache D3D12 de até **256 MiB de alocações retidas pelo cache**, por hash de
  conteúdo e assinatura da variante de paleta. Uma câmera já carregada pode
  reutilizar diretamente sua imagem na GPU, evitando outra leitura e upload.
  Isso não é um limite para toda a VRAM: texturas ainda usadas pelo jogo e
  recursos em voo mantêm suas referências até a retirada segura pelo frame.
- Imagens `.idx.png` continuam seguindo a paleta viva. Seus índices podem ser
  mantidos no cache CPU, mas a imagem expandida fica mutável e fora do cache
  GPU de imagens imutáveis. Antes de escrever uma imagem mutável, qualquer
  recurso imutável compartilhado é destacado. O worker possui sua própria
  fábrica WIC, sem compartilhar interfaces entre threads.
- Notificações opcionais de abertura de arquivos alimentam um worker que
  indexa contêineres BGS/XPR da pasta `BGS`, usando os mesmos hashes do renderer.
  Leituras antecipadas usam handles próprios: não alteram posição, status,
  evento ou conclusão de I/O do convidado.
- A pré-carga espera duas apresentações após a abertura para priorizar o frame
  da câmera atual. Ler todas as outras imagens imediatamente competia com a
  carga inicial nos testes. D: e sua cópia Z: com o mesmo nome de sala não
  cancelam uma à outra. Uma sala diferente cancela trabalho obsoleto entre
  leituras individuais; filas e índices também têm limites.
- A seleção nativa por câmera agora prioriza as faixas BGS do fundo atual e
  dos destinos associados, incluindo aliases por progresso do jogo e vizinhos
  já residentes. Pedidos antigos são cancelados entre leituras. Sem seleção
  válida, permanece a pré-carga ampla; animações XPR mantêm seu próprio caminho.
- As imagens CPU do fundo atual recebem proteção contra evicção especulativa,
  respeitando os mesmos 512 MiB. Se não houver espaço sem remover uma imagem
  protegida, a nova imagem é entregue sem retenção. Não há leases permanentes
  adicionais. Detalhes em [native-camera-streaming.md](native-camera-streaming.md).

Esta versão implementa cache e pré-carga, **não upload parcelado**. Misses de
CPU/GPU ainda podem executar leitura e upload síncronos. O prefetch por arquivo
requer assets extraídos acessíveis ao host; boot direto por ISO ainda recebe os
caches sob demanda, mas não essa pré-carga de contêineres. O backend Vulkan não
passa a suportar replacements HD por esta alteração.

O pack fica estável durante a execução; reiniciar após editar replacements.
Desligar HD interrompe novas solicitações de pré-carga e volta a usar imagens
nativas. Dados já retidos continuam limitados pelos caches. Os limites são de
payload/alocações retidas, não um teto para RSS ou memória transitória de leitura.

## Comparação automática

Windows, i7-4790K/RTX 3050, pack HD local, janela 1280×720, escala 100%, V-Sync
desligado, cap 60, cálculo original de acos e cache de shaders preparado.
Mesmo save e comandos de analógico, com logs de posição confirmando as câmeras
4 → 3 → 5 e as repetições. As execuções terminaram normalmente. Não é um replay
determinístico por frame e os intervalos são medidos no backend, não no monitor.

Runs locais: `texture-cameras-off` e `texture-cameras-priority`, em `out/perf/`.

| Medida | Caches desligados | Cache e prefetch |
| --- | ---: | ---: |
| Tempo CPU do caminho de texturas, 45–85 s | 139,50 ms | 12,12 ms |
| Maior intervalo, 45–85 s | 108,72 ms | 67,23 ms |
| p99, 45–85 s | 21,15 ms | 20,93 ms |
| Leituras de `2FA884467704CA4E.dds` na execução | 3 | 1 |
| Leituras de `0E6D94C7E19FCA8D.dds` na execução | 2 | 1 |
| Acertos do cache GPU, 45–85 s | 0 | 4 |
| Maior intervalo na carga inicial, 35–45 s | 139,98 ms | 151,51 ms |

O ganho é claro no trabalho repetido de texturas. Não demonstrou melhora na
primeira carga; picos de espera/sincronização permanecem. Uma variante anterior
que começava prefetch imediatamente chegou a 335 ms nessa carga e foi alterada.
O protótipo medido foi depois ajustado para manter imagens indexadas mutáveis;
uma rodada visual separada com essa proteção confirmou o percurso e a imagem,
mas não foi usada para substituir as medidas acima. Cargas frias também variaram
nessa rodada: não tratar os valores da tabela como garantia de latência.

Os primeiros testes com comandos UP/DOWN e um A adicional após o load não
confirmavam movimento. Os resultados de travessia acima usam a rota corrigida
e as posições/IDs registrados, não aqueles testes iniciais.

## Diagnóstico e testes

`XBE_TEXTURE_CACHE=0` desliga as três partes para comparação na mesma build;
sem a variável, ficam ligadas. O script local aceita `-NoTextureCache`:

```powershell
.\start.ps1 -Trace
.\start.ps1 -Trace -NoTextureCache
```

A CSV inclui `hd_gpu_cache_hits`. O log verboso registra leituras reais feitas
pelo loader e resumos de prefetch com bytes, hits, misses e evicções do cache CPU.

`test_resource_cache` verifica orçamento, LRU, leases, falhas/retries, coalescing
e cargas independentes concorrentes. Também verifica que a falha de um observer
não altera o sucesso da abertura convidada. `test_xpr_index` cobre contêineres
concatenados, limites, formatos não suportados e truncamento. Os dois foram
incluídos no CTest e na esteira Windows. Onze testes Linux e os testes Windows
de cache, relógio e controles passaram; o runtime Linux compilou.

## Sobre a tela preta “Now Loading”

O host não adiciona essa tela nas trocas de sala: ela é desenhada pelo código
original. Esta alteração reduz trabalho dos replacements, mas não desativa a
máquina de estados de carga, fades ou esperas originais. Ainda não foi
identificado um flag/timer confiável para eliminar a tela especificamente.

É possível tratar a apresentação separadamente, mantendo uma cópia do último
quadro completo até o primeiro quadro válido da sala nova. Isso evitaria o
preto, mas preservaria o tempo de espera. Exige identificar entrada e saída do
estado de carga; heurísticas baseadas apenas em imagem escura também atingiriam
fades e cenas legítimas. Não implementar esse reconhecimento por suposição.

Para reduzir a espera de fato, a próxima investigação precisa medir o estado de
carga original, os arquivos requeridos e os eventuais tempos mínimos. Como
precedente, o autor do [Parasite Eve II HD Remaster](https://github.com/faligame/Parasite-Eve-2-HD-Remaster)
relata reduzir “Now Loading” de cerca de 8 para 3 segundos. É um relato do
projeto, não uma técnica verificável: seu repositório não publica código-fonte.
