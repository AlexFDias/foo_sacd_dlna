# DVD-Audio → FLAC runtime

## Encoder único

A conversão DVD-Audio usa **exclusivamente o libFLAC 1.5.x oficial da Xiph**. O writer FLAC manual que existia em revisões anteriores foi removido. Não há código local para gerar STREAMINFO, headers de frames, subframes ou CRCs.

Fluxo:

```text
DVD-Audio
  -> foo_input_dvda
  -> PCM float do foobar2000
  -> PCM inteiro 24-bit intercalado
  -> libFLAC 1.5.x
  -> FLAC nativo
  -> validação estrutural
  -> cache .flac
  -> DLNA/HTTP
```

A API oficial documenta `FLAC__stream_encoder_init_file()` para criar um ficheiro FLAC nativo e `FLAC__stream_encoder_process_interleaved()` para alimentar PCM inteiro intercalado. `FLAC__stream_encoder_finish()` finaliza os frames pendentes e actualiza a metadata final.

## Runtime Windows x64

O projecto inclui:

`third_party/libFLAC/Win64/libFLAC.dll`

O componente carrega esta DLL dinamicamente a partir do directório do próprio `foo_sacd_dlna.dll`. O projecto copia a DLL para `$(OutDir)` no build — **mas essa é a pasta de output da compilação, não a pasta de componentes do foobar2000**. Tens de copiar `libFLAC.dll` manualmente para a pasta onde o `foo_sacd_dlna.dll` fica instalado (ver `BUILD.md`, secção 15).

Não são necessários headers FLAC nem uma import library para compilar esta integração.

### Aviso no arranque se a DLL faltar

Este é o passo de instalação mais fácil de esquecer, e esquecê-lo produzia um sintoma enganador: todas as faixas DVD-Audio falhavam silenciosamente, uma a uma, como `HTTP 503` no renderer, sem nada a apontar para a causa até se inspeccionar `network.log`.

`SacdDlnaServer::start()` agora testa, no arranque, se `libFLAC.dll` está mesmo acessível (mesma ordem de pesquisa do `dvd_audio_flac.cpp`: primeiro a pasta do próprio componente, depois o caminho de pesquisa padrão do Windows) e, se não estiver, escreve de imediato na Consola do foobar2000:

```text
SACD DLNA: libFLAC.dll was not found at "<caminho testado>" -- DVD-Audio to FLAC
conversion will fail for every track until it is copied there (see FLAC_RUNTIME.md).
DSD/SACD sharing is not affected.
```

Isto é apenas um aviso, não bloqueia o arranque: a partilha DSD/SACD continua a funcionar normalmente mesmo sem `libFLAC.dll`, só a conversão DVD-Audio → FLAC fica indisponível até a DLL ser copiada.

## Configuração

- FLAC nativo, não Ogg FLAC
- 24-bit PCM
- sample rate original do DVD-Audio
- número de canais original, máximo 8
- streamable subset activado
- blocksize 4096
- compression level 5
- verify activado

## Validação

O cache DVD-Audio usa `cacheVersion = 4`. A validação local verifica a assinatura `fLaC`, percorre toda a cadeia de metadata, valida o bloco STREAMINFO contra sample rate/canais/bits/samples esperados e confirma o início de pelo menos um frame. **Não interpreta nem calcula CRCs de frames**; isso pertence ao libFLAC.

Um cache criado por versões anteriores é invalidado automaticamente pelo incremento da versão de cache.

## Limpeza

Ficheiros `.partial` e resultados de conversão falhada são removidos. O ficheiro final só é publicado no cache depois de `finish()` e da validação estrutural terem sucesso.

## Estado de validação

A integração foi revista estaticamente nesta árvore; continua a requerer compilação Windows/MSVC própria (não há aqui toolchain Windows). Não declarar a reprodução no T+A SDX 3100 HV como validada apenas com base nisto.

Dito isto, já existe evidência real de campo (`network.log` + diagnóstico VLC de uma sessão com foobar2000 e um renderer T+A, ver `docs/VALIDATION_STATUS.md`): na sessão de teste mais recente disponível, **nenhuma** falha de conversão foi atribuída à validação estrutural do FLAC gerado nem ao timeout de socket (`SO_SNDTIMEO`) — todas as falhas, sem excepção, foram `libFLAC.dll 1.5.x was not found next to foo_sacd_dlna`. Ou seja, o encoder e a validação da cache não mostraram problemas nesse teste; o que faltava era o passo manual de colocar `libFLAC.dll` na pasta de componentes (ver secção acima).
