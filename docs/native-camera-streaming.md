# Seleção nativa de fundos por câmera

Investigação e implementação de 30/10/2026, USA retail Xbox. A investigação do
[Warlords HD](onimusha-hd-loading-analysis.md) sugeriu procurar uma tabela ligada
à câmera. O Xbox **já possui** o mecanismo abaixo; não foi transplantado código
do remaster nem demonstrado que esse mecanismo nasceu na versão HD.

## Endereços verificados nas listagens geradas

| Endereço | Uso observado |
| --- | --- |
| `0x546AE8`, `0x546AEA`, `0x546AEC` | Stage, sala e câmera atuais, palavras de 16 bits. |
| `0x54344C` | Ponteiro para tabela: contagem de 32 bits seguida de registros de 24 bytes. |
| `0x1D840` | Reorganiza o cache nativo e enfileira fundos associados ao argumento de câmera. |
| `0x1D950`–`0x1DAAE` | Percorre os registros. Testa habilitação em `+0`, resolve origem em `+3` e, quando corresponde, considera destino em `+4`. |
| `0x11350` | Resolve câmera/fundo conforme progresso, stage e sala. Tem efeitos colaterais em alguns caminhos. |
| `0x29EDAF`, `0x29EDB4` | Contagem e IDs da fila nativa; não incluem necessariamente os vizinhos já residentes. |
| `0x54347C` | Diretório BGS com registros de 40 bytes, indexado pelo fundo resolvido. |
| `0x1D030` | Procura um fundo residente ou lê a faixa BGS: offset `+0`, tamanho `align2048(+4) + align2048(+12)`. |
| `0x1CF20`, `0x1BF70` | Encaminham essa faixa ao carregador de arquivos. |

As listagens locais são `generated/game/x86_recomp.0.cpp` e, para chamadas de
troca de câmera, `x86_recomp.14.cpp`. A captura real confirmou seleções para
câmeras 4, 3 e 5 da Room 005, além da correspondência das faixas com texturas
XPR completas dentro do BGS. Não extrapolar offsets para outra edição do XBE.

## Política implementada

`src/game/background_streaming.cpp` envolve a execução original de `0x1D840` e
observa resultados de `0x11350` nos retornos `0x1D8EB`, `0x1DA0C`, `0x1DA38`,
`0x1DA53` e `0x1DA82`. Exclui `0x1D991`, que resolve as origens de todas as linhas
da tabela, inclusive as que não correspondem à câmera. Assim, preserva aliases
por progresso e inclui destinos já residentes sem chamar novamente o resolvedor
nem alterar seu contexto convidado. IDs repetidos são deduplicados; a seleção
tem no máximo 17 IDs, atual mais até 16 associados. O escopo é restaurado também
se a rotina original lançar uma exceção no host.

O observador valida os endereços das entradas e limita as faixas ao teto do
indexador BGS, 64 MiB. Apenas comunica metadados ao SDK; não escreve na fila,
no diretório BGS, em flags ou em timers do jogo. A rotina original continua
decidindo movimento, fades, readiness e troca de câmera.

O worker mantém offsets dos níveis XPR e só solicita replacements cujos bytes
cabem integralmente nas faixas indicadas. Processa primeiro a faixa atual,
depois os destinos. Um contador de seleção cancela a especulação BGS anterior
entre imagens; a geração da sala ainda cancela trabalhos de outra sala. XPR
abertos para animações continuam independentes. Sem hints válidos, o caminho
amplo permanece disponível. `XBE_CAMERA_PREFETCH=0` permite comparação isolada.

A proteção de evicção é aplicada apenas aos arquivos CPU da primeira faixa,
sem aumentar o orçamento. O cache GPU já mantém referências dos recursos
atualmente usados; esta etapa não acrescenta upload antecipado/parcelado nem
uma nova política de proteção para todos os vizinhos na GPU.

## Diagnóstico

Novos campos CSV: `stage`, `room`, `camera`, `guest_read_ms`, `guest_read_mib`,
`hd_source_ms`, `hd_prepare_ms`, `hd_resource_ms` e `hd_copy_ms`. A identidade é
capturada no produtor junto ao Present e transportada na fila do renderer. Ela
identifica a câmera observada ao processar o pedido nativo, não uma detecção
visual do primeiro fundo correto.

