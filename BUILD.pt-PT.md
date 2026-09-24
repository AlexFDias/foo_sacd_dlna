# BUILD.pt-PT.md — Compilar o foo_sacd_dlna no Windows

Para a versão em Inglês, ver `BUILD.md`.

Este documento explica, passo a passo, como preparar um PC Windows para **compilar, testar e diagnosticar** o `foo_sacd_dlna`.

## Estado de build desta árvore

Esta é a **1.0.0**, a primeira revisão em que código, `.vcxproj` e documentação (`BUILD.md`, `CHANGELOG.md`, `docs/`) foram reconciliados entre si — ver a entrada "1.0.0 — first consolidated release" no topo do `CHANGELOG.md` para a lista concreta do que estava inconsistente e foi corrigido. Não é uma reescrita: é a mesma base de código Alpha (histórico completo em `CHANGELOG.md`), agora sem as contradições entre revisões que se tinham acumulado.

Isto **não** é uma afirmação de build/hardware validado — não existe aqui um toolchain Windows/MSVC para compilar esta árvore. O histórico de builds Windows anteriores (revisões *Alpha 3 E* a *Alpha 3 U*) está em `CHANGELOG.md` e não prova que a 1.0.0 compila; ver `docs/VALIDATION_STATUS.md` para o que está e não está confirmado.

Em contrapartida, esta revisão **já foi exercitada com evidência real de campo**: os logs de diagnóstico de um teste com foobar2000 real, um renderer T+A e a VLC mostraram que o único motivo, consistente e sem excepções, pelo qual a conversão DVD-Audio → FLAC falhava era o `libFLAC.dll` não estar junto do `foo_sacd_dlna.dll` instalado — não um defeito no código de validação FLAC ou no encoder. Ver o passo 15 abaixo e `FLAC_RUNTIME.md`.

O build x64 usa WTL através de `WTL.props`. A localização pode ser fornecida por `WTLIncludeDir`, `WTL_INCLUDE` ou `WTL_ROOT` — ver o passo 8.


## 1. O que é necessário para compilar

### Software obrigatório

1. **Windows 64-bit**
2. **Visual Studio 2022** ou Build Tools 2022
3. Workload **Desktop development with C++**
4. **MSVC C++ Build Tools** para x64/x86
5. **Windows SDK**
6. **foobar2000 SDK 2025-03-07**
7. **foobar2000 64-bit** para testar o componente
8. **foo_input_sacd** para testar SACD ISO

O SDK usado neste projecto é o **2025-03-07**. A página oficial do foobar2000 indica que essa versão inclui projectos para Visual Studio 2019/2022. O changelog também indica que esta versão mantém C++17 em determinados projectos da SDK. 

- SDK oficial: https://www.foobar2000.org/SDK
- Changelog da SDK: https://www.foobar2000.org/changelog-sdk

### Software opcional, mas recomendado

- Git for Windows
- 7-Zip
- Python 3.x — para os scripts de diagnóstico DLNA
- Wireshark — para analisar SSDP/HTTP
- Windows Terminal

---

## 2. Hardware recomendado para a máquina de desenvolvimento

Para **compilar** o plugin, não é necessário hardware especial.

| Componente | Mínimo prático | Recomendado |
|---|---|---|
| CPU | 4 threads | 4–8+ núcleos físicos |
| RAM | 8 GB | 16 GB ou mais |
| Armazenamento | 20 GB livres | 40 GB+ num SSD/NVMe |
| GPU | integrada | integrada é suficiente |
| Rede | 100 Mbps | Gigabit Ethernet |

A Microsoft indica actualmente para Visual Studio 2022 um mínimo de 4 GB de RAM, processador x64/ARM64 e, para soluções profissionais típicas, 16 GB de RAM recomendados; as instalações típicas necessitam de 20–50 GB livres e a Microsoft recomenda SSD.

Para este projecto, **16 GB de RAM + SSD/NVMe** é uma configuração confortável, sobretudo quando se instala o Visual Studio, símbolos, SDKs e ferramentas adicionais.

---

## 3. Instalar o Visual Studio 2022

