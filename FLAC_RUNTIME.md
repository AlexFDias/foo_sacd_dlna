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

O componente carrega esta DLL dinamicamente a partir do directório do próprio `foo_sacd_dlna.dll`. O projecto copia a DLL para `$(OutDir)` no build.

Não são necessários headers FLAC nem uma import library para compilar esta integração.

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

A integração foi revista estaticamente nesta árvore, mas requer compilação Windows/MSVC e teste real com uma faixa DVD-Audio. Não declarar a reprodução no T+A SDX 3100 HV como validada até esse teste ser executado.
