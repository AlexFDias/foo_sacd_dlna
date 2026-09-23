# foo_sacd_dlna — Documentação Consolidada

**Componente:** `foo_sacd_dlna`  
**Versão da árvore:** `0.8-alpha3-u-dvda-flac`  
**Estado:** Alpha / desenvolvimento  
**Plataforma alvo:** foobar2000 x64 / Windows  
**SDK alvo:** foobar2000 SDK 2025-03-07  
**Toolchain documentada:** Visual Studio 2022 / MSVC v142 / WTL

> Esta documentação consolida o estado da árvore fornecida. Não substitui os registos históricos de build e auditoria; esses continuam no directório raiz.

## 1. Objectivo

O `foo_sacd_dlna` é um servidor UPnP/DLNA para foobar2000 destinado a disponibilizar música através da rede para leitores compatíveis.

O projecto combina dois caminhos principais:

1. **SACD/DSD:** fontes DSD, incluindo SACD ISO através do `foo_input_sacd`, são preparadas para entrega em DSF/DSD.
2. **DVD-Audio:** fontes DVD-Audio são descodificadas através do `foo_input_dvda` e disponibilizadas na rede como FLAC lossless.

O servidor implementa descoberta SSDP, descrição UPnP, ContentDirectory, ConnectionManager e entrega HTTP de média.

## 2. Política de áudio

### 2.1 Caminho DSD

O caminho pretendido é:

```text
SACD ISO / DSF / DFF
        ↓
foobar2000 + decoder público
        ↓
DSD interno / extracção dos bits DSD
        ↓
DSF
        ↓
HTTP / UPnP / DLNA
        ↓
Renderer
```

O DoP usado na interface pública do decoder é um transporte **interno ao foobar2000**. O componente não envia DoP pela rede como formato de transporte.

O projecto não pretende fazer DSD → PCM no caminho DLNA nativo.

### 2.2 Caminho DVD-Audio

```text
DVD-Audio
   ↓
foo_input_dvda
   ↓
PCM
   ↓
FLAC 24-bit lossless
   ↓
cache persistente
   ↓
HTTP / UPnP / DLNA
```

O código `dvd_audio_flac.cpp` escreve um FLAC próprio, sem depender de uma biblioteca FLAC externa incluída no componente. A implementação usa subframes verbatim de 24-bit PCM.

A implementação rejeita fontes com mais de 8 canais porque o mapeamento de canais usado pelo conversor não está definido para mais de 8 canais.

## 3. Formatos partilhados

A opção **Shared formats** controla as extensões que entram na publicação da Music Library. A configuração existente na árvore é:

```text
dsf,dff,iso,aob,ifo,mlp,thd,truehd,flac,wav,mp3
```

A lista é normalizada e duplicados são eliminados pelo parser. Separadores por vírgula, espaços, tabs e novas linhas são suportados pela implementação de parsing.

A partilha de formatos não DSD é nativa: quando não é necessária uma cache de conversão, `serveMedia()` pode entregar o ficheiro através do caminho nativo do foobar2000.

## 4. Dependências

### Obrigatórias para desenvolvimento

- foobar2000 SDK 2025-03-07.
- Visual Studio 2022 com ferramentas C++ adequadas.
- MSVC v142 conforme a configuração documentada.
- Headers WTL disponíveis segundo `WTL_SETUP.md`, `WTL_RELOCATION.md` e `WTL_SDK_INTEGRATION.md`.

### Dependências funcionais

- `foo_input_sacd` para SACD ISO/DSD SACD.
- `foo_input_dvda` para DVD-Audio.
- `foo_dsd_processor` é opcional e só é necessário para o caminho de processamento DSP configurável.

O servidor só permite activar-se a partir das Preferences quando pelo menos `foo_input_sacd` ou `foo_input_dvda` está disponível.

## 5. Preferences

A página principal está em:

```text
File → Preferences → Tools → SACD DLNA
```

A árvore de Preferences está organizada em páginas nativas do foobar2000:

```text
SACD DLNA
├── Status
├── Settings
└── Maintenance
```

O host de Preferences do foobar2000 não é redimensionado pelo componente.

### 5.1 Status

A página é informativa. Mostra, entre outros estados:

- DLNA / broadcasting;
- presença das dependências SACD e DVD-Audio;
- estado da Music Library partilhada;
- stream activo;
- detecção T+A SDX;
- read-ahead DSD;
- DSD Processor;
- estado de rede;
- clientes e streams.

### 5.2 Settings

Contém:

