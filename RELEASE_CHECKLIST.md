# Release Checklist

Antes de uma release pública, confirma tudo isto numa máquina Windows real, com o build de foobar2000 alvo.

## Histórico de build já confirmado

- [x] `0.8 Alpha 3 E` — build Debug x64 sem erros de compilação/link.
- [x] `0.8 Alpha 3 I` — build Debug x64 confirmado pelo responsável do projecto, componente a correr.
- [x] Revisões `Alpha 3 J`–`Alpha 3 U` e a árvore actual — apenas revisão de código; **não** foram recompiladas neste ambiente (não há aqui toolchain Windows/MSVC). Não tratar como build-validadas até um build Windows real ser feito e registado aqui.

## Validação ainda pendente (build/instalação)

- [ ] Visual Studio Release x64 compila de forma limpa com a SDK fixada.
- [ ] `foo_input_sacd` é detectado.
- [ ] **`libFLAC.dll` (Win64, 1.5.x) está copiado para a pasta real de componentes do foobar2000**, não só para `$(OutDir)` do build. A Consola não mostra `"libFLAC.dll was not found"` no arranque.

## Validação ainda pendente (funcional)

- [ ] DSF DSD64 reproduz
- [ ] DSF DSD128 reproduz
- [ ] DSF DSD256 reproduz
- [ ] SACD ISO é descodificado através do `foo_input_sacd` instalado
- [ ] DVD-Audio → FLAC reproduz com `foo_input_dvda` instalado (ver `BUILD.md`, secção 15)
- [ ] `flac.exe -t` valida a cache `.flac` gerada; `metaflac.exe --list` confirma sample rate/canais/bits/samples esperados
- [ ] Browse Root / Artist / Album / Track funciona
- [ ] BrowseMetadata funciona
- [ ] Artwork carrega
- [ ] HTTP Range funciona
- [ ] TX rate ao vivo é mostrado
- [ ] T+A SDX é detectado correctamente
- [ ] Negociação de capacidades via `ConnectionManager` é registada
- [ ] Não ocorre conversão DSD→PCM no caminho de rede
- [ ] A cache é invalidada após alterações relevantes de source/decoder
- [ ] O cancelamento não deixa artefactos `.partial` em cache
- [ ] Callbacks de add/remove/modify da Music Library actualizam o servidor
- [ ] Orientação de firewall do Windows testada
- [ ] Firmware exacto do SDX registado em `HARDWARE_VALIDATION.md`
- [ ] Teste de 30+ minutos em DSD256 concluído
- [ ] Comportamento gapless registado como PASS/FAIL, nunca assumido

## Evidência real já recolhida (não é validação de release, mas é diagnóstico útil)

Um teste real com foobar2000 + T+A SDX + VLC (ver `docs/VALIDATION_STATUS.md`) já confirmou, a partir de `network.log` e do diagnóstico da VLC, que:

- a lógica de validação FLAC (STREAMINFO + cadeia de metadata) e o `SO_SNDTIMEO` de 15s no socket de streaming **não** produziram falhas significativas na sessão mais recente;
- **100% das falhas de conversão DVD-Audio → FLAC** nessa sessão foram `libFLAC.dll 1.5.x was not found next to foo_sacd_dlna` — ou seja, um passo de instalação em falta, não um defeito de código.

Isto não substitui os testes funcionais acima, mas explica porque é que testes anteriores falhavam e deve ser confirmado como resolvido (DLL presente, aviso da Consola ausente) antes de repetir os testes de DVD-Audio.
