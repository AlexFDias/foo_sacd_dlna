# INSTALL.pt-PT.md — Instalar o foo_sacd_dlna

Este documento é para instalar um `foo_sacd_dlna.dll` já compilado no foobar2000. Se ainda não tens um build, vê primeiro o `BUILD.pt-PT.md` (ou `BUILD.md` em Inglês) — esse cobre a compilação a partir do código-fonte. Tudo abaixo assume que já tens (ou acabaste de compilar) dois ficheiros:

- `foo_sacd_dlna.dll`
- `libFLAC.dll` (Win64, 1.5.x) — só necessário para DVD-Audio → FLAC; ver passo 3.

## 1. Requisitos

- **foobar2000, 64-bit**, versão actual. Este componente é exclusivamente x64.
- **`foo_input_sacd`** — obrigatório. Sem isto o componente recusa-se a arrancar (a Consola mostra `SACD DLNA: Super Audio CD Decoder (foo_input_sacd.dll) is not installed`), mesmo que só te interesse DVD-Audio/DSF.
- **`foo_input_dvda`** — obrigatório apenas se quiseres DVD-Audio → FLAC. A partilha DSD/SACD/DSF funciona sem isto.
- **`foo_dsd_processor`** — opcional, só se quiseres processamento DSP antes de transmitir.

Instala-os da forma habitual: `File → Preferences → Components → Install...`, ou arrastando o `.fb2k-component` para a janela do foobar2000.

## 2. Copiar o componente

1. Fecha o foobar2000.
2. Encontra a pasta de componentes: `File → Preferences → Components`, e vê o destino do botão "Install", ou abre:
   ```text
   %AppData%\foobar2000-v2\user-components\
   ```
   (instalações mais antigas do foobar2000 podem usar `%AppData%\foobar2000\components\` — confirma qual delas a página de Components realmente lista).
3. Cria lá uma subpasta chamada `foo_sacd_dlna` e copia o `foo_sacd_dlna.dll` para dentro:
   ```text
   %AppData%\foobar2000-v2\user-components\foo_sacd_dlna\foo_sacd_dlna.dll
   ```

## 3. Copiar o libFLAC.dll (só para DVD-Audio, mas fácil de esquecer)

Se quiseres DVD-Audio → FLAC, o `libFLAC.dll` **tem** de ficar exactamente na mesma pasta que o `foo_sacd_dlna.dll` — não basta estar algures na máquina onde compilaste, nem numa pasta de "output" da compilação, tem de ser aquela pasta exacta do passo 2:

```text
%AppData%\foobar2000-v2\user-components\foo_sacd_dlna\libFLAC.dll
```

Este é, de longe, o erro de instalação mais comum com este componente. Se saltares este passo, todas as faixas DVD-Audio falham, uma a uma, como `HTTP 503` no renderer — que a maioria dos renderers depois mostra como um erro genérico de "formato inválido ou desconhecido", sem nada a apontar para a causa real.

Não precisas de adivinhar se acertaste: a partir desta versão, a **Consola** do foobar2000 (`View → Console`) mostra um aviso no arranque se o `libFLAC.dll` estiver em falta, indicando o caminho exacto onde procurou. Se não vires esse aviso, está tudo bem. Se nem sequer precisas de DVD-Audio, podes ignorar este passo e o aviso — a partilha DSD/SACD/DSF não é afectada de qualquer forma.

## 4. Arrancar o foobar2000 e activar o componente

1. Arranca o foobar2000.
2. `File → Preferences → Tools → SACD DLNA → Settings`.
3. Activa **DLNA**, activa **Share Music Library**.
4. Ajusta opcionalmente **Shared formats** (que formatos não-DSD/DVD-A também partilhar tal como estão), **Max streams** (predefinição 2), e **Stability Mode** / read-ahead.
5. Clica OK / Apply.

## 5. Confirmar que está a funcionar

- `File → Preferences → Tools → SACD DLNA → Status`: deve mostrar `BROADCASTING / ACTIVE`. Isto só confirma que a descoberta está activa, não que já foi transmitido algum áudio.
- `View → Console`: não deve mostrar o aviso `libFLAC.dll was not found` (a não ser que tenhas saltado deliberadamente o passo 3).
- A partir de outro dispositivo na mesma rede, abre um renderer/control point DLNA/UPnP e procura o servidor (o nome é configurável em Settings; por predefinição inclui "SACD DLNA").
- Reproduz alguma coisa. `TRANSMITTING` no painel de Status significa que há áudio a fluir neste momento.
- Para uma verificação com script a partir de outra máquina na LAN com Python 3 instalado:
  ```bash
  python tools/dlna_smoke_test.py <IP-deste-PC> 8192 --get --ssdp
  ```
  (só útil se também tiveres a árvore de código-fonte; ver `BUILD.pt-PT.md`/`EXAMPLES.md`.)

## 6. Firewall

Se nada aparecer noutros dispositivos, confirma que a Firewall do Windows permite ligações de entrada ao foobar2000 na porta configurada (predefinição `8192`) e UDP `1900` (multicast SSDP). Ver `NETWORK_REQUIREMENTS.md`.

## 7. Actualizar

Para actualizar para um build mais recente: fecha o foobar2000, substitui o `foo_sacd_dlna.dll` (e o `libFLAC.dll`, se tiver mudado) na mesma pasta, arranca o foobar2000 outra vez. As caches SACD/DSF/DVD-Audio existentes são invalidadas automaticamente se a versão de decoder/formato de cache do novo build for diferente da que as gerou — não precisas de limpar a cache manualmente.

## 8. Desinstalar

`File → Preferences → Components`, selecciona **SACD DLNA Server**, clica **Disable** ou **Uninstall**. Ou, com o foobar2000 fechado, apaga a pasta `foo_sacd_dlna` da pasta de componentes indicada no passo 2.

## Alguma coisa não está a funcionar?

Ver `HELP.pt-PT.md` para os problemas mais comuns (o do `libFLAC.dll` acima é de longe o mais frequente) e `docs/USER_GUIDE.md` para o uso diário depois de estar a funcionar.