Descarrega o Visual Studio 2022 pela Microsoft:

https://visualstudio.microsoft.com/downloads/

Pode ser usada a edição **Community**, que é suficiente para este projecto.

No instalador selecciona:

```text
Workloads
└── Desktop development with C++
```

Este workload inclui os componentes essenciais de C++ para Windows, incluindo MSBuild e as ferramentas de build C++. A documentação Microsoft identifica o workload como `Microsoft.VisualStudio.Workload.VCTools`.

### Confirmar os componentes

Na instalação, confirma pelo menos:

```text
✓ MSVC C++ x64/x86 build tools
✓ MSBuild
✓ Windows SDK
✓ C++ core tools
```

O Windows SDK é instalado normalmente com o workload de desenvolvimento desktop C++.

---

## 4. Requisitos actuais do Visual Studio

Para a documentação de referência:

https://learn.microsoft.com/pt-pt/visualstudio/releases/2022/system-requirements

Actualmente, a Microsoft documenta suporte para versões 64-bit do Windows 11 e Windows Server suportado pela edição em causa. O Visual Studio 2022 requer .NET Framework 4.8 para funcionar e o instalador utiliza WebView2 quando necessário.

Para este projecto, a escolha recomendada é simples:

```text
Windows 11 x64
Visual Studio 2022
Desktop development with C++
```

---

## 5. Obter a SDK do foobar2000

A SDK **não deve ser incluída automaticamente neste repositório** sem verificar os termos de distribuição.

Descarrega-a directamente da página oficial:

https://www.foobar2000.org/SDK

A versão actualmente publicada na página oficial é:

```text
SDK 2025-03-07
```

A página oficial indica explicitamente project files para **Visual Studio 2019/2022**.

---

## 6. Estrutura de pastas recomendada

Uma estrutura simples é:

```text
C:\dev\
│
├── SDK-2025-03-07\
│   ├── foobar2000\
│   ├── helpers\
│   ├── pfc\
│   ├── shared\
│   └── ...
│
└── foo_sacd_dlna\
    ├── foo_sacd_dlna.sln
    ├── foo_sacd_dlna.vcxproj
    ├── dlna_server.cpp
    ├── sacd_decode.cpp
    └── ...
```

Mantém os dois projectos no mesmo nível para simplificar as referências relativas usadas pela solução.

---

## 7. Abrir o projecto

Abra:

```text
foo_sacd_dlna.sln
```

no Visual Studio 2022.

Selecciona:

```text
Configuration: Release
Platform: x64
```

Para depuração:

```text
Configuration: Debug
Platform: x64
```

Não uses `Win32`/x86 para este projecto.

---

## 8. Confirmar os caminhos da SDK

Se aparecer:

```text
cannot open include file ...
```

abre:

```text
Project → Properties
```

E confirma:

```text
C/C++ → Additional Include Directories
```

bem como:

```text
Linker → Additional Library Directories
```

As pastas da SDK, `pfc`, `shared` e restantes componentes têm de corresponder à SDK que extraíste.

Não assumes um caminho fixo como `C:\foobar2000-sdk`. Usa o caminho real da tua instalação.

---

## 9. Compilar

No Visual Studio:

```text
Build → Build Solution
```

ou:

```text
Ctrl + Shift + B
```

Configuração inicial recomendada:

```text
Release | x64
```

Uma compilação bem sucedida deve criar a DLL do componente no directório de output configurado pelo projecto.

---

## 10. Compilar a partir da linha de comandos

Abre:

```text
Developer Command Prompt for VS 2022
```

e executa:

```powershell
msbuild .\foo_sacd_dlna.sln /m /p:Configuration=Release /p:Platform=x64
```

Build limpo:

```powershell
msbuild .\foo_sacd_dlna.sln /t:Clean /p:Configuration=Release /p:Platform=x64
msbuild .\foo_sacd_dlna.sln /t:Build /m /p:Configuration=Release /p:Platform=x64
```

Se `msbuild` não for encontrado, estás provavelmente a usar uma consola normal em vez da Developer Command Prompt ou Developer PowerShell do Visual Studio.

