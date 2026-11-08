# Estudio de las cachés de Fly

Estudio realizado el 8 de noviembre de 2026 sobre `main` en
`8e87983ef8b22822a263d9b437a1a27281154b98` (Fly 0.42.3, Chromium
157.0.8103.38). Mide las cachés que Fly añade por su cuenta y las cachés de
disco de Chromium que Fly hereda, para decidir en una pull request posterior
si se cambia algún tamaño o alguna caché. Este trabajo solo añade medición y
documentación; no cambia el comportamiento del navegador.

Los datos están en [`cache-measurements.json`](./cache-measurements.json) y
las herramientas en `tests/chromium/cache-study/`.

## Resultado principal

| Caché | Qué se ha medido | Conclusión |
| --- | --- | --- |
| HTTP de Chromium (por perfil) | Con las visitas reales del propietario deja de mejorar a partir de 32 MiB. Con 40 sitios públicos, a partir de 128–256 MiB. El límite actual en esta máquina es 800 MiB | El tamaño solo importa en disco: ningún límite medido por encima de 256 MiB devuelve más aciertos. Hay margen para fijar un límite, no hay un problema que lo exija |
| Código JS de Chromium (por perfil) | Límite actual 200 MiB en esta máquina. El perfil real usa 136 MiB; la reproducción real, 36–44 MiB | `--disk-cache-size` y la política `DiskCacheSize` limitan también esta caché con el mismo número |
| Favicons de marcadores (Fly, 256 entradas, 5 min) | Acierto 1 µs; fallo 150–350 µs. Abrir el selector con 1000 marcadores: 87 ms con la caché vacía y 85 ms con la caché llena (mediana) | Ahorra trabajo en segundo plano, no tiempo visible. El tamaño 256 no limita nada en la práctica: la caducidad de 5 minutos vacía la caché antes |
| Catálogo validado de workspaces (Fly) | Lectura con caché 0,8–5,5 µs; sin caché 9–108 µs (1 a 50 workspaces). 32 validaciones en una sesión de 75 s, 0,2 ms en total | Correcta y barata. Nada que decidir |
| Recursos de la nueva pestaña (Fly) | Primera carga 86 ms con el disco frío, 6 ms con el disco caliente. Retiene 7,18 MiB de fondos decodificados (claro y oscuro) más 0,47 MiB de archivos | La caché sirve. Retener los dos temas cuesta 3,6 MiB que solo se usan al cambiar de tema |
| Dibujos del panel lateral y script del modo lectura (Fly) | 0,92 MiB y 221 KiB; decodificación 1,2–1,5 ms | Nada que decidir |

En las trazas de uso habitual no aparece trabajo de Fly repetido y caro que
pida una caché nueva. El único coste grande, `Fly.UserContent.Generate`
(0,8–1,0 s), ocurre al crear un perfil nuevo en un disco mecánico; en los
arranques siguientes tarda 0,2 ms porque ya reutiliza los archivos generados.

