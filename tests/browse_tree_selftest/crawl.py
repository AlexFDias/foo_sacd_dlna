import subprocess, xml.etree.ElementTree as ET, sys
NS = {'d':'urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/','dc':'http://purl.org/dc/elements/1.1/','upnp':'urn:schemas-upnp-org:metadata-1-0/upnp/'}
fails = 0; checks = 0
def check(c, msg):
    global fails, checks; checks += 1
    if not c: fails += 1; print("FAIL:", msg)
def browse(oid, flag='children', start=0, count=0):
    out = subprocess.run(['./harness', oid, flag, str(start), str(count)], capture_output=True, text=True, check=True).stdout
    head, _, didl = out.partition('\n'); nr, tm = map(int, head.split())
    root = ET.fromstring(didl)                       # falha se o XML nao for bem formado
    kids = []
    for e in root:
        tag = e.tag.split('}')[1]
        kids.append(dict(kind=tag, id=e.get('id'), parent=e.get('parentID'), childCount=e.get('childCount'),
                         title=e.find('dc:title', NS).text, cls=e.find('upnp:class', NS).text,
                         creator=(e.find('dc:creator', NS).text if e.find('dc:creator', NS) is not None else None)))
    return nr, tm, kids
def full(oid):
    nr, tm, kids = browse(oid); check(nr == tm == len(kids), f"{oid}: NumberReturned/TotalMatches/len {nr}/{tm}/{len(kids)}"); return kids
def paged(oid, size):
    got, start = [], 0
    while True:
        nr, tm, kids = browse(oid, 'children', start, size); got += kids
        if not kids or start + size >= tm: break
        start += size
    return got

# ---------- raiz
root = full('0')
check([k['title'] for k in root] == ['Artists','Albums','Genres','Folders','All Tracks'], f"raiz: {[k['title'] for k in root]}")
check(all(k['kind'] == 'container' and k['parent'] == '0' for k in root), "filhos da raiz sao containers com parentID=0")
m = browse('0', 'meta')[2]; check(len(m) == 1 and m[0]['childCount'] == '5' and m[0]['parent'] == '-1', "metadata da raiz")

# ---------- percorrer tudo
tracks_via = {}
def crawl(oid, via, seen_containers):
    kids = full(oid)
    for k in kids:
        check(k['parent'] == oid, f"parentID de {k['id']} deveria ser {oid}, veio {k['parent']}")
    for size in (7, 100):                                  # paginacao == listagem completa
        check([k['id'] for k in paged(oid, size)] == [k['id'] for k in kids], f"paginacao ({size}) de {oid} difere da completa")
    for k in kids:
        if k['kind'] == 'container':
            nr, tm, sub = browse(k['id'])
            check(int(k['childCount']) == tm, f"childCount de {k['id']} ({k['childCount']}) != TotalMatches ({tm})")
            md = browse(k['id'], 'meta')
            check(md[0] == 1 and md[2][0]['id'] == k['id'] and md[2][0]['title'] == k['title'], f"metadata de {k['id']}")
            if k['id'] in seen_containers: continue
            seen_containers.add(k['id']); crawl(k['id'], via, seen_containers)
        else:
            tracks_via.setdefault(via, set()).add(k['id'])
for entry in root:
    crawl(entry['id'], entry['title'], set())

allids = tracks_via['All Tracks']
check(len(allids) == 2406, f"All Tracks devolve {len(allids)} faixas distintas (esperadas 2406)")
for v in ('Artists','Albums','Genres','Folders'):
    check(tracks_via[v] == allids, f"{v}: alcanca {len(tracks_via[v])} faixas, difere de All Tracks")
print(f"faixas alcancaveis por vista: " + ", ".join(f"{k}={len(v)}" for k, v in tracks_via.items()))

# ---------- regras especificas
artists = full('artists'); check(len(artists) == 40 + 1 + 1 + 0 - 0 + 0 - 0 or len(artists) == 42, f"n. de artistas {len(artists)}")
check('Various Artists' in [a['title'] for a in artists], "compilacao agrupada em 'Various Artists'")
albums = full('albums'); titles = [a['title'] for a in albums]
check(titles == sorted(titles, key=str.lower), "Albums ordenados por titulo")
comp = [a for a in albums if a['title'] == 'Best of & <More>']; check(len(comp) == 1 and comp[0]['creator'] == 'Various Artists', "album da compilacao tem dc:creator e o XML escapa & < >")
va = next(a for a in artists if a['title'] == 'Various Artists'); va_albums = full(va['id'])
check(all(a['creator'] is None for a in va_albums), "albuns dentro de um artista nao repetem o criador")
genres = full('genres'); check([g['title'] for g in genres] == sorted([g['title'] for g in genres], key=str.lower) and 'Unknown Genre' in [g['title'] for g in genres], f"generos: {[g['title'] for g in genres]}")
folders = full('folders'); check([f['title'] for f in folders] == ['D:', 'E:'], f"raiz de Folders: {[f['title'] for f in folders]}")
d = folders[0]; dkids = full(d['id']); check([k['title'] for k in dkids] == ['Music'], f"D: -> {[k['title'] for k in dkids]}")
music = full(dkids[0]['id']); check([k['title'] for k in music] == ['DSD', 'ISO'], f"Music -> {[k['title'] for k in music]}")
iso = full(music[1]['id']); check([k['title'] for k in iso] == ['Iso 1','Iso 2','Iso 3'] and all(k['kind']=='item' for k in iso), "pasta ISO: 3 faixas por ordem")
md = browse(music[1]['id'], 'meta')[2][0]; check(md['parent'] == dkids[0]['id'], "parentID de uma subpasta")
md = browse(folders[0]['id'], 'meta')[2][0]; check(md['parent'] == 'folders', "parentID de pasta de topo e 'folders'")
tr = next(k for k in full('artist-1' if False else full('artists')[0]['id']))       # 1.o album do 1.o artista
alltr = full('alltracks'); t1 = [k['title'] for k in alltr]; check(t1 == sorted(t1, key=str.lower), "All Tracks ordenadas por titulo")

# ---------- paginacao: casos limite
nr, tm, k = browse('alltracks', 'children', 5, 0xFFFFFFFF); check(tm == 2406 and nr == 2401, f"RequestedCount=0xFFFFFFFF: {nr}/{tm}")
nr, tm, k = browse('alltracks', 'children', 4294967295, 4294967295); check(nr == 0 and tm == 2406, f"StartingIndex enorme: {nr}/{tm}")
nr, tm, k = browse('alltracks', 'children', 2400, 100); check(nr == 6 and tm == 2406, f"ultima pagina: {nr}/{tm}")
nr, tm, k = browse('alltracks', 'children', 0, 1); check(nr == 1 and tm == 2406, f"pagina de 1: {nr}/{tm}")
for bad in ('nope', 'artist-99999', 'album-abc', 'genre-', 'folder-0', 'track-999999', 'artist-', ''):
    nr, tm, k = browse(bad); check(nr == 0 and tm == 0, f"objectId invalido {bad!r} -> {nr}/{tm}")
    nr, tm, k = browse(bad, 'meta'); check(nr == 0 and tm == 0, f"metadata de objectId invalido {bad!r}")
# item: metadata e filhos de uma faixa
tid = alltr[0]['id']; nr, tm, k = browse(tid, 'meta'); check(nr == 1 and k[0]['kind'] == 'item', "metadata de faixa")
print(f"\n{checks} verificacoes, {fails} falhas")
sys.exit(1 if fails else 0)