---

## 11. Preparar um foobar2000 de teste

É altamente recomendado utilizar uma **instalação/perfil separado do foobar2000** para o desenvolvimento.

Não testes uma DLL Alpha directamente na tua instalação principal de música.

O ambiente de teste deve conter:

```text
foobar2000 x64
foo_input_sacd
foo_sacd_dlna
```

Depois reinicia o foobar2000.

---

## 12. Confirmar a dependência SACD

Abre:

```text
File → Preferences → Tools → SACD DLNA
```

Deve aparecer algo semelhante a:

```text
foo_input_sacd: INSTALLED
```

O `foo_input_sacd` é obrigatório para a parte **SACD ISO → DSD**.

O componente foi desenhado para não depender de funções privadas da DLL do SACD Decoder. O objectivo é usar as interfaces públicas do foobar2000 para pedir o fluxo DSD ao decoder instalado, permitindo actualizar o decoder independentemente.

---

## 13. Primeiro teste: DSF

Antes de testar SACD ISO, usa uma faixa `.dsf` que já saibas estar correcta.

Isto testa:

```text
Music Library
      ↓
foo_sacd_dlna
      ↓
UPnP/DLNA
      ↓
T+A SDX 3100 HV
```

Se o DSF não funcionar, não vale a pena começar por investigar o SACD Decoder.

---

## 14. Segundo teste: SACD ISO

Depois de DSF funcionar, testa:

```text
Album.iso
```

O caminho esperado é:

```text
SACD ISO
   ↓
foo_input_sacd
   ↓
DSD
   ↓
DSF cache
   ↓
HTTP/DLNA
   ↓
SDX 3100 HV
```

A ISO original não deve ser alterada.

---

## 15. Terceiro teste: DVD-Audio → FLAC

Depois de DSF e SACD ISO funcionarem, testa uma faixa DVD-Audio.

Isto exige três coisas, todas obrigatórias:

1. **`foo_input_dvda`** instalado (o decoder DVD-Audio).
2. A extensão/origem DVD-Audio incluída em **Shared formats**.
3. **`libFLAC.dll` (Win64, 1.5.x) copiado para a mesma pasta onde está instalado `foo_sacd_dlna.dll`** no perfil de teste do foobar2000 — normalmente `%AppData%\foobar2000-v2\user-components\foo_sacd_dlna\` ou equivalente. A DLL está em `third_party\libFLAC\Win64\libFLAC.dll` no código-fonte; o build já a copia para a pasta de output do projecto (`$(OutDir)`), mas **isso não é a pasta de componentes do foobar2000** — tens de a copiar tu, à mão, para lá.

Este último passo é fácil de esquecer porque é uma cópia manual separada da compilação, e esquecê-lo produz um sintoma enganador: todas as faixas DVD-Audio falham, uma a uma, com HTTP 503 no renderer (e HTTP 404/503 na VLC), sem qualquer indicação óbvia de que falta um ficheiro. A partir desta revisão, se a DLL não for encontrada, o foobar2000 mostra logo no arranque, na **Consola**, uma linha do género:

```text
SACD DLNA: libFLAC.dll was not found at "...\libFLAC.dll" -- DVD-Audio to FLAC
conversion will fail for every track until it is copied there (see FLAC_RUNTIME.md).
DSD/SACD sharing is not affected.
```

Se vires esta linha, o teste vai falhar sempre — resolve isto primeiro, antes de investigar mais nada.

O caminho esperado, uma vez a DLL presente, é:

```text
DVD-Audio
   ↓
foo_input_dvda
   ↓
PCM 24-bit
   ↓
libFLAC 1.5.x (encoder real, carregado dinamicamente)
   ↓
cache .flac
   ↓
HTTP/DLNA
   ↓
