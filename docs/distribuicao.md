# Distribuição: release sem código do jogo — plano (19/10/2026)

## Objetivo

A pipeline do GitHub gera um zip **sem nenhum asset ou código do jogo**. O jogador abre o `GenmaOnimusha.exe`, escolhe a ISO dele, e o programa faz o resto na máquina dele: extrai o XBE, recompila, compila, liga e abre o jogo. Nas execuções seguintes, o jogo abre direto.

## Por que um único toolchain (LLVM-MinGW)

- O código gerado usa a biblioteca padrão de C++ (`<cmath>`, `<cstring>`, `<limits>`, `<type_traits>`). No alvo MSVC, ela vem da STL e do Windows SDK da Microsoft, que **não podem ser redistribuídos**. O LLVM-MinGW (clang, lld, libc++, mingw-w64) é redistribuível (Apache-2.0 com exceção LLVM, ZPL, domínio público).
- O runtime lança exceções C++ (`throw X86Resume`, em `cpu/dispatch.cpp`) que o código gerado captura. Runtime e jogo precisam do **mesmo** runtime C++ e de **um único link**, então dividir em exe + DLL de toolchains diferentes não funciona.
- Versão fixada: `llvm-mingw-20260922-ucrt-x86_64.zip`, sha256 `e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666` (clang 23.1.2).

## Pacote do release

```
GenmaOnimusha/
  GenmaOnimusha.exe        launcher: pede a ISO, monta, abre o jogo
  toolchain/               LLVM-MinGW reduzido a x86_64 (clang, lld, sysroot, libc++)
  sdk/
    bin/xbe_recomp.exe
    lib/libGenmaOnimusha_runtime.a runtime + hooks + imgui + SDL3, ligado com --whole-archive
    include/               headers que o código gerado inclui (cpu/x86_ops.h e afins)
    xdk/                   layouts do XDK
    config/game.toml       endereços do jogo (sem bytes do jogo)
  README.md, THIRD_PARTY.txt, LICENSE
```

Gerado na máquina do jogador, fora do zip: `work/` (código recompilado e objetos) e `game/GenmaOnimusha.exe`.

## Primeira execução (launcher)

1. Procura a ISO em `assets/`, ou pede ao jogador com um diálogo de arquivo. O caminho escolhido fica gravado; a ISO não é copiada.
2. Abre a ISO com `kernel::Disc` (aceita redump completo e XISO) e extrai o `default.xbe` para `work/`.
3. Confere o SHA-1 contra o do release (`08bfe432eb55877b5af2dd401b713743c5b751cd`). Se for outra versão, avisa.
4. Executa `xbe_recomp` com `config/game.toml` → `work/gen/`. O `image_symbols.h` tem de sair igual ao que o runtime usou na compilação; se divergir, para com erro.
5. Compila as partições em paralelo (todos os núcleos) com o clang do pacote, mostrando o progresso.
6. Liga: objetos gerados + `--whole-archive libGenmaOnimusha_runtime.a` + bibliotecas do sistema (mingw-w64) → `game/GenmaOnimusha.exe`.
7. Abre o jogo. Falhas ficam em `work/build.log`, e o launcher mostra a mensagem.

## Estado (19/10/2026)

| Etapa | Estado |
| --- | --- |
| 1. Viabilidade com LLVM-MinGW | **Pronta.** O jogo compila e roda (tela de título verificada). Só DLLs do Windows e o UCRT. Os hooks continuam vencendo as funções recompiladas. |
| 2. CMake | **Pronta.** `add_xbe_release` no SDK (biblioteca do runtime, launcher, `package_<alvo>`) e `xbe_recomp --symbols-only`. O jogo escolhe `XBE_RELEASE=ON` ou o build de desenvolvimento. |
| 3. Launcher | **Pronto.** `sdk/launcher/` (main, build, window). O `FindGame` lê `assets/game_path.txt`. |
| 4. Pipeline | **Escrita** (`.github/workflows/build.yml`), ainda não rodou no GitHub. |
| 5. Repositório | `.gitignore`, README, `release.bat` e `assets/PUT_YOUR_GAME_HERE.txt` prontos. O SDK fica copiado em `sdk/` (repositório próprio, sem fork; origem em `sdk/UPSTREAM.md`). |
| 6. Teste de ponta a ponta | **Passou** numa pasta limpa: o launcher recompilou, compilou 33 partes e ligou em 42 s (i7-4790K, 8 threads) e abriu o jogo a partir da ISO redump fora da pasta. |

Pacote: 338 MB descompactado (toolchain com 328 MB: bin 129, include 113, sysroot 69, clang 17).

## Mudanças no SDK (`sdk/`, base TGP482 `99078b1`)

- `cmake/XbeRecomp.cmake`: `$<TARGET_FILE:xbe_recomp>` no comando de recompilação; `_WIN32_WINNT`, `-municode` e `xaudio2_9` no MinGW; `add_xbe_release`.
- `cmake/XbePackage.cmake`: layout do pacote e redução do toolchain.
- `tools/recomp`: `ImageSymbolsHeader`, `--symbols-only`, `#include <cstdlib>`.
- `launcher/`: o launcher.
- `runtime/app/game_files.cpp`: `assets/game_path.txt`.
