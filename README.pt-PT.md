# Alpha 3 O — WTL relocável

**Critical fix:** all commands exposed by View → SACD DLNA now have GUIDs returned by `get_command()`. This prevents the `uBugCheck()` path that could crash foobar2000 when the View menu was opened. See `BUILD_VALIDATION_0.8_ALPHA3_L.md`.

# foo_sacd_dlna

Componente de servidor UPnP/DLNA de DSD nativo para [foobar2000](https://www.foobar2000.org/).

> **Estado: Alpha / desenvolvimento**
>
> Este repositório contém o código-fonte actual do `foo_sacd_dlna` v0.8 Alpha 3 O. Ainda não é uma versão final nem uma componente oficial do foobar2000.

**Estado da compilação:** a Alpha 3 I foi confirmada pelo responsável do projecto como compilada com sucesso e a funcionar no Windows **Debug x64**, com `pfc` em **Debug FB2K x64**, usando a SDK do foobar2000 2025-03-07, MSVC v142 e os headers WTL documentados neste projecto. A Alpha 3 M contém a integração seguinte do roadmap e o endurecimento do código, necessitando de novo rebuild no Windows. A validação de reprodução no T+A SDX 3100 HV e no firmware exacto continua a ser uma etapa separada.

## Alpha 3 M — auditoria e endurecimento (baseline histórico)

A Alpha 3 M é uma revisão de robustez sobre a árvore da Alpha 3 L. Foram reforçados o ciclo de vida do servidor, o limite de streams HTTP, a validação do manifesto de cache, o cálculo do tamanho da cache, os erros visíveis no Status, a sincronização dos diagnósticos de rede, o estado dos workers de prefetch, o acesso ao IP local e a normalização das extensões dos ficheiros.

**Estado da compilação:** a Alpha 3 I continua a ser a última versão confirmada pelo utilizador como compilada sem erros e a funcionar em runtime. A Alpha 3 M necessita de novo rebuild em Windows/MSVC v142.

## UI de buffer e diagnóstico UPnP/DLNA — Alpha 3 J

A Alpha 3 J acrescenta um painel em tempo real **SACD DLNA Status / Diagnostics**. O painel actualiza a cada 500 ms e mostra o read-ahead DSD do servidor, o estado do streaming, o TX medido, o estado HTTP/SSDP e a presença de tráfego na rede.

O indicador de buffer refere-se à **estimativa da reserva de send-buffer / read-ahead do servidor**, não ao buffer interno do T+A SDX. Durante um stream, o estado pode aparecer como `READY / FULL RESERVE`, `DRAINING / HEALTHY`, `LOW / REFILL NOT AVAILABLE` ou `DEPLETED / RISK OF UNDERRUN`.

A secção de rede distingue `SSDP NOTIFY sent`, `HTTP self-test`, `SSDP self-probe` e `NETWORK VISIBILITY`. O UI só apresenta `CONFIRMED / REMOTE SSDP M-SEARCH` ou `CONFIRMED / REMOTE HTTP` depois de um equipamento não local contactar efectivamente o servidor.

Foi adicionado **Enable Debug Diagnostics** e **Run Network Probe** às Preferences, além dos comandos equivalentes em **View → SACD DLNA**. O probe valida `device.xml`, `ContentDirectory.xml`, `ConnectionManager.xml` e executa um `M-SEARCH` SSDP para MediaServer.

## Objectivo

O `foo_sacd_dlna` foi concebido para disponibilizar a parte DSD da Music Library do foobar2000 através de UPnP/DLNA para leitores de áudio de rede compatíveis.

O alvo inicial é o **T+A SDX 3100 HV**, mantendo a seguinte política:

- apenas DSD no caminho de rede;
- nenhuma conversão DSD → PCM pelo `foo_sacd_dlna`;
- nenhum DoP enviado para o leitor de rede;
- entrega de `DSF` / `DFF` nativos quando suportados pelo equipamento;
- SACD ISO depende do **Super Audio CD Decoder (`foo_input_sacd`)** instalado separadamente;
- utilização da interface pública do decoder do foobar2000, sem carregar APIs privadas da DLL do `foo_input_sacd`.

## Funcionalidades da V0.8 Alpha 3 J

- Página dedicada **Preferências → Tools → SACD DLNA**.
- Menu próprio **View → SACD DLNA** com comandos de activar, partilhar, abrir o estado e configurar o DSD Processor.
- Elemento opcional **SACD DLNA Status**.
- Estado visível `BROADCASTING / ACTIVE`.
- Verificação da instalação do `foo_input_sacd`.
- Detecção da versão do SACD Decoder quando disponível.
- Nome do servidor configurável.
- Porta HTTP configurável.
- Activação/desactivação do broadcasting DLNA.
- Partilha da Music Library DSD.
- Estrutura **Artista → Álbum → Faixa**.
- Album art através do sistema de artwork do foobar2000.
- SSDP / UPnP MediaServer / ContentDirectory / ConnectionManager.
- HTTP `Range`.
- DSF/DFF directamente e SACD ISO com cache DSF temporário.
- DSD64 / DSD128 / DSD256.
- Ajuda integrada.
- UI de buffer/read-ahead em tempo real.
- Diagnóstico HTTP/SSDP e presença de rede.
- Probe SSDP MediaServer e validação dos endpoints UPnP locais.
- Opção de Debug Diagnostics.

## Fluxo de SACD ISO

```text
SACD ISO
   ↓
foo_input_sacd
   ↓
API pública input_decoder do foobar2000
   ↓
DoP interno
   ↓
extracção dos bits DSD
   ↓
DSF
   ↓
UPnP / DLNA / HTTP
   ↓
T+A SDX 3100 HV
```

O DoP acima é apenas um mecanismo interno entre o decoder e o componente. Não é enviado para a rede.


### Fluxo DLNA real

A versão actual implementa o fluxo principal de um MediaServer UPnP/DLNA real:

```text
T+A SDX 3100 HV
      │
      ├── SSDP discovery
      ├── Device Description
      ├── ContentDirectory Browse/BrowseMetadata
      ├── ConnectionManager GetProtocolInfo
      │
      └── HTTP GET /media/<id>.dsf
                     │
                     └── DSF / SACD ISO → foo_input_sacd → cache DSF
```

O estado do sistema mostra separadamente `BROADCASTING` e `TRANSMITTING`, incluindo TX, DSD, cliente activo e detecção do T+A.

## Requisitos de Hardware e Rede

### Hardware mínimo

O `foo_sacd_dlna` é um componente do foobar2000 e **não necessita de uma placa gráfica dedicada**.

Para uma instalação Windows prática:

| Componente | Mínimo | Recomendado |
|---|---|---|
| CPU | 2 núcleos físicos / 4 threads | 4+ núcleos físicos |
| RAM | 4 GB | 8 GB ou mais |
| Disco do sistema | SSD preferível | SSD |
| Armazenamento da música | HDD/SSD/NAS | SSD para a cache SACD/DSF |
| Rede | Ethernet 100 Mbps | Ethernet 1 Gbps |
| GPU | Não necessária | A gráfica integrada é suficiente |
| SO | Windows 7 ou mais recente | Windows actual 64-bit |
| foobar2000 | 64-bit recomendado | Versão actual 64-bit |
| `foo_input_sacd` | **Obrigatório para SACD ISO** | Versão actual |

Os requisitos oficiais actuais do foobar2000 para Windows indicam Windows 7 ou mais recente. O componente não utiliza aceleração por GPU.

### Largura de banda necessária para DSD

O `foo_sacd_dlna` mantém o DSD nativo e não o comprime.

Débitos aproximados para DSD estéreo:

| Formato | Relógio DSD | Débito aproximado |
|---|---:|---:|
| DSD64 | 2,8224 MHz | 5,64 Mbit/s |
| DSD128 | 5,6448 MHz | 11,29 Mbit/s |
| DSD256 | 11,2896 MHz | 22,58 Mbit/s |

Estes valores correspondem apenas ao payload de áudio DSD. TCP/IP, HTTP e DLNA/UPnP acrescentam algum tráfego.

Uma rede Ethernet de 100 Mbps é, portanto, **teoricamente suficiente para DSD256**, mas **Ethernet de 1 Gbps é fortemente recomendada** para obter estabilidade numa rede doméstica com outro tráfego.

### Topologia recomendada

Para o streaming DSD256 mais estável:

```text
                 Ethernet 1 Gbps
                       │
                       ▼
              ┌────────────────┐
              │ Switch Gigabit │
              └───────┬────────┘
                      │
              ┌───────┴────────┐
              │                │
              ▼                ▼
       PC Windows /       T+A SDX 3100 HV
       foobar2000
```

O PC e o SDX devem, de preferência, estar ligados por Ethernet ao mesmo switch/router.

Wi-Fi 5 GHz pode fornecer largura de banda suficiente para DSD256, mas Ethernet é preferível porque oferece latência mais previsível e é menos afectada por interferências rádio e congestionamento. O SDX 3100 HV dispõe de Ethernet 10/100/1000 Base-T e Wi-Fi.

### Rede por velocidade DSD

| Rede | DSD64 | DSD128 | DSD256 |
|---|---|---|---|
| Ethernet 100 Mbps | ✓ | ✓ | ✓* |
| Ethernet 1 Gbps | ✓ | ✓ | **Recomendada** |
| Wi-Fi 2,4 GHz | ✓* | Possível* | Não recomendada |
| Wi-Fi 5 GHz | ✓ | ✓ | Possível* |

`*` A estabilidade real depende do restante tráfego de rede, qualidade do sinal, retransmissões, desempenho do switch/router e desempenho do armazenamento.

### Stability Mode

Para redes congestionadas, activar o **Stability Mode**.

A configuração predefinida utiliza uma estratégia de read-ahead/cache de 15 segundos. Isto separa a descodificação do SACD da transmissão pela rede, para que pequenas quebras temporárias de velocidade não interrompam imediatamente a reprodução.

Em DSD256, 15 segundos de DSD estéreo correspondem a aproximadamente 42,3 MB de payload de áudio.

O Stability Mode não consegue compensar uma rede cuja velocidade sustentada seja inferior ao débito necessário pelo DSD. Nesse caso, deve ser usada Ethernet Gigabit e/ou reduzido o tráfego simultâneo.

### Armazenamento e cache

Quando é utilizada uma SACD ISO, o componente pode criar uma cache DSF antes/durante a preparação da transmissão. Recomenda-se um SSD para a cache, sobretudo quando existem outras actividades intensivas no disco.

Como referência aproximada de espaço para a cache local:

- DSD64: cerca de 20 MB/minuto
- DSD128: cerca de 40 MB/minuto
- DSD256: cerca de 81 MB/minuto

São valores aproximados para DSD estéreo; o sistema de ficheiros e o contentor DSF acrescentam algum espaço.

### T+A SDX 3100 HV

A documentação do SDX 3100 HV especifica LAN 10/100/1000 Base-T e indica DFF/DSF e DSD64, DSD128 e DSD256 para o Streaming Client. O DAC do aparelho suporta taxas DSD superiores através de outras entradas, mas isso não deve ser confundido com os formatos documentados para streaming pela rede.


## Compilação

O repositório não inclui a SDK do foobar2000. Utiliza a **SDK 2025-03-07** oficial.

Estrutura esperada:

```text
SDK-2025-03-07/
├── foobar2000/
│   ├── SDK/
│   ├── shared/
│   └── foo_sacd_dlna/
├── pfc/
└── libPPUI/
```

Abrir `foobar2000/foo_sacd_dlna/foo_sacd_dlna.sln` no Visual Studio 2022 e compilar `Release | x64`.

**Nota:** o código desta Alpha ainda não foi compilado neste ambiente, porque não está disponível aqui o ambiente MSVC/Visual Studio.

## Estado do DLNA

Quando o servidor está activo, a interface mostra:

```text
SACD DLNA:  BROADCASTING / ACTIVE
foo_input_sacd:  INSTALLED  <versão>
Music Library:  SHARING  (<N> DSD tracks)
Server: foobar2000 SACD DSD
HTTP port: 8192
```

`BROADCASTING / ACTIVE` significa que os serviços HTTP e SSDP estão activos. Não significa, por si só, que o T+A aceitou ou esteja actualmente a reproduzir uma faixa.

## T+A SDX 3100 HV

A T+A indica actualmente suporte de `DFF` e `DSF` e streaming DSD64/DSD128/DSD256 no Streaming Client do SDX 3100 HV. As entradas USB têm suporte para taxas superiores separadamente. ([T+A SDX 3100 HV](https://www.ta-hifi.de/en/audiosystems/hv-series/sdx-3100-reference-streaming-pre-dac/))

## Rede

- HTTP: `TCP 8192`
- SSDP: `239.255.255.250:1900`

Pode ser necessário criar uma regra no Windows Firewall para o foobar2000 na rede privada.

## Segurança

Esta Alpha foi concebida para uma rede local de confiança. O servidor HTTP não tem autenticação e não deve ser exposto directamente à Internet.

## Roadmap

### Concluído nesta Alpha 3 J

- `BrowseMetadata` e paginação reforçados.
- Negociação `protocolInfo` específica do renderer, reutilizando o token exacto anunciado pelo SDX quando compatível.
- Ordenação determinística das faixas por disco/faixa/título para testes gapless.

### Estado do roadmap na Alpha 3 J

- Preparação gapless mais robusta com pré-cache da faixa seguinte no servidor ✅
- DIDL-Lite completo para metadados da faixa, duração e resolução ✅
- Cache persistente de capas com detecção JPEG/PNG/WebP/GIF/BMP/TIFF ✅
- Clientes HTTP concorrentes limitados e cancelamento ✅
- Cache persistente e invalidado por versão/origem para SACD→DSF e DSP→DSF ✅
- Callbacks da Music Library e `GetSystemUpdateID` ✅
- Diagnóstico Console + `network.log` e indicação explícita de visibilidade remota na rede ✅
- Workflow GitHub Actions para build/package Windows ✅
- Descoberta T+A SDX, negociação ConnectionManager e checklist de validação ✅
- Validação externa restante: novo rebuild no Windows e teste do firmware/rede exactos do SDX 3100 HV

## Menu View → SACD DLNA / janela de estado

Na Alpha 3 J foi corrigido o caminho da interface que podia deixar o estado SACD DLNA sem abrir. O menu **View → SACD DLNA** passa a incluir explicitamente **Open SACD DLNA Status** e **Configure DSD Processor...**.

A janela de estado recebe também dimensões iniciais explícitas para que o popup tenha uma área utilizável logo na abertura. O elemento `SACD DLNA Status` continua disponível no sistema de edição de UI do foobar2000.

## Monitorização em tempo real

A V0.7 separa explicitamente o **broadcasting/descoberta DLNA** da **transmissão de áudio**:

- **DLNA discovery: BROADCASTING / ACTIVE** — os anúncios SSDP do servidor estão activos.
- **Audio stream: ACTIVE / TRANSMITTING** — um renderer DLNA está efectivamente a receber áudio por HTTP.
- **TX speed** — velocidade real medida dos dados enviados pela ligação TCP.
- **DSD rate** — taxa nominal DSD, quando conhecida.
- **DLNA client** — IP e identidade conhecida do cliente que está a receber o áudio.
- **T+A SDX** — é procurado através de SSDP/UPnP e aparece como `DETECTED / STREAMING` quando o IP detectado corresponde ao cliente que está a receber o áudio.

O áudio não é transmitido como um broadcast UDP. O SSDP serve para descoberta/anúncio; os dados de música são enviados ao renderer por HTTP.

## Stability Mode (V0.7)

A Stability Mode foi acrescentada para separar a conversão SACD ISO → DSD do caminho de rede. A ISO é convertida para DSF numa cache persistente e a transmissão só começa quando o ficheiro DSD está pronto. Antes de transmitir, o componente pode fazer um read-ahead configurável (5–60 s) e aumentar o buffer de envio TCP.

Isto ajuda a absorver picos curtos de carga do disco ou variações temporárias da rede. Não é possível garantir reprodução contínua se a largura de banda sustentada da rede ficar abaixo do débito necessário do DSD.

Débito estéreo aproximado: DSD64 = 5,64 Mbit/s; DSD128 = 11,29 Mbit/s; DSD256 = 22,58 Mbit/s.

## Current Alpha roadmap

The current alpha focuses on real renderer interoperability and diagnostics:

- complete `Browse` / `BrowseMetadata` and pagination ✅
- renderer-specific DSD `protocolInfo` negotiation using the exact negotiated Sink token where possible ✅
- deterministic track order and duration metadata for gapless testing ✅
- richer DIDL-Lite metadata and album art
- concurrent HTTP clients with cancellation
- persistent, invalidation-aware SACD→DSF cache
- Media Library callbacks and `GetSystemUpdateID`
- verbose Console + `network.log` diagnostics
- Windows GitHub Actions build packaging
- explicit T+A SDX 3100 HV firmware validation checklist

Gapless playback is deliberately marked as **renderer/firmware dependent** until it is tested on the exact SDX firmware.

## Documentação

- `HELP.md` — ajuda detalhada e resolução de problemas.
- `EXAMPLES.md` — exemplos práticos de reprodução e diagnóstico.
- `NETWORK_REQUIREMENTS.md` — requisitos de hardware e rede.
- `PREFERENCES_FIELDS.md` — referência rápida dos campos.
- `HARDWARE_VALIDATION.md` — matriz de validação do T+A SDX 3100 HV por firmware.
- `DLNA_TRACE_EXAMPLE.md` — sequência esperada de pedidos UPnP/DLNA.
- `NETWORK_DIAGNOSTICS_UI_0.8_ALPHA3_G.md` — documentação do UI de buffer/read-ahead e validação de rede.
- `BUILD_VALIDATION_0.8_ALPHA3_E.md` — registo da validação de compilação bem-sucedida desta versão.

## Real DLNA / renderer validation

The MediaServer path now separates SSDP discovery from real HTTP media transfer and records live renderer/TX status. Renderer capabilities are queried through UPnP `ConnectionManager::GetProtocolInfo` where available. Final T+A compatibility remains firmware-specific and must be validated on the exact SDX 3100 HV unit. See `HARDWARE_VALIDATION.md` and `tools/ta_sdx_probe.py`.

The Preferences page also provides **Clear DSF Cache** for invalidating generated DSF/manifests/artwork without touching source music.

See `PROTOCOL_COMPATIBILITY.md` for renderer negotiation details and `HARDWARE_VALIDATION.md` for exact-firmware testing.

## Referências

- [foobar2000 SDK](https://www.foobar2000.org/SDK)
- [T+A SDX 3100 HV](https://www.ta-hifi.de/en/audiosystems/hv-series/sdx-3100-reference-streaming-pre-dac/)
- [Super Audio CD Decoder](https://sourceforge.net/projects/sacddecoder/files/foo_input_sacd/)

## Validação de compilação — 0.8 Alpha 3 J

A Alpha 3 J é uma revisão posterior e **não foi validada neste ambiente de compilação**. A última versão que o utilizador confirmou como compilada sem erros e a funcionar é a Alpha 3 I. Consulta `BUILD_VALIDATION_0.8_ALPHA3_I.md`.

## Compilação / Build

See [`BUILD.md`](BUILD.md) for the complete Windows build guide, software/hardware requirements, Visual Studio setup, SDK configuration, testing and troubleshooting.


## Optional DSD Processor integration

The DLNA server can optionally run the installed **DSD Processor** DSP (`foo_dsd_processor`) on the audio before network delivery. This is intentionally separate from foobar2000's normal playback DSP chain.

The user keeps control of the DSD Processor preset through its normal configuration window. The private preset is stored by `foo_sacd_dlna` and its fingerprint is included in DSF cache invalidation.

Typical use:

```text
PCM → DSD Processor → DSD128 → DSF → DLNA → SDX
DSD256 → DSD Processor → DSD128 → DSF → DLNA → SDX
```

The first path can convert PCM sources to DSD for the T+A. The second can reduce DSD256 network bandwidth by configuring the DSD Processor to output DSD128. The default mode remains native DSD/bypass.

A documentação actual do `foo_dsd_processor` descreve conversão PCM→DSD e conversão entre diferentes taxas DSD. Esta integração DLNA exige intencionalmente que o resultado continue a ser DSD/DoP, para manter a transmissão em DSD nativo. DSD→PCM não é activado silenciosamente por esta opção.

- [`DSP_PROCESSOR.md`](DSP_PROCESSOR.md) — optional DSD Processor integration and configuration.

## Nota sobre compilação no Windows

O componente utiliza **MSVC v142**. A camada de helpers da SDK do foobar2000
também necessita dos headers da **WTL** (incluindo `atlapp.h`). Consulta
[`WTL_SETUP.md`](WTL_SETUP.md) para a instalação e configuração do Visual Studio.

## Build toolchain note

This repository uses **MSVC v142** with the WTL headers from the SDK tree at:

```text
<SDK root>\<WTL folder>\include
```

See `BUILD.md`, `WTL_SETUP.md` and `V142_WTL_FIX.md` for the complete configuration.

## Alpha 3 J — informação de áudio em tempo real

O painel SACD DLNA Status mostra agora música/título/artista/álbum, formato de origem e saída, tamanho de origem/saída, frequência de amostragem, canais e bits, velocidade efectiva em x-tempo-real e a cadeia de processamento activa. O estado distingue DSD nativo (`NO CONVERSION`), descodificação/cache de SACD ISO e saída do DSD Processor (`DSP OUTPUT / CACHED` ou `DSP CONVERTING`).

## Alpha 3 J — informação de áudio em tempo real

O painel Status / Diagnostics mostra agora a música actual (título/artista/álbum), formato e tamanho de origem/saída, frequência/resolução, canais e bits, TX medido, velocidade em x-tempo-real e a cadeia de processamento/conversão. Durante a criação de cache ISO ou processamento pelo DSD Processor, o estado de preparação é mostrado imediatamente.

### View → SACD DLNA menu hardening — Alpha 3 K

The complete View → SACD DLNA command set was reviewed. Library sharing is now a true toggle, the preferences command opens the dedicated SACD DLNA page directly, and refresh/clear-library/clear-cache actions are available from the same menu. DSD Processor toggling now only re-indexes an already shared/running DLNA library.
## Alpha 3 N — Windows discovery hardening

Improved Windows UPnP/DLNA discovery compatibility: LAN-interface selection for the advertised LOCATION, DLNA device namespace/description, SSDP service announcements and service-type M-SEARCH responses. Added explicit advertised LOCATION diagnostics. Windows Explorer discovery remains dependent on the Windows SSDP/Function Discovery stack and firewall configuration.



## WTL relocável — Alpha 3 O

A pasta do WTL já não tem de se chamar `WTL`. O `WTL.props` procura automaticamente, na raiz da SDK, uma pasta irmã que contenha `include\atlapp.h`. Também podes definir `WTLIncludeDir`, `WTL_INCLUDE` ou `WTL_ROOT`, ou usar um ficheiro local `WTL.user.props`. Consulta `WTL_RELOCATION.md`.


### Alpha 3 P — correcção de descoberta WTL

A descoberta do WTL foi corrigida para evitar referências a listas de itens dentro de `Condition`.


### Alpha 3 Q — WTL relocation / MSBuild fix

Alpha 3 Q removes the invalid MSBuild item-list-to-property conversion from WTL discovery. It also adds an SDK-root `Directory.Build.targets` overlay so WTL headers are injected into referenced projects such as libPPUI and foobar2000_sdk_helpers. The `tools/install_wtl_support.ps1` script auto-detects any WTL folder containing `include\atlapp.h`, independent of the folder name.

## Alpha 3 S — correcções de compilação
A Alpha 3 S corrige os erros C2664/C2668/C3487 detectados no `dlna_server.cpp` e inclui uma forma segura de propagar o WTL aos projectos `libPPUI` e `foobar2000_sdk_helpers` do SDK.

Executar uma vez após mudar o nome/localização da pasta WTL:

```powershell
.\tools\apply_sdk_wtl_patch.ps1 -SdkRoot 'D:\SDK-2025-03-07'
```


## Atualização alpha3-t

- SACD ISO é apresentado na árvore DLNA como **DSD** e o recurso anunciado ao renderer é `.dsf`; o ISO nunca é enviado diretamente.
- A partilha passou a aceitar um filtro de formatos configurável em `Shared formats` (por exemplo `dsf,dff,iso,flac,wav,mp3`).
- Quando o DSD Processor está desligado, formatos não-DSD selecionados são enviados no formato nativo.
- Quando o DSD Processor está ligado, os formatos selecionados podem ser convertidos para DSD/DSF para o caminho DLNA.
- A árvore ContentDirectory passou a publicar **Playlists** e os itens das playlists, permitindo ao renderer navegar pelas listas do foobar2000.