Quedarse como está es una opción válida para todas las cachés. Las decisiones
posibles están en [Opciones para la próxima pull request](#opciones-para-la-proxima-pull-request).

## Qué se ha medido y cómo

- **Máquina.** Ryzen 7 5800X, 32 GiB de RAM, `/home` en un SSD SATA y
  `/mnt/storage` en un disco mecánico de 7200 rpm. Las pruebas con navegador
  usan una pantalla virtual (Xvfb `:93` con Openbox); no tocan la sesión ni
  el perfil del propietario.
- **Perfil real del propietario.** Se leyeron copias de los índices de la caché
  HTTP y de la caché de código del perfil `Default` del Fly instalado y una copia
  de su historial. Este documento y el JSON solo contienen agregados: ninguna
  URL ni dominio del historial.
- **Reproducción de visitas** (`replay.mjs`). Lanza `out/FlyRelease/chrome`
  con un perfil desechable y un límite `--disk-cache-size`, navega por una
  secuencia de URL y cuenta por CDP, en todas las pestañas, iframes y workers,
  qué peticiones sirve la caché de disco y cuántos bytes llegan por red. Cada
  visita espera el evento `load` y después 1,5 s sin actividad de red (máximo
  10 s). Las ocho ejecuciones de cada carga corren en paralelo, con cachés en
  el disco mecánico.
  - **Carga real.** Las 122 visitas finales (no redirecciones) del historial
    del propietario entre el 29 de octubre y el 6 de noviembre, sin las de red
    local, `localhost` y tailnet: 107 visitas, 57 URL distintas, 12 hosts. Se
    recorre dos veces seguidas.
  - **Carga sintética.** 300 visitas a portadas de 40 sitios públicos de
    noticias, compras, vídeo, documentación y servicios, elegidas con pesos
    1/rango (Zipf) y semilla 7; salieron 38 sitios distintos. Una pasada. La
    lista está en el JSON.
- **Microbenchmark** (`cache_bench_browser_test.cc`). Una prueba de
  `fly_browser_tests` en `out/FlyNative` (build de componentes con DCHECK, así
  que los tiempos absolutos son pesimistas). Mide la caché de favicons con
  400 favicons en el historial de un perfil de prueba, el selector de
  marcadores con 1000 marcadores, y `WorkspaceService::Read` con 1, 5, 20 y
  50 workspaces.
- **Trazas** (`trace.mjs`). Dos arranques de `out/FlyRelease/chrome` con
  perfil nuevo y traza de las categorías `browser`, `toplevel`, `views` y `ui`
  durante 75 s: diez pestañas nuevas, tres rondas de cambio entre ellas, tres
  navegaciones y cierre de cinco pestañas.
- **Tamaños calculados** (`preferred_size.py`). Reproduce
  `disk_cache::PreferredCacheSize` de `net/disk_cache/cache_util.cc` en esta
  versión de Chromium.

## 1. Cachés de disco de Chromium

### Cómo se calcula su tamaño hoy

Fly no fija ningún tamaño. Al abrir cada perfil, Chromium calcula el máximo a
partir del espacio libre en el disco de la caché: en Linux la caché HTTP usa
cuatro veces el valor base de 80 MiB y la caché de código JS una vez, con
estos topes:

| Espacio libre | Caché HTTP | Caché de código JS |
| --- | ---: | ---: |
| 1 GiB | 205 MiB | 102 MiB |
| 2 GiB | 410 MiB | 200 MiB |
| 4–16 GiB | 800 MiB | 200 MiB |
| 20 GiB | 819 MiB | 205 MiB |
| 32 GiB o más | 1280 MiB | 320 MiB |

`/home` tiene 6,2 GiB libres (97 % ocupado), así que el perfil del propietario
puede llegar a 800 MiB de caché HTTP y 200 MiB de código JS. Cada workspace es
un perfil y calcula su propio máximo: con N workspaces el máximo en disco es N
veces esa suma. El propietario usa hoy un solo perfil.

La preferencia de `Local State` `DiskCacheSize` (la misma que fijan
`--disk-cache-size` y la política de empresa) sustituye ese cálculo por un
número fijo, igual para todos los perfiles, y se aplica **a la caché HTTP y a
la caché de código JS** (`ChromeContentBrowserClient::GetGeneratedCodeCacheSettings`).
La reproducción lo confirma: con cada límite, las dos cachés terminaron con
tamaños parecidos.

### El perfil real del propietario

| | Caché HTTP | Caché de código JS |
| --- | ---: | ---: |
| Tamaño | 335,7 MiB (6240 entradas) | 136,3 MiB (3896 entradas) |
| Máximo actual | 800 MiB | 200 MiB |
| Usado en el último día | 2,3 MiB | 26,9 MiB |
| Usado en los últimos 3 días | 112,8 MiB | 46,1 MiB |
| Usado en los últimos 7 días | 153,6 MiB | 83,3 MiB |
| Entrada usada hace más tiempo | 12,7 días | 11,7 días |
| Tamaño de entrada p50 / p90 / p99 / máx. | 9,8 KiB / 62 KiB / 490 KiB / 73,8 MiB | 3,2 KiB / 46,5 KiB / 585 KiB / 4,5 MiB |
| Las 10 entradas más grandes | 125,9 MiB | 34,6 MiB |

La caché HTTP se creó el 26 de octubre y su entrada más antigua es de ese día:
nunca ha tenido que expulsar nada. Más de un tercio de sus bytes son diez
entradas. Las columnas de "usado" cuentan el último uso de cada entrada, no
cada acceso, así que describen el conjunto de trabajo, no la tasa de aciertos.
El índice de la caché de código se escribió por última vez el 6 de noviembre
porque Fly sigue abierto desde entonces.

### Aciertos según el límite

Carga real (107 visitas, dos pasadas). "Aciertos" es el porcentaje de
peticiones servidas por la caché de disco; "Red" son los bytes recibidos por
red (comprimidos).

| Límite | Aciertos 1.ª / 2.ª pasada | Red 1.ª / 2.ª pasada | Caché HTTP final |
| ---: | ---: | ---: | ---: |
| 8 MiB | 56,2 % / 57,4 % | 44,5 / 44,2 MiB | 7,3 MiB |
| 16 MiB | 61,5 % / 76,3 % | 35,4 / 33,3 MiB | 14,4 MiB |
| 32 MiB | 61,2 % / 83,5 % | 31,7 / 15,4 MiB | 29,2 MiB |
| 64 MiB | 61,9 % / 83,6 % | 31,4 / 10,6 MiB | 31,6 MiB |
| 128 MiB | 61,5 % / 83,9 % | 31,5 / 10,3 MiB | 31,4 MiB |
| 256 MiB | 62,0 % / 83,8 % | 31,9 / 11,2 MiB | 31,5 MiB |
| 512 MiB | 62,3 % / 83,7 % | 31,5 / 11,0 MiB | 30,8 MiB |
| sin límite (1280 MiB) | 56,3 % / 79,5 % | 150,2 / 397,6 MiB | 37,8 MiB |

Con estas visitas toda la caché cabe en unos 32 MiB; por encima no cambia
nada. La ejecución sin límite es una excepción que no depende del tamaño: su
caché acabó en 37,8 MiB, muy lejos de su máximo, pero hizo un 22 % más de
peticiones y recibió mucho más por red. Una de las páginas sirvió contenido
distinto en esa ejecución; no se ha identificado cuál.

Carga sintética (300 visitas, 40 sitios):

| Límite | Aciertos | Red | Caché HTTP final | Caché JS final |
| ---: | ---: | ---: | ---: | ---: |
| 8 MiB | 4,6 % | 560,7 MiB | 7,2 MiB | 7,1 MiB |
| 16 MiB | 26,7 % | 532,1 MiB | 14,0 MiB | 14,4 MiB |
| 32 MiB | 43,2 % | 476,9 MiB | 28,9 MiB | 29,1 MiB |
| 64 MiB | 56,2 % | 353,4 MiB | 58,1 MiB | 58,2 MiB |
| 128 MiB | 59,0 % | 239,7 MiB | 113,3 MiB | 118,1 MiB |
| 256 MiB | 61,1 % | 176,6 MiB | 146,3 MiB | 162,9 MiB |
| 512 MiB | 59,6 % | 193,8 MiB | 169,3 MiB | 155,9 MiB |
| sin límite (1280 MiB) | 60,6 % | 263,4 MiB | 253,7 MiB | 181,0 MiB |

La tasa de aciertos se aplana desde 128 MiB y los bytes por red dejan de bajar
desde 256 MiB. Las diferencias por encima son variación del contenido: la
ejecución sin límite hizo 40 537 peticiones frente a unas 35 000 de las demás.
La tasa no llega al 100 % porque buena parte de las respuestas no se pueden
guardar o caducan (anuncios, medición, datos dinámicos).

El tiempo total de carga no sirve para comparar límites: las ocho ejecuciones
compartían red y CPU y la suma varía sin relación con el tamaño.

## 2. Favicons de marcadores

`BookmarkFavicons` (`ui/bookmark_favicons.h`) guarda hasta 256 iconos ya
decodificados por perfil. Una entrada con icono caduca a los 5 minutos y una
sin icono a los 30 segundos. Solo la usa el selector de marcadores
(`CommandPalette::AppendRow`), que pide un icono por cada fila que construye;
con la ventana del test construye 34 filas al abrir y otra vez con cada tecla.

| Tamaño pedido | Fallo con icono (p50 / p90) | Fallo sin icono (p50) | Acierto (p50) | Píxeles por entrada |
| ---: | ---: | ---: | ---: | ---: |
| 16 px | 273 / 399 µs | 82 µs | 1 µs | 1 KiB |
| 32 px | 144 / 165 µs | 77 µs | 1 µs | 4 KiB |
| 64 px | 333 / 399 µs | 81 µs | 1 µs | 16 KiB |

Un fallo es una consulta asíncrona al hilo del historial más la decodificación
del PNG en el hilo de la interfaz. Con 256 entradas, la memoria máxima es de
256 KiB a escala 1 (16 px), 1 MiB a escala 2 (32 px) y 4 MiB en el tamaño
máximo permitido (64 px), más las URL de las claves.

Selector con 1000 marcadores (seis aperturas con la caché vacía y seis con la
caché llena, alternas), hasta que las 34 filas tienen icono:

| | Caché vacía (p50 / máx.) | Caché llena (p50 / máx.) |
| --- | ---: | ---: |
| Abrir | 87 / 275 ms | 85 / 256 ms |
| Una tecla (`Site 9`) | 78 / 95 ms | 54 / 120 ms |

Las 34 consultas que ahorra una apertura suman 5–10 ms de trabajo en segundo
plano, que no cambian el tiempo visible: el coste lo pone la construcción de
las filas. El límite de 256 entradas tampoco decide nada en la práctica: para
volver a pedir un icono hay que reabrir el selector o teclear, y si han pasado
más de 5 minutos el icono ya ha caducado. El perfil del propietario tiene un
marcador.

## 3. Catálogo validado de workspaces

`WorkspaceService` guarda el catálogo ya validado y lo invalida cuando cambia
la preferencia.

| Workspaces | Lectura con caché | Lectura sin caché |
| ---: | ---: | ---: |
| 1 | 0,8 µs | 8,9 µs |
| 5 | 1,2 µs | 15,8 µs |
| 20 | 2,6 µs | 40,7 µs |
| 50 | 5,5 µs | 107,8 µs |

"Sin caché" es una instancia nueva por lectura menos su construcción
(4,8 µs). En las dos trazas hubo 32 validaciones (`Fly.Workspaces.Validate`),
0,2 ms en total.

## 4. Recursos estáticos de Fly

| Caché | Memoria retenida | Coste de cargar sin caché |
| --- | ---: | --- |
| Nueva pestaña (`NewTabResources`): 15 archivos y los fondos decodificados | 0,47 MiB de archivos + 7,18 MiB de dos mapas de bits de 1680 × 560 RGBA | `Fly.NewTabResources.ReadBundle`: 86 ms con el disco frío, 6 ms con el disco caliente; en un hilo de fondo |
| Dibujos del panel lateral (`SidebarArtwork`) | 0,92 MiB (dos de 336 × 336 y uno de 128 × 128) | `Fly.SidebarArtwork.ReadAndDecode`: 1,2–1,5 ms |
| Script del modo lectura (`WithReaderScript`) | 221 KiB (113 KiB en UTF-16) | lectura de 113 KiB |

Las tres duran lo que dura el proceso. La nueva pestaña decodifica y conserva a
la vez el fondo oscuro (`memory-chasis@2x.png`) y el claro
(`memory-papel@2x.png`), aunque en cada momento se pinta uno.

## 5. Dónde más podría servir una caché

Las trazas agrupan el trabajo de Fly en el hilo principal y en los hilos de
fondo durante las acciones descritas. Valores de las dos trazas:

| Evento | Veces | Total | Máximo |
| --- | ---: | ---: | ---: |
| `Fly.UserContent.Generate` | 2 | 0,84–0,96 s | 0,84–0,96 s |
| `Fly.NewTabResources.ReadBundle` | 1 | 6–86 ms | 6–86 ms |
| `Fly.Sidebar.Refresh` | 112 | 15 ms | 1,1 ms |
| `Fly.SidebarArtwork.ReadAndDecode` | 1 | 1,2–1,5 ms | 1,5 ms |
| `Fly.Workspaces.Validate` | 32 | 0,2 ms | 0,02 ms |
| Tareas de `ReadDesktopIntegration` | 2 | 23 ms | 23 ms |

- `Fly.UserContent.Generate` tarda casi un segundo la primera vez en un perfil
  nuevo: escribe cinco archivos con `ImportantFileWriter`, que sincroniza cada
  uno con el disco, y estos perfiles estaban en el disco mecánico. La segunda
  llamada tarda 0,2 ms porque compara los archivos existentes y no reescribe
  nada. Corre en un hilo de fondo. No es un caso de caché.
- `Fly.Sidebar.Refresh` se ejecuta muchas veces (112 en 75 s), pero cada una
  cuesta 0,14 ms de media.
- Nada más de Fly pasa de 1 ms repetido.

## 6. Espacio en disco del propietario

No son cachés de Fly nativo, pero ocupan el mismo disco casi lleno:

| Carpeta | Tamaño | Qué es |
| --- | ---: | --- |
| `~/.config/fly-browser/Partitions` | 1,5 GB | Datos de la etapa Electron (`default` y `workspace-1`): 999 MB de caché HTTP, 227 MB de código, 224 MB de service workers, IndexedDB, cookies y datos de extensiones. Sin cambios desde el 21 de octubre; ningún código de Fly la usa |
| `~/.cache/fly-browser` | 489 MB | Cachés HTTP y de código del perfil actual |
| `~/.config/fly-browser-dev` | 748 MB | Perfil de desarrollo de `./launch.sh` |
| `~/.cache/fly-browser-launch` | 549 MB | Paquetes descargados por `./launch.sh` |

`Partitions` contiene sesiones y datos, no solo caché: borrarla es una decisión
del propietario.

## Opciones para la próxima pull request

1. **Caché HTTP y de código: dejar el cálculo de Chromium.** Es lo que hay. En
   esta máquina permite 800 + 200 MiB por perfil y nunca ha llegado a llenarse.
2. **Fijar `DiskCacheSize` por defecto** (por ejemplo, un valor por defecto de
   la preferencia en `Local State`, que el usuario o la política pueden
   cambiar). Un valor de 256 MiB cubre las dos cargas medidas sin pérdida de
   aciertos HTTP y acota el disco a 512 MiB por workspace. Hay que tener en
   cuenta que ese número limita también la caché de código: con 256 MiB, el
   límite de código **sube** de 200 a 256 MiB en esta máquina; con 128 MiB baja
   a 128 MiB, por debajo de los 181 MiB que alcanzó la carga sintética. El
   efecto de limitar la caché de código en la velocidad de las páginas no se
   ha medido.
3. **Favicons: dejarla como está.** Alternativas medidas: reducir el límite a
   64 entradas (sin efecto observable) o quitarla (unas 34 consultas al
   historial por apertura o tecla, sin cambio visible en este equipo).
4. **Nueva pestaña: conservar solo el fondo del tema activo.** Ahorraría
   3,6 MiB por proceso a cambio de decodificar el otro al cambiar de tema
   (unos pocos ms en un hilo de fondo).
5. **Workspaces, panel lateral, modo lectura: dejarlas.** No hay nada medido
   que justifique tocarlas.

## Límites del estudio

- La reproducción comprime ocho días de visitas en unos 20 minutos por pasada, así que
  caduca menos contenido que en el uso real. Las visitas van sin sesión
  iniciada, sin las direcciones locales y con perfiles nuevos; el historial del
  propietario es pequeño (12 hosts). La carga sintética visita solo portadas.
- Las cachés de la reproducción estaban en un disco mecánico y las ocho
  ejecuciones compartían red y CPU. Esto no cambia qué peticiones acierta la
  caché, pero invalida los tiempos de carga.
- Solo se ha medido la tasa de aciertos de la caché HTTP; la de la caché de
  código JS no.
- Los tiempos del microbenchmark vienen de un build con DCHECK y de una base
  de favicons con 400 entradas; los selectores tienen seis muestras por caso.
- Una sola máquina.

## Reproducir

Desde la raíz del repositorio, con `out/FlyRelease/chrome` compilado y una
pantalla virtual en `:93`:

```sh
# Una ejecución de la reproducción: límite en MiB o "default", pasadas y puerto.
node tests/chromium/cache-study/replay.mjs \
  /mnt/storage/fly-chromium/src/out/FlyRelease/chrome /ruta/run-zipf-64 64 visitas.json 1 9400
# Resumen de varias ejecuciones (lee out-<carga>-<límite>.json del directorio).
python3 tests/chromium/cache-study/analyze.py /ruta/con/resultados
# Conjunto de trabajo de una caché existente (copia del índice).
python3 tests/chromium/cache-study/working_set.py tests/chromium/cache-study \
  copia/the-real-index
# Límites de Chromium según el espacio libre.
python3 tests/chromium/cache-study/preferred_size.py
# Traza y resumen.
node tests/chromium/cache-study/trace.mjs \
  /mnt/storage/fly-chromium/src/out/FlyRelease/chrome /ruta/trace 9601
python3 tests/chromium/cache-study/analyze_trace.py /ruta/trace/trace.json resumen.json
```

`visitas.json` es una lista JSON de URL. El microbenchmark se ejecuta como
explica la cabecera de `tests/chromium/cache-study/cache_bench_browser_test.cc`.
