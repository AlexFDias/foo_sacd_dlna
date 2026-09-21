# Windows Network / DLNA Discovery — Alpha 3 N

## Problema investigado

O Windows usa SSDP para descobrir dispositivos UPnP/DLNA e a vista **File Explorer → Network** depende da pilha de descoberta do Windows, incluindo SSDP Discovery / Function Discovery. A ausência de um Media Device no Explorer não prova, por si só, que o servidor HTTP esteja offline.

## Correcções nesta versão

- Seleccção do IP anunciado passou a usar `GetAdaptersAddresses()` e a preferir interfaces activas Ethernet/Wi-Fi com endereços privados e gateway. Isto evita anunciar por engano uma VPN, Hyper-V, VirtualBox ou outra interface virtual.
- O `device.xml` passou a declarar o namespace DLNA e `X_DLNADOC=DMS-1.50`.
- SSDP `ssdp:alive` / `ssdp:byebye` passou a anunciar também `ContentDirectory:1` e `ConnectionManager:1`.
- O servidor responde a `M-SEARCH` para esses dois service types, além de root/device/UUID.
- O Status HTTP mostra explicitamente a `Advertised LOCATION` que os outros dispositivos devem conseguir abrir.
- A versão anunciada no `SERVER`/`modelNumber` é agora sincronizada com `VERSION`.

## O que o Windows precisa

A Microsoft documenta que a descoberta UPnP/SSDP usa UDP 1900; a descoberta de dispositivos na rede também depende dos serviços **Function Discovery Provider Host**, **Function Discovery Resource Publication**, **SSDP Discovery** e **UPnP Device Host**, bem como das regras de firewall de descoberta.

## Teste decisivo

1. Abrir `http://IP-ANUNCIADO:8192/device.xml` num browser no mesmo PC e noutro equipamento da LAN.
2. No Status, executar **Run Network Diagnostics**.
3. Confirmar `REMOTE SSDP M-SEARCH` ou `REMOTE HTTP` quando um segundo dispositivo consultar o servidor.
4. No Windows Explorer, activar Network Discovery e actualizar a vista Network.

Se o servidor funcionar via HTTP e o SDX o descobrir, mas não aparecer apenas no Explorer do próprio PC, isso deve ser tratado como comportamento da camada de descoberta do Windows, não como falha da entrega DLNA.
