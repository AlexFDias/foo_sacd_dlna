# foo_sacd_dlna

Servidor UPnP/DLNA para foobar2000 com suporte a **DSD nativo** e **DVD-Audio → FLAC**.

**Versão da árvore:** `1.0.0`  
**Estado:** Alpha / desenvolvimento  
**SDK:** foobar2000 SDK 2025-03-07  
**Plataforma:** Windows / foobar2000 x64

## Funcionalidades principais

- MediaServer UPnP/DLNA com SSDP, Device Description, ContentDirectory e ConnectionManager.
- Partilha da Music Library através de árvore de Artists, Albums, Genres, Folders e All Tracks.
- SACD ISO através da API pública do `foo_input_sacd`, preparado para entrega DSD/DSF.
- DSF/DFF nativos.
- DVD-Audio através do `foo_input_dvda`, com cache FLAC lossless de 24-bit para DLNA.
- Filtro configurável **Shared formats**.
- Limite configurável de **1–16 streams simultâneos**, predefinido 2.
- Stability Mode e read-ahead configurável de 5–60 segundos, predefinido 15.
- Cache persistente de áudio/artwork e invalidação por estado relevante.
- Diagnóstico HTTP/SSDP e página `/status`.
- Integração opcional com `foo_dsd_processor` através da API DSP pública.
- Preferences nativas organizadas em **Status**, **Settings** e **Maintenance**.

## Preferences

```text
File → Preferences → Tools → SACD DLNA

SACD DLNA
├── Status
├── Settings
└── Maintenance
```

Dimensões actuais dos recursos:

| Página | Dimensão |
|---|---:|
| Root | 390 × 180 |
| Status | 450 × 400 |
| Settings | 450 × 400 |
| Maintenance | 450 × 400 |

O componente não redimensiona nem manipula a janela host das Preferences.

## Dependências funcionais

- `foo_input_sacd` — SACD ISO/DSD SACD.
- `foo_input_dvda` — DVD-Audio.
- `foo_dsd_processor` — opcional, apenas para processamento DSP DLNA.

## Validação

A documentação histórica regista **Alpha 3 I** como a última revisão explicitamente confirmada pelo responsável do projecto como compilada e funcional em Windows Debug x64. As revisões posteriores introduzem alterações adicionais e requerem novo build Windows/MSVC v142 para uma declaração de validação equivalente.

A árvore actual `1.0.0` deve, portanto, ser tratada como **código Alpha que requer rebuild e validação da revisão exacta** antes de uma release.

A validação do T+A SDX 3100 HV depende do hardware e firmware exactos.

Logs reais de uma sessão de teste com foobar2000 + T+A SDX + VLC (ver `docs/VALIDATION_STATUS.md`) mostram a validação FLAC e a lógica de timeout de streaming DVD-Audio → FLAC a funcionar correctamente; todas as falhas observadas resultaram do `libFLAC.dll` não estar instalado junto do `foo_sacd_dlna.dll`, algo agora sinalizado por um aviso na Consola no arranque em vez de 503 silenciosos por faixa. Isto é evidência de um ambiente de teste, não substitui a checklist de `RELEASE_CHECKLIST.md`.

## Documentação

A referência consolidada está em:

- `INSTALL.pt-PT.md` — instalar um componente já compilado (começa aqui se só queres usar, não compilar).
- `docs/DOCUMENTACAO.pt-PT.md` — documentação técnica e de utilização completa.
- `docs/ARCHITECTURE.md` — arquitectura.
- `docs/USER_GUIDE.md` — guia de utilização.
- `docs/DEVELOPER_GUIDE.md` — build, testes e desenvolvimento.
- `docs/VALIDATION_STATUS.md` — estado de validação.
- `DOCUMENTATION_INDEX.md` — índice de toda a documentação histórica e actual.

A árvore mantém apenas o código actual, instruções de build, documentação de utilizador/desenvolvimento e o changelog consolidado; os antigos registos de build/auditoria por revisão foram removidos.

## Licença

O código original do projecto é disponibilizado sob MIT. Dependências de terceiros mantêm os seus próprios termos.

### Correcção DLNA para DVD-Audio/FLAC

A publicação FLAC usa agora uma descrição coerente em todas as camadas DLNA: `GetProtocolInfo` anuncia `audio/flac`/`audio/x-flac`, o DIDL-Lite usa o perfil `DLNA.ORG_PN=FLAC` e a resposta HTTP para `.flac` publica o mesmo perfil em `contentFeatures.dlna.org`. Isto evita que renderizadores estritos descartem o recurso apesar de o ficheiro FLAC ser válido.

O endpoint HTTP mantém suporte a `HEAD`, `Range`, `206 Partial Content`, `Content-Range`, `Accept-Ranges` e read-ahead antes do envio.

Ver `docs/ARCHITECTURE.md` para os detalhes de implementação do pipeline DVD-Audio -> FLAC, disponibilidade de cache, diagnóstico HTTP e limitador de streams, e `CHANGELOG.md` para o respectivo histórico.
