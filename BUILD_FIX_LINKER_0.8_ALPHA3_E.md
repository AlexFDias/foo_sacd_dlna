# Build fix — 0.8 Alpha 3 E

## Sintoma

O build chega ao link final de `foo_sacd_dlna`, mas falha com:

```text
foobar2000_SDK.lib(utility.obj) : error LNK2005:
pfc::winFormatSystemErrorMessageHook ... já definida no pfc.lib(pfc-fb2k-hooks.obj)

foobar2000_SDK.lib(utility.obj) : error LNK2005:
pfc::crashHook ... já definida no pfc.lib(pfc-fb2k-hooks.obj)

LNK1169: encontrados um ou mais símbolos múltiplos definidos
```

## Causa

O SDK 2025-03-07 fornece configurações específicas `Debug FB2K` / `Release FB2K` para `pfc`.
Nestas configurações, `pfc-fb2k-hooks.cpp` fica excluído da compilação. Isto é intencional para componentes foobar2000, porque os hooks correspondentes são fornecidos/redirecionados pelo lado `foobar2000_SDK`/`shared`.

A solução deste projeto estava a compilar `pfc` como `Debug|x64` / `Release|x64`, fazendo com que `pfc-fb2k-hooks.obj` e `foobar2000_SDK.lib(utility.obj)` definissem os mesmos dois símbolos.

## Correção

A configuração da solução `foo_sacd_dlna.sln` continua a ser:

```text
Debug|x64
Release|x64
```

mas o projeto `pfc` é agora mapeado para:

```text
Debug|x64   -> Debug FB2K|x64
Release|x64 -> Release FB2K|x64
```

Isto segue o padrão usado pelo `foo_sample.sln` do SDK oficial.

## O que fazer no Visual Studio

1. Abrir `foo_sacd_dlna.sln`.
2. Selecionar `Debug` / `x64`.
3. `Build > Clean Solution`.
4. `Build > Rebuild Solution`.

O projeto `pfc` deve aparecer no Output como:

```text
Debug FB2K x64
```

e não como `Debug x64`.

## Resultado esperado

A etapa de compilação deve deixar de produzir os dois `LNK2005` e o `LNK1169` acima. O DLL de `foo_sacd_dlna` deverá então avançar para a conclusão do link.

## Nota

Os avisos `C4996` mostrados no build do SDK/libPPUI são warnings existentes nessas fontes do SDK e não são a causa desta falha de link.

## Validação desta correcção

A versão **0.8 Alpha 3 E** foi posteriormente compilada com sucesso em Windows **Debug x64**, sem erros de compilação ou linker.

Projectos concluídos:

```text
foobar2000_component_client  OK
foobar2000_sdk_helpers       OK
pfc                         OK
foobar2000_SDK              OK
libPPUI                     OK
foo_sacd_dlna               OK
```

A correcção dos mapeamentos `Debug FB2K` / `Release FB2K` para `pfc` eliminou o conflito que originava `LNK2005` e `LNK1169` no link final.
