# Plano — esteiras Windows e Linux

Data: 28/10/2026. Escopo: CI separado por plataforma e publicação dos dois pacotes a partir da mesma versão. Implementação na branch `feature/linux-support`; o estado abaixo registra o ponto de partida da investigação.

## Implementação

- `build.yml` coordena metadados, os workflows reutilizáveis `build-windows.yml`/`build-linux.yml` e um único publicador por tag. Execuções manuais permitem escolher a plataforma.
- Linux usa uma biblioteca de runtime sem assets, launcher CLI, diretórios XDG e cache por identidade do build. O pacote inclui Clang 14/libstdc++ 11, sysroot glibc 2.35, loader Vulkan e DXC 1.8.2407; URLs e SHA-256 constam de `config/linux-dependencies.lock.json`. Cabeçalhos de desenvolvimento X11/Pulse/Vulkan são usados apenas na construção do pacote.
- `test_linux_package.py` verifica hashes, ausência de assets, toolchain relocada, exceções C++, link sintético de todo o runtime e layout somente leitura. O tar preserva symlinks e permissões executáveis.
- O publicador verifica versão/commit/hashes de ambas as plataformas antes de criar um draft. Só publica após conferir os uploads; reexecuções idênticas são aceitas e arquivos diferentes nunca são sobrescritos.
- Validação concluída: `actionlint` dos três workflows; cinco testes do publicador; sete testes do núcleo no toolchain de desenvolvimento e oito testes no toolchain de distribuição, incluindo a ponte SDL; build do pacote Windows, duas regressões de controles e todos os fixtures do updater, incluindo assets mistos.
- A primeira publicação revelou que o endpoint REST por tag não encontra rascunhos. O publicador passou a localizar a tag na lista autenticada e paginada de releases e consultar os uploads pelo ID da release. O teste adicional garante a retomada de um rascunho existente sem recriá-lo; a verificação dos quatro uploads continua obrigatória antes de publicar.
- O teste do pacote Linux passou: hashes/lista permitida, dependências privadas ELF, ausência de assets, compilador relocado, exceções C++, link sintético de todo o runtime, layout completo em pasta UTF-8/com espaços e somente leitura, diretórios XDG e símbolos do launcher limitados a glibc 2.35. O teste somente leitura usa uma cópia independente, sem mudar permissões do pacote original.
- Prova local do pacote: tar de aproximadamente 189 MiB, 33 unidades recompiladas/compiladas a partir de `assets/default.xbe` pela CLI embarcada, seguida de uma segunda chamada sem recompilação. O estado ficou em `out/package validation/{config,data,cache}`. A execução real de 60 segundos via Dozen/RTX 3050 terminou com código 0, último contador de 1.440 quadros/7.041 draws/dez pipelines; saída SDL float estéreo de 48 kHz inicializada. A captura do quadro 600 foi inspecionada e mostra a tela de título. Logs em `out/linux-package-{test,game-build,cached-build,game-run}.log`; nenhum dado dessa prova entra nos artifacts do CI. Isso valida o uso do pacote no ambiente WSL disponível, sem substituir uma prova em Linux nativo/Steam Deck.
- Os nomes antigos dos checks não estão exigidos por proteção de branch: a API retornou `Branch not protected` para `main` e nenhum ruleset em 28/10/2026. O PR #28 da base de ferramentas ainda estava aberto; seus commits estão incluídos nesta branch.
- Steam Deck, drivers Linux nativos, gameplay completo e paridade audiovisual continuam pendentes. A issue #8 permanece aberta; nenhuma tag/release é criada ao preparar esta branch.

## Estado examinado

- `.github/workflows/build.yml` executa `linux-core`, `linux-window` e um job `release` Windows. O próprio job Windows chama `gh release create` em tags `v*`.
- Windows já gera um pacote sem assets: launcher, runtime estático, recompiler, headers e LLVM-MinGW. O updater exige exatamente `GenmaOnimusha-<tag>-win64.zip` e valida tamanho/digest do asset.
- Linux já executa o jogo localmente pelo host SDL/Vulkan, mas o alvo `linux-game` depende de `assets/default.xbe`. Ele não pode ser usado diretamente para publicar um pacote no runner público.
- `add_xbe_release()` exige MinGW. O launcher, as flags de link e `XbePackage.cmake` ainda são específicos de Windows.
- O Vulkan de janela e a ponte de controle têm testes sem assets; a execução real no WSL é uma evidência complementar. Ainda não há validação de uma instalação limpa do pacote Linux nem Steam Deck.

## Arquitetura proposta