SDX 3100 HV
```

Confirma a cache gerada com as ferramentas oficiais da distribuição FLAC 1.5.0 Win64:

```powershell
flac.exe -t caminho\para\a\cache\<id>.flac
metaflac.exe --list caminho\para\a\cache\<id>.flac
```

`flac -t` deve reportar o ficheiro como válido; `metaflac --list` deve mostrar o sample rate, canais, bits e total de samples esperados para a faixa. Ver `FLAC_RUNTIME.md` para os detalhes do encoder e da validação de cache.
---

## 16. Testar o servidor DLNA sem o SDX

O projecto inclui scripts em:

```text
tools\
```

O script de smoke test pode verificar a parte de MediaServer sem depender imediatamente do T+A:

```powershell
python .\tools\dlna_smoke_test.py 192.168.1.20 8192
```

Substitui `192.168.1.20` pelo IP do PC que executa o foobar2000.

O teste verifica elementos como:

```text
/device.xml
ContentDirectory::Browse
BrowseMetadata
ConnectionManager::GetProtocolInfo
/status
media resource
```

Para pedir também o recurso de áudio:

```powershell
python .\tools\dlna_smoke_test.py 192.168.1.20 8192 --get
```

---

## 17. Testar com o T+A SDX 3100 HV

A rede recomendada é:

```text
PC / foobar2000
      │
   Ethernet
      │
Gigabit Switch
      │
   Ethernet
      │
T+A SDX 3100 HV
```

O estado da interface deve distinguir:

```text
DLNA: BROADCASTING / ACTIVE
```

de:

```text
Audio stream: ACTIVE / TRANSMITTING
```

O primeiro indica que o servidor está disponível/discoverable.

O segundo indica que existe uma transferência HTTP de áudio activa.

Durante reprodução, procura algo como:

```text
T+A SDX: DETECTED / STREAMING
DSD: DSD256
TX: ~22–24 Mbit/s
```

---

## 18. Requisitos de rede

Débito aproximado do payload DSD estéreo:

```text
DSD64   ≈ 5.64 Mbit/s
DSD128  ≈ 11.29 Mbit/s
DSD256  ≈ 22.58 Mbit/s
```

TCP/IP, HTTP e UPnP acrescentam overhead.

Uma Ethernet de 100 Mbps é suficiente em teoria, mas para DSD256 a configuração recomendada é:

```text
Gigabit Ethernet
```

A razão é simples: o objectivo não é apenas ter largura de banda suficiente, mas também **ter margem** para outros dispositivos e congestionamento temporário.

---

## 19. Windows Firewall

Para o funcionamento local de DLNA, o componente usa normalmente:

```text
UDP 1900
```

para SSDP e:

```text
TCP 8192
```

para HTTP/media, por defeito.

Se mudares a porta nas Preferências, ajusta a regra do firewall.

Para o primeiro teste, permite o foobar2000 na rede **Private** do Windows.

Não exponhas este servidor DLNA directamente à Internet.

---

## 20. Testar HTTP Range

O renderer pode fazer pedidos parciais:

```http
Range: bytes=...
```

Isto é relevante para seek e determinados padrões de reprodução DLNA.

A resposta deve manter correctamente:

```text
206 Partial Content
Content-Range
Content-Length
Accept-Ranges: bytes
```

quando um pedido Range válido é recebido.

---

## 21. Diagnóstico com Wireshark

Para problemas de DLNA, Wireshark é extremamente útil.

Filtros úteis:

```text
ssdp
```

```text
http
```

```text
tcp.port == 8192
```

ou apenas o SDX:

```text
ip.addr == <IP-do-SDX>
```

Uma sessão típica deverá parecer-se com:

```text
SDX → SSDP M-SEARCH
PC  → SSDP response
SDX → GET /device.xml
SDX → SOAP Browse
SDX → SOAP BrowseMetadata
SDX → SOAP GetProtocolInfo
SDX → GET /media/<id>.dsf
```

O último pedido é o ponto em que começa a transmissão real do áudio.

---

## 22. Testar estabilidade DSD256

Configuração inicial:

```text
Stability Mode: ON
Pre-buffer: 15 s
```

Se a rede estiver muito ocupada:

```text
Pre-buffer: 20–30 s
```

Para redes muito instáveis:

```text
Pre-buffer: 30–60 s
```

Isto absorve variações temporárias. Não resolve uma ligação que fique permanentemente abaixo do débito necessário.

Para DSD256, 15 segundos correspondem a aproximadamente **42,3 MB** de payload DSD estéreo.

---

## 23. Testar cache SACD

Quando uma ISO é usada, o projecto pode criar uma cache DSF.

O comportamento pretendido é:

```text
Primeiro acesso
ISO → DSD → DSF cache