`guest_read_ms` mede chamadas de leitura do convidado, incluindo seek e cópia
para memória, depois de obter o mutex do arquivo; inclui hits do cache do SO.
`hd_source_ms` mede a aquisição do replacement no renderer, incluindo lookup,
leitura/parse ou espera por uma leitura concorrente. `hd_prepare_ms` mede expansão
de paleta/mipmaps, `hd_resource_ms` criação do recurso D3D12 e `hd_copy_ms` preparo
de staging e registro das cópias. Todos são tempos CPU; não medem execução GPU
nem acesso físico ao disco. As leituras antecipadas do worker não são incluídas
em `hd_source_ms`, pois não bloqueiam diretamente o renderer.

## Comparação automática

Runs locais `out/perf/camera-table-off` e `camera-table-on`, 90 segundos cada,
720p, escala 100%, caches ligados, cap 60, V-Sync desligado e acos original.
A sequência registrada em ambas foi Room 005: 4 → 3 → 5 → 3 → 4 → 3 → 5.
O relógio CSV começa na primeira apresentação, separadamente do log.

| Medida | Prefetch amplo | Seleção por câmera |
| --- | ---: | ---: |
| Imagens selecionadas na primeira câmera 4 | 8 | 2 |
| Imagens selecionadas na câmera 3 | 8 | 4 |
| Payload CPU retido após o percurso | 398,7 MiB | 372,5 MiB |
| Aquisição HD no renderer, 45–85 s | 0,0064 ms | 0,0062 ms |
| Criação de recursos HD, 45–85 s | 2,7355 ms | 1,8734 ms |
| Preparo/registro de cópias HD, 45–85 s | 1,5698 ms | 1,3506 ms |
| Acertos GPU, 45–85 s | 4 | 4 |
| p99 de intervalo, 45–85 s | 22,53 ms | 20,24 ms |
| Maior intervalo, 45–85 s | 50,05 ms | 73,32 ms |

Esta única comparação sustenta menor trabalho especulativo e menor payload
retido; **não demonstra uma melhora geral de stuttering**. A ordem foi amplo
primeiro, seleção depois, sem limpar caches do sistema operacional e sem replay
determinístico por frame. O máximo piorou, enquanto p99 melhorou; não atribuir
essas diferenças à estratégia sem mais amostras. Pequenos testes sintéticos
também foram compilados durante parte da rodada ampla; os frametimes não são
uma comparação isolada de desempenho. A contagem de imagens e o payload do
cache são os resultados mais úteis deste par.

Nas primeiras trocas para 3 e 5, a aquisição HD já estava no cache em ambas as
estratégias. Criação mais cópia custaram cerca de 1,6–2,4 ms por troca na seleção
nova. Na entrada inicial, houve intervalos de 792,44/716,70 ms, com respectivamente
45,23/52,21 ms somados em aquisição, criação e cópia HD no frame do pico. As
leituras convidadas desse frame custaram 3,14/3,27 ms. Os instrumentos não explicam
o restante desses intervalos: não é correto atribuí-los todos ao upload.

Por isso esta etapa não introduz upload parcelado nem altera a tela Now Loading.
A medição separa melhor os custos e fornece o mapa nativo para preparar recursos
com antecedência quando os próximos testes demonstrarem necessidade.

## Testes e limites

`test_background_streaming_hook` executa os hooks de produção com uma tabela
convidada sintética e callees originais simulados. Verifica ABI/registradores,
exclusão das origens não relacionadas, deduplicação, alinhamento independente
dos tamanhos, ponteiros inválidos, limites de faixas e falha do observer. O teste
de cache verifica proteção sob pressão sem ultrapassar o orçamento e a liberação
da proteção. Ambos integram os testes portáveis; o hook também roda no CI Windows.

O percurso não cobre todos os estados de progresso nem todas as salas. Falta
medir a duração exata entre o gatilho e o primeiro frame visualmente correto;
o novo CSV identifica o pedido observado, não inspeciona a imagem. Não há
alteração nem promessa de remoção do Now Loading.