Manter `build.yml` como coordenador e criar dois workflows reutilizáveis, chamados com `workflow_call`. Assim, metadados e publicação ficam centralizados, e as etapas de cada sistema permanecem separadas. Referência: [workflows reutilizáveis do GitHub](https://docs.github.com/en/actions/how-tos/reuse-automations/reuse-workflows).

| Arquivo | Responsabilidade |
| --- | --- |
| `.github/workflows/build.yml` | Eventos PR/push/tag/manual, versão/commit, chamada das duas plataformas e publicação final |
| `.github/workflows/build-windows.yml` | Toolchain LLVM-MinGW, runtime/launcher, testes, pacote ZIP e upload do arquivo final |
| `.github/workflows/build-linux.yml` | Núcleo, diagnóstico gráfico/entrada, runtime/launcher Linux, pacote tar e testes do pacote |
| `cmake/LinuxRelease.cmake` e script de empacotamento Linux | Alvos de distribuição sem assets e montagem do pacote por lista explícita de arquivos |
| Manifesto de dependências versionado | Versões, URLs oficiais, SHA-256, baseline Linux e licenças das toolchains/DXC |

Dentro do workflow Linux, manter jobs distintos para núcleo, janela/ponte e pacote. O pacote só é considerado pronto quando seus testes e os testes de plataforma passam. As esteiras Windows/Linux rodam em paralelo; uma falha não cancela o diagnóstico da outra.

O coordenador fornece versão, commit e modo de publicação como inputs. Workflows reutilizáveis não recebem implicitamente as variáveis `env` globais do chamador: mover os pins do LLVM-MinGW ao workflow Windows ou ao manifesto, em vez de depender do `env` atual de `build.yml`.

## Artefatos e publicação

| Plataforma | Asset da release |
| --- | --- |
| Windows x86-64 | `GenmaOnimusha-vX.Y.Z-win64.zip` — manter o contrato atual do updater |
| Linux x86-64 | `GenmaOnimusha-vX.Y.Z-linux-x86_64.tar.gz` |
| Ambas | `SHA256SUMS` e manifesto com versão, commit, plataforma, ABI/toolchain e maturidade do pacote |

Cada esteira deve subir o arquivo compactado final, mais seu manifesto, como artifact exclusivo da plataforma. Publicar os mesmos bytes que foram testados; não remontar arquivos no job de publicação. O tar preserva permissões executáveis e os links necessários do toolchain.

Um único job de publicação, dependente das duas esteiras, baixa os artifacts da execução atual, confere nomes, versões, commit e hashes e cria a release uma vez. Retirar `gh release create` do job Windows para evitar concorrência com Linux. Na repetição de um job, conferir uma release existente: se o asset já existir com o mesmo hash, não duplicar; se divergir, falhar com diagnóstico em vez de sobrescrever silenciosamente.

Eventos:

- PR e push em `main`: compilar/testar as duas plataformas e disponibilizar artifacts, sem publicar release.
- Tag `v*`: depois de habilitada a distribuição Linux, publicar ambos os pacotes e checksums na mesma release.
- `workflow_dispatch`: permitir testar pacote Linux/Windows sem criar tag ou release.
- Durante a migração: manter o release Windows existente funcionando. Habilitar inicialmente o pacote Linux como artifact manual/de PR; somente adicionar Linux à dependência da publicação por tag após passar nos critérios de saída. Não enviar um tar incompleto como se fosse a versão Linux pronta.

Permissões padrão `contents: read`; somente o job final de publicação recebe `contents: write`. Usar `pull_request`, sem executar código de PR com privilégios de publicação. Chaves de cache incluem plataforma, arquitetura, compilador e manifesto de dependências. Separar cache de downloads do cache de objetos. Usar concurrency por ref: cancelar builds de PR substituídos, preservando execuções de release por tag.

## Pacote Linux sem dados do jogo

A distribuição inicial será uma pasta em `.tar.gz`, marcada como Linux experimental. AppImage fica para uma etapa posterior. O pacote deve permitir primeira execução sem exigir compilador, headers ou CMake instalados pelo jogador.

Conteúdo planejado:

```text
GenmaOnimusha/
  GenmaOnimusha                 entrada/launcher Linux
  sdk/bin/xbe_recomp
  sdk/lib/libGenmaOnimusha_runtime.a
  sdk/lib/libSDL3.a             ou incorporada ao runtime, sem duplicar símbolos
  sdk/include/                 headers do runtime e image_symbols.h
  sdk/config/game.toml
  sdk/xdk/                     layouts existentes
  toolchain/                   clang/lld, headers e sysroot Linux x86-64 compatível
  shader-tools/                DXC e suas bibliotecas necessárias
  manifest.json
  README, LICENSE, THIRD_PARTY e licenças dos componentes incluídos
```

Não incluir `assets/` com dados do jogo, XBE/ISO, `generated/game`, objetos recompilados do jogo, HDD/saves, capturas ou shader caches de sessões reais. O Dozen e o helper Windows→WSL são ferramentas do nosso teste local e ficam fora do pacote Linux. O usuário de Linux nativo usa os drivers Vulkan/controle do próprio sistema.

O build de release deve produzir o runtime usando `--symbols-only`, como Windows, sem ler um XBE. Separar a parte comum de `add_xbe_release` da seleção do launcher, flags e empacotador por plataforma. A composição do runtime deve excluir explicitamente o experimento local `acos_experiment.cpp` e quaisquer fontes de teste em ambas as plataformas.

Para a primeira entrega do launcher Linux, usar uma CLI com caminho de ISO/XBE e mensagens de progresso/log claras. A seleção por GUI e integração com Gaming Mode podem vir depois. O launcher deverá:

1. Receber a cópia do jogador, verificar o SHA-1 e preparar os arquivos sem modificar a ISO.
2. Recompilar localmente, conferir `image_symbols.h`, compilar as partições e fazer um único link com o runtime pré-compilado.
3. Usar exatamente a ABI, flags, biblioteca C++ e sysroot com que o runtime foi construído; preservar weak overrides e exceções entre unidades de compilação.
4. Usar subprocessos com argumentos separados, tratar caminhos UTF-8/com espaços, limitar paralelismo por recursos e registrar falhas/cancelamento.
5. Guardar saves/configuração em XDG data/config e código gerado, objetos, shader cache e executável construído em diretórios graváveis próprios. Adaptar `ExeDir`/busca de assets/configuração e `XBE_DXC` para esse layout; não depender de `out/` ou de variáveis da máquina de desenvolvimento.
6. Invalidar o cache de compilação por versão do runtime/ABI e iniciar o jogo diretamente nas execuções seguintes.

Fixar inicialmente uma base Linux x86-64 equivalente ao Ubuntu 22.04 para a construção e os testes. Confirmar o piso real de glibc/libstdc++ pelos símbolos ELF e pela execução em ambiente limpo; o rótulo do runner não é prova de compatibilidade. O container/imagem e o conjunto toolchain/sysroot devem ser fixados e usados também pelo compilador embarcado. Não transportar o compilador Swift da máquina de desenvolvimento para o pacote.

O pin final do toolchain Linux e o tamanho do pacote dependem de uma prova de empacotamento; decidir isso antes de declarar a release utilizável sem dependências de desenvolvimento. DXC precisa ter versão/hash/licenças fixados e ser localizado relativamente ao pacote. Não embutir drivers de GPU. Documentar e testar as bibliotecas de execução que permanecerem externas (loader Vulkan e backends de janela/áudio).

## Testes e critérios de saída

| Área | Verificação exigida |
| --- | --- |
| Windows | Runtime/launcher, regressões de controles e updater existentes; pacote Windows conserva seu nome e funcionamento |
| Linux sem assets | Núcleo, fibers, políticas/hooks de controles e atenuação de áudio; janela dummy; ponte simulada; Vulkan/readback em Xvfb/lavapipe |
| Runtime de release | Gerar símbolos e construir a biblioteca Linux sem `assets/default.xbe`; linkar um fixture sintético sem dados do jogo, exercitando hooks e exceções |
| Integridade do pacote | Lista permitida de arquivos, ausência de assets/código gerado real, flags executáveis, dependências ELF, caminhos/RPATH e licenças; manifestos/hashes consistentes |
| Uso em máquina limpa | Extrair em pasta com espaços; executar a toolchain/DXC embarcados sem instalações de desenvolvimento; verificar erro útil sem ISO e diretórios XDG graváveis com pacote somente leitura |
| Teste com a cópia local | Fora do CI público: primeira compilação a partir da ISO, boot, controle, áudio, gameplay 3D, save/load e segunda execução sem recompilar; testar driver Linux nativo |
| Release | Artifacts das duas plataformas pertencem à mesma tag/commit; falha de pacote impede publicação incompleta após a migração; reexecução não duplica release/assets |

O runner público não recebe os nossos assets. Fixtures sintéticos e testes por software podem rodar publicamente; a validação com o jogo completo é local, sem upload do XBE, objetos, capturas ou logs que contenham dados proprietários. A aprovação da distribuição experimental exige ao menos uma instalação limpa real a partir do pacote.

O updater Windows já seleciona o asset exato `win64.zip`; adicionar um tar Linux não deve mudar isso. Ampliar o fixture do updater para uma release com os dois sistemas e conferir que ele ignora o asset Linux. No Linux, a primeira versão terá atualização manual documentada. Auto-update Linux deve ser uma entrega posterior com seleção por plataforma, verificação, substituição/recuperação e preservação dos diretórios do jogador; não executar `update.ps1` no Linux.

## Ordem de implementação

1. **PR de separação de CI:** extrair a esteira Windows para workflow reutilizável; organizar testes Linux no seu workflow; deixar a publicação atual produzindo o mesmo ZIP, centralizada no coordenador. Checar nomes de status usados em branch protection antes de renomeá-los.
2. **PR de runtime de distribuição Linux:** biblioteca sem assets, símbolos por configuração, ABI/flags compartilhadas e fixture de link; seleção explícita das fontes por plataforma.
3. **PR de launcher/toolchain/pacote Linux:** CLI, XDG/cache, toolchain/sysroot/DXC fixados, montagem do tar e testes de ambiente limpo. Gerar inicialmente artifact experimental por PR/execução manual.
4. **PR de publicação conjunta:** após validar o pacote Linux localmente, incluir ZIP + tar + checksums no job único por tag; adicionar regressão do updater Windows com assets mistos e testar reexecução da publicação.

Resultado esperado: a mesma tag gera dois downloads independentes, cada um capaz de preparar a cópia do jogador na sua plataforma. A issue #8 permanece aberta enquanto os critérios de gameplay, compatibilidade e distribuição Linux não estiverem atendidos.
