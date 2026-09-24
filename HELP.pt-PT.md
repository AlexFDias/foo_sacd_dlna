# foo_sacd_dlna — Ajuda rápida / FAQ

Para a versão em Inglês, ver `HELP.md`.

Esta é uma página de ajuda curta e orientada a tarefas. Para detalhe completo ver `docs/USER_GUIDE.md` (utilização), `INSTALL.pt-PT.md` (instalação), `BUILD.pt-PT.md` (compilação), e `DOCUMENTATION_INDEX.md` (tudo o resto).

## O que este componente faz

O `foo_sacd_dlna` é um MediaServer UPnP/DLNA para o foobar2000. Partilha a tua Music Library pela rede, para que um renderer DLNA (um streamer de áudio de rede, uma TV, uma app control-point, etc.) a possa explorar e reproduzir:

- Ficheiros **DSF/DFF** são servidos tal como estão.
- **SACD ISO** é descodificado através do `foo_input_sacd` e servido como DSD/DSF nativo (o próprio contentor `.iso` nunca é enviado).
- **DVD-Audio** é descodificado através do `foo_input_dvda` e servido como **FLAC** 24-bit sem perdas, em cache, através do encoder oficial Xiph libFLAC 1.5.x.
- Qualquer outro formato partilhado (FLAC/WAV/MP3/...) também pode ser servido tal como está, opcionalmente, através de **Shared formats** em Settings.

## Forma mais rápida de começar a ouvir

1. `File → Preferences → Tools → SACD DLNA → Settings`: activa o DLNA, activa o Share Music Library.
2. Aponta a tua app renderer/control-point para este PC; deve descobrir o servidor automaticamente via SSDP.
3. Observa o `Status`: `BROADCASTING / ACTIVE` significa que o servidor está no ar; `TRANSMITTING` significa que há áudio a fluir de facto para um cliente neste momento.

## "Toca DSF mas não faixas DVD-Audio" — lê isto primeiro

De longe a causa mais comum **não é um bug**: DVD-Audio → FLAC precisa do `libFLAC.dll` (Win64, 1.5.x) na *mesma pasta onde está instalado* o `foo_sacd_dlna.dll` — não basta estar na pasta de output da compilação da árvore de código-fonte. Se faltar, todas as faixas DVD-Audio falham, uma a uma, como um `503` HTTP no renderer (que muitos renderers/players depois mostram como um erro genérico de "formato inválido ou desconhecido").

A partir deste build, a **Consola** do foobar2000 avisa-te de imediato no arranque se for esse o problema:

```text
SACD DLNA: libFLAC.dll was not found at "...\libFLAC.dll" -- DVD-Audio to FLAC
conversion will fail for every track until it is copied there (see FLAC_RUNTIME.md).
```

Correcção: copia o `third_party/libFLAC/Win64/libFLAC.dll` (da árvore de código-fonte, ou de uma distribuição FLAC 1.5.0 Win64 equivalente) para a pasta onde o `foo_sacd_dlna.dll` está de facto instalado, e reinicia o foobar2000. Ver `FLAC_RUNTIME.md` e `BUILD.pt-PT.md` (secção 15) para a explicação completa.

## Outros problemas comuns

| Sintoma | Causa provável | Onde procurar |
|---|---|---|
| O renderer nunca vê o servidor | Multicast SSDP bloqueado, ou PC/renderer em sub-redes/VLANs diferentes | `NETWORK_REQUIREMENTS.md`, `PROTOCOL_COMPATIBILITY.md` |
| Funciona um bocado, depois outras faixas começam a devolver 503 | Limite de streams concorrentes atingido, muitas vezes por ligações abandonadas ao saltar de faixa | `PREFERENCES_FIELDS.md` ("Max streams"), `CHANGELOG.md` (correcção `SO_SNDTIMEO`) |
| Faixas SACD ISO falham a preparar | `foo_input_sacd` em falta, ou não devolveu um stream DSD/DoP nativo para aquela ISO em particular | Preferences → Status: linha de detecção do `foo_input_sacd` |
| Reprodução com falhas/cortes em Wi-Fi | Débito/latência; DSD256 em particular precisa de uma ligação sólida | `NETWORK_REQUIREMENTS.md` |
| Dúvida se está mesmo a enviar áudio | `BROADCASTING` só significa que a descoberta está activa, não que há um stream a fluir | painel `Status` / página web `/status` |

## Onde ir a seguir

- Instalar um build que já tens (ou acabaste de descarregar): `INSTALL.pt-PT.md`
- Utilização diária e definições: `docs/USER_GUIDE.md`, `PREFERENCES_FIELDS.md`
- Compilar a partir do código-fonte: `BUILD.pt-PT.md`
- Arquitectura / como funciona internamente: `docs/ARCHITECTURE.md`
- O que está e não está realmente validado: `docs/VALIDATION_STATUS.md`
- Mapa completo da documentação: `DOCUMENTATION_INDEX.md`
