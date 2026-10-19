# Transparência do cabelo e acessório — 19/10/2026

## Cabelo: causa confirmada e tradução corrigida

A captura `out/build/release/captures/134300`, draw0010, desenha 616 índices do cabelo com textura P8 `8299A000`, blend SRCALPHA/INVSRCALPHA e descarte de alpha zero. A textura contém alpha fracionário, mas o pixel shader anterior usava `cur.a`, derivado do vértice, em toda a superfície. A constante de material c-93 tinha alpha 0,25098.

A amostragem da memória do processo identificou AlphaOp **17 (PREMODULATE)** com AlphaArg1=TEXTURE e o AlphaOp seguinte desativado. O tradutor não tratava essa operação e caía no fallback `cur.a`.

O código original do XDK em `18D979 → 18D5E0` seleciona AlphaArg1 nessa configuração. A execução independente da rotina original `18D800` em Unicorn confirmou que AlphaOp17 e SELECTARG1 produzem os mesmos registros NV2A: AlphaInputs `18200000` e AlphaOutputs `00000C00`. MODULATE produz outro resultado. Portanto, multiplicar pela opacidade do vértice também não seria uma reprodução correta desse caso.

Foi corrigido `sdk/runtime/gpu/shaders.cpp` (SDK compartilhado, destino real `H:/dev/xbox/XboxXBERecomp/runtime/gpu/shaders.cpp`) para traduzir PREMODULATE alpha conforme o contexto: Arg1 quando o próximo AlphaOp está desativado; no primeiro combiner com próximo AlphaOp ativo, Arg1 multiplicado pela textura; nos combiners seguintes, Arg1. Os modificadores de argumento são preservados.

## Validação

- Release recompilado e jogo reiniciado, mantendo tela sem bordas e resolução automática.
- Teste `tools/test_alpha_premodulate.cpp` ligado ao gerador real do SDK: PREMODULATE e SELECTARG1 emitem o mesmo HLSL no caso reproduzido; o descarte de alpha zero permanece; MODULATE continua distinto.
- Captura posterior `out/build/release/captures/135141`, draw0010: `ps_F55988A9F3AA45A4.hlsl` agora escreve `float c1 = saturate((t0).a)`.
- Reprodução do combiner original: `out/hair_math_combiner.py`; requer os snapshots locais e Unicorn.

Para executar o teste do gerador no ambiente de desenvolvimento Windows:

```powershell
cmd /c "dev.bat clang++ -std=c++20 -O2 -ffunction-sections -fdata-sections -Isdk/runtime tools/test_alpha_premodulate.cpp sdk/runtime/gpu/shaders.cpp -Wl,/OPT:REF -o out/test_alpha_premodulate.exe"
.\out\test_alpha_premodulate.exe
```

## Acessório do braço: confirmação do usuário

A imagem adicional do usuário mostrou uma região do acessório aparentemente transparente. Após a correção e o reinício, o usuário relatou que o problema parece resolvido. Essa é uma confirmação visual do usuário, sem reprodução diferencial separada da cena do acessório. Não foi aplicada opacidade forçada aos materiais.

A execução original em Unicorn também validou 36 combinações de PREMODULATE: stages 0/1/2, próximo AlphaOp ativo/desativado e seis variantes de AlphaArg1, incluindo modificadores. Script exploratório local: `out/hair_math_active.py`.