- `Enable DLNA`;
- `Share Music Library`;
- `Server name`;
- `HTTP port`;
- `Max streams`;
- `Shared formats`;
- `Stability mode`;
- `Pre-buffer (seconds)`;
- `Process DLNA audio through DSD Processor`.

O limite de streams é mantido entre **1 e 16**, com valor predefinido de **2**.

O pre-buffer é limitado pelo código entre **5 e 60 segundos**, com valor predefinido de **15 segundos**.

### 5.3 Maintenance

Contém:

- `Network logging`;
- `Enable Debug Diagnostics`;
- `Run Network Probe`;
- refresh da Music Library;
- abertura das Preferences da Music Library;
- limpeza da Music Library partilhada;
- limpeza da cache persistente;
- configuração do DSD Processor;
- ajuda integrada.

### 5.4 Dimensões actuais das páginas

A árvore fornecida define:

| Página | Dialog units |
|---|---:|
| Root SACD DLNA | 390 × 180 |
| Status | **450 × 400** |
| Settings | **450 × 400** |
| Maintenance | **450 × 400** |

Estas dimensões pertencem aos recursos `DIALOGEX`; não são obtidas através de hacks de resize do host.

## 6. Limite de clientes e streams

O componente distingue **clientes** de **streams**.

Um cliente é um peer remoto identificado pelo IP que efectuou pedidos HTTP. O self-test local/loopback não entra nessa contagem.

O estado expõe:

- total de clientes;
- clientes activos;
- clientes idle;
- clientes vistos desde o arranque;
- streams em uso;
- limite configurado;
- pedidos rejeitados por limite.

Quando o limite de streams é atingido, novos pedidos de áudio podem receber `503` com `Retry-After`. O slot é adquirido atomicamente no ponto em que o áudio está prestes a ser transmitido e libertado em todos os caminhos de saída.

Alterar o limite aplica-se a novos pedidos; streams já em execução não são interrompidos por essa alteração.

## 7. Stability Mode e read-ahead

O Stability Mode utiliza um send-buffer/read-ahead calculado a partir da taxa DSD e do número de segundos configurado.

O indicador de buffer é **do servidor**, não do buffer interno do renderer.

Estados documentados incluem:

- `READY / FULL RESERVE`;
- `DRAINING / HEALTHY`;
- `LOW / REFILL NOT AVAILABLE`;
- `DEPLETED / RISK OF UNDERRUN`.

A funcionalidade não torna uma rede insuficiente magicamente suficiente; apenas cria margem contra variações temporárias.

## 8. DSD Processor

O DSD Processor é opcional e utiliza a API DSP pública do foobar2000. Não altera a cadeia DSP normal de reprodução do utilizador.

Modos documentados:

```text
Native DSD:
DSD → DSF → DLNA

DSD Processor:
PCM → DSD Processor → DSD/DoP interno → DSF → DLNA
DSD → DSD Processor → DSD/DoP interno → DSF → DLNA
```

O resultado é aceite apenas quando continua a ser DSD/DoP válido para geração de DSF. Um resultado PCM não é silenciosamente convertido em saída PCM DLNA.

A configuração/preset do DSP participa na chave da cache de DSF, permitindo invalidar resultados antigos quando o preset muda.

## 9. Cache

A árvore implementa caches para cenários de conversão, incluindo:

- SACD ISO → DSF;
- DSD Processor → DSF;
- DVD-Audio → FLAC;
- artwork.

Os manifests incluem informação de versão/estado do decoder ou da configuração relevante para evitar reutilizar resultados incompatíveis.

A limpeza da cache persistente não altera os ficheiros fonte.

## 10. UPnP/DLNA

O servidor implementa, na árvore fornecida:

- SSDP discovery;
- Device Description;
- ContentDirectory;
- ConnectionManager;
- `Browse`;
- `BrowseMetadata`;
- `GetSystemUpdateID`;
- `GetSearchCapabilities`;
- `GetSortCapabilities`;
- DIDL-Lite;
- paginação de resultados;
- HTTP `Range`;
- artwork;
- página `/status`.

A árvore de navegação inclui:

```text
Artists
Albums
Genres
Folders
All Tracks
```

## 11. Diagnóstico de rede

Os diagnósticos distinguem actividade local de visibilidade real por um peer remoto.

Exemplos de estados:

- `SSDP NOTIFY sent`;
- `HTTP self-test`;
- `SSDP self-probe`;
- `CONFIRMED / REMOTE SSDP M-SEARCH`;
- `CONFIRMED / REMOTE HTTP`.

O Network Probe valida os endpoints locais `device.xml`, `ContentDirectory.xml` e `ConnectionManager.xml` e executa um `M-SEARCH` SSDP de MediaServer.