Acessos seguintes
DSF cache → DLNA
```

A cache deve ser invalidada quando o source ou os parâmetros relevantes mudam.

O objectivo é evitar que a conversão SACD seja repetida desnecessariamente e, ao mesmo tempo, desacoplar a descodificação da velocidade do cliente DLNA.

---

## 24. Teste de cancelamento/concurrency

Durante desenvolvimento, testa situações como:

1. iniciar uma faixa;
2. mudar rapidamente para outra;
3. cancelar enquanto uma ISO está a ser preparada;
4. abrir duas faixas/clients de forma concorrente;
5. desligar/reiniciar o renderer durante um streaming.

O componente deve cancelar tarefas antigas sem deixar ficheiros DSF incompletos a serem servidos como válidos.

---

## 25. Desenvolvimento no Visual Studio

Para depuração podes configurar o executável do foobar2000 como aplicação de arranque:

```text
Debug → foo_sacd_dlna Properties → Debugging
```

Exemplo:

```text
Executable:
C:\...\foobar2000.exe
```

O caminho deve apontar para a tua instalação de teste.

Áreas especialmente úteis para breakpoints:

```text
dlna_server.cpp
sacd_decode.cpp
dsf_writer.cpp
preferences.cpp
ui_element.cpp
```

---

## 26. Testes recomendados por ordem

Para reduzir o número de variáveis, testa por esta ordem:

```text
1. Compilar DLL
2. Carregar DLL no foobar2000
3. Preferences
4. foo_input_sacd detection
5. SSDP
6. device.xml
7. Browse
8. BrowseMetadata
9. DSF HTTP GET
10. HTTP Range
11. DSF no SDX
12. SACD ISO
13. DSF cache
14. libFLAC.dll presente (ver passo 15) + DVD-Audio -> FLAC
15. flac.exe -t / metaflac.exe --list na cache gerada
16. DSD64
17. DSD128
18. DSD256
19. estabilidade/rede congestionada
20. gapless
21. artwork
22. reprodução longa
```

---

## 27. CI / GitHub Actions

O projecto inclui um workflow em `.github/workflows/build.yml` que compila Debug e Release x64 e publica `foo_sacd_dlna.dll` + `libFLAC.dll` como artefacto.

**Antes de correr, tens de configurar duas repository variables** (Settings → Secrets and variables → Actions → Variables), porque a SDK do foobar2000 e o WTL não são distribuídos neste repositório (ver secção 5):

```text
FOOBAR2000_SDK_URL   URL directo do arquivo da SDK (ver https://www.foobar2000.org/SDK)
WTL_URL              URL directo de um arquivo WTL contendo include\atlapp.h
```

Sem estas variáveis definidas, o workflow falha logo no primeiro passo com uma mensagem clara, em vez de falhar de forma confusa mais tarde. Confirma que os dois URLs ainda são válidos antes de depender deste workflow — páginas de download oficiais mudam de versão em versão.

Um build automático deve produzir, no mínimo:

```text
BUILD: PASS
ARTIFACT: foo_sacd_dlna
HARDWARE VALIDATION: NOT RUN
```

Um build verde no GitHub Actions **não prova compatibilidade com o T+A**.

A validação física tem de ser feita com um SDX 3100 HV real e com o firmware exacto que estiver instalado.

---

## 28. Informações a guardar em cada release

Regista sempre:

```text
foo_sacd_dlna version
foobar2000 SDK version
Visual Studio version
MSVC toolset version
Windows SDK version
Target architecture
Configuration
Git commit
Windows version
T+A SDX firmware version
```

Exemplo:

```text
foo_sacd_dlna: 1.0.0
foobar2000 SDK: 2025-03-07
Visual Studio: 2022
Configuration: Release
Platform: x64
```

---

## 29. Requisitos para utilizador final vs. programador

### Para desenvolver/compilar

```text
Windows x64
Visual Studio 2022 / Build Tools
Desktop development with C++
MSVC
Windows SDK
foobar2000 SDK
```

### Para executar o componente

```text
Windows x64
foobar2000 x64
foo_sacd_dlna
foo_input_sacd (necessário para SACD ISO)
rede local
```

O utilizador final **não precisa de Visual Studio nem da SDK** para utilizar uma versão já compilada do componente.

---

## 30. Checklist antes de publicar uma release

```text
[ ] Release | x64 compila sem erros
[ ] Sem DLL Debug no pacote
[ ] foobar2000 carrega o componente
[ ] Preferences funciona
[ ] Help funciona
[ ] foo_input_sacd é detectado
[ ] SSDP funciona
[ ] Browse funciona
[ ] BrowseMetadata funciona
[ ] DSF abre no renderer
[ ] HTTP Range funciona
[ ] SACD ISO funciona
[ ] Cache funciona
[ ] libFLAC.dll está junto de foo_sacd_dlna.dll na pasta de componentes (não só em $(OutDir))
[ ] Consola não mostra o aviso "libFLAC.dll was not found" no arranque
[ ] DVD-Audio -> FLAC funciona (foo_input_dvda instalado + formato partilhado)
[ ] flac.exe -t / metaflac.exe --list confirmam a cache .flac gerada
[ ] DSD64 testado
[ ] DSD128 testado
[ ] DSD256 testado
[ ] Artwork testado
[ ] Cancelamento testado
[ ] Concorrência testada
[ ] Firewall documentado
[ ] Logs verificados
[ ] Firmware T+A registado
[ ] Hardware T+A testado
```

---

## 31. Referências oficiais

### foobar2000

SDK:
https://www.foobar2000.org/SDK

SDK changelog:
https://www.foobar2000.org/changelog-sdk

### Microsoft

Visual Studio 2022 — requisitos:
https://learn.microsoft.com/pt-pt/visualstudio/releases/2022/system-requirements

Desktop development with C++ / workload:
https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools

MSVC Build Tools:
https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc

Windows C++ development:
https://learn.microsoft.com/en-us/cpp/windows/overview-of-windows-programming-in-cpp

## 32. WTL e o toolset v142

A camada de helpers da SDK do foobar2000 precisa dos headers do WTL, além do ATL. Este projecto usa o toolset **v142**:

```xml
<PlatformToolset>v142</PlatformToolset>
```

Não mudes o componente para v143 isoladamente.

O nome da pasta WTL não é fixo — o projecto descobre automaticamente, via `WTL.props`, uma pasta irmã que contenha `include\atlapp.h`:

```text
<pasta raiz da SDK>\<pasta WTL>\include\atlapp.h
```

Podes também indicar o caminho explicitamente, com qualquer uma destas três propriedades MSBuild: `WTLIncludeDir`, `WTL_INCLUDE` ou `WTL_ROOT` (por exemplo em `WTL.user.props`).

### Verificar o WTL

A partir de uma Developer PowerShell do Visual Studio:

```powershell
.\tools\check_build_env.ps1
```

ou, com o caminho explícito:

```powershell
.\tools\check_build_env.ps1 -WtlInclude '<pasta raiz da SDK>\<pasta WTL>\include'
```

### Compilar com WTL auto-descoberto ou explícito

```powershell
.\tools\build.ps1 -Configuration Debug -Platform x64
.\tools\build.ps1 -Configuration Release -Platform x64
.\tools\build.ps1 -Configuration Debug -Platform x64 -WtlInclude '<pasta raiz da SDK>\<pasta WTL>\include'
```

### Caminho da shared library da SDK

O projecto resolve a `shared-x64.lib` da SDK como:

```text
$(SolutionDir)..\shared\shared-x64.lib
```

isto é, relativo à pasta onde colocaste a SDK (ver passo 6 — "Estrutura de pastas recomendada"). Não é um caminho absoluto fixo; ajusta a estrutura de pastas em vez de editar este caminho.