## 12. T+A SDX 3100 HV

O SDX 3100 HV é o alvo de hardware documentado no projecto. A validação real depende da unidade e firmware exactos.

A matriz de hardware deve cobrir pelo menos:

- discovery;
- device description;
- Browse root/Artist/Album;
- BrowseMetadata;
- DSD64;
- DSD128;
- DSD256;
- SACD ISO → DSF;
- artwork;
- Range/seek;
- next track;
- gapless;
- teste prolongado de DSD256;
- recuperação com congestionamento de rede.

O projecto não deve declarar compatibilidade de firmware específico sem uma execução dessa matriz.

## 13. Build

A solução é:

```text
foo_sacd_dlna.sln
```

O projecto principal é:

```text
foo_sacd_dlna.vcxproj
```

O caminho documentado usa a SDK 2025-03-07 e WTL. A integração WTL foi alterada ao longo da série Alpha 3 para suportar descoberta/relocação da pasta WTL.

### Build recomendado

1. Disponibilizar a SDK do foobar2000 2025-03-07.
2. Disponibilizar WTL com `include\atlapp.h` detectável.
3. Abrir `foo_sacd_dlna.sln` no Visual Studio 2022.
4. Seleccionar `x64` e a configuração desejada.
5. Compilar a solução.
6. Registar o resultado no ficheiro de validação correspondente.

## 14. Testes existentes

A árvore inclui self-tests para:

- client registry / stream limiter;
- shared formats;
- DoP → DSF;
- library index;
- browse tree / SOAP.

Os scripts estão em `tests/*/run.sh` e alguns testes requerem ferramentas adicionais, como `ffmpeg`.

## 15. Estado de validação

A documentação histórica indica que **Alpha 3 I** foi confirmada pelo responsável do projecto como compilada e funcional em Windows Debug x64, com pfc em Debug FB2K x64.

As revisões posteriores contêm alterações adicionais. A documentação histórica também regista explicitamente que essas revisões requerem novo rebuild Windows/MSVC v142 antes de serem declaradas build-validated.

Portanto, para esta árvore `0.8-alpha3-u-dvda-flac`, o estado correcto é:

- **código/documentação presentes:** sim;
- **feature set documentado no código:** sim;
- **build desta árvore confirmado por esta documentação consolidada:** não;
- **validação de hardware T+A desta árvore:** requer teste no hardware/firmware exacto.

## 16. Estrutura principal do código

| Ficheiro | Responsabilidade principal |
|---|---|
| `main.cpp` | metadata e entrada do componente |
| `initquit.cpp` | ciclo de vida do componente |
| `dlna_server.cpp/.h` | servidor HTTP/SSDP/UPnP, publicação e streaming |
| `preferences.cpp` | Preferences e páginas Status/Settings/Maintenance |
| `config.cpp/.h` | configuração persistente |
| `sacd_decode.cpp/.h` | integração de descodificação SACD/DSD |
| `dvd_audio_flac.cpp/.h` | geração de cache FLAC para DVD-Audio |
| `dsf_writer.cpp/.h` | escrita DSF |
| `dsp_bridge.cpp/.h` | integração opcional com DSD Processor |
| `library_index.h` | índices e árvore de navegação |
| `client_registry.h` | clientes, actividade e limite de streams |
| `status.h` | snapshot de estado para UI/diagnóstico |
| `foo_sacd_dlna.rc` | recursos e layout das Preferences |

## 17. Licenciamento

O código original do projecto é indicado como licenciado sob **MIT**. SDK do foobar2000, WTL, `foo_input_sacd`, `foo_input_dvda`, DSD Processor e restantes componentes de terceiros mantêm os seus próprios termos de licença/distribuição.

Consultar `LICENSE.md`/`LICENSE`, `LICENSING.md` e a documentação de cada dependência antes de redistribuir componentes de terceiros.

## 18. Documentação histórica

Os seguintes ficheiros são registos históricos e continuam a ser úteis para auditoria:

- `CHANGELOG.md`;
- `BUILD.md`;
- `BUILD_VALIDATION_*.md`;
- `CODE_AUDIT_*.md`;
- `RELEASE_NOTES_*.md`;
- `RELEASE_CHECKLIST.md`;
- `NETWORK_*.md`;
- `WTL_*.md`;
- `HARDWARE_VALIDATION.md`;
- `DSP_PROCESSOR.md`;
- `PREFERENCES_FIELDS.md`;
- `EXAMPLES.md`.

A regra para resolver aparentes contradições é: **a documentação consolidada descreve a árvore fornecida; os documentos históricos descrevem estados anteriores da evolução Alpha 3.**
