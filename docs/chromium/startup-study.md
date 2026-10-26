# Estudio del arranque y la restauración de Fly

Estudio realizado el 26 de octubre de 2026 sobre el estado publicado de
`main` en `3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1`. El objetivo es reducir
el tiempo entre abrir Fly y poder ver y usar la página que interesa.
Este trabajo añade medición y documentación; no cambia el comportamiento del navegador.
El registro compacto contiene 118 muestras de apertura, incluidas ocho con
trazas, además del microbenchmark separado de los launchers.

## Resultado principal

Hay trabajo prescindible alrededor de la página activa. La oportunidad más
concreta es evitar que Fly construya/precaliente la interfaz web de la barra de
direcciones de Chromium, que Fly sustituye por una interfaz nativa propia.
También hay escrituras repetidas del contenido de usuario, recursos de nueva
pestaña precalentados aunque se vaya a abrir una web y actualizaciones de listas
que recorren todas las pestañas.

La restauración ya es progresiva. No corresponde sustituirla por otro sistema
de sesiones ni asumir que todas las pestañas compiten por la red desde el
primer instante. La prioridad debe ser acortar el camino de la página visible
y evitar trabajo duplicado conservando las garantías de Chromium.

## Qué se ha medido

El ejecutable instalado en `/opt/fly-browser/fly-browser`, cuya procedencia
declara el commit anterior, con sandbox y extensiones normales. El registro
de resultados conserva su SHA-256. La máquina tiene un Ryzen 7 5800X y 32 GiB
de RAM; el ejecutable está en el SSD del sistema. Los perfiles principales
son desechables en el `tmpfs` de `/tmp`. Es un límite favorable para el coste
de almacenamiento del perfil, no una reproducción del perfil cotidiano.

Se usa Xvfb con Openbox en un escritorio propio de 1440 × 1000. No se toca
la sesión gráfica del propietario ni su perfil. El servidor HTTP local devuelve
HTML pequeño sin caché; tiene variantes con 1200 ms de espera y 250 ms de JavaScript.
Las pestañas de la serie principal comparten sitio. No representa 60 sitios
independientes con sus procesos, anuncios, vídeo, sesiones autenticadas e iframes.

Las medidas principales tienen cinco muestras por escenario en orden rotatorio.
No llevan tracing, que añade trabajo. Las trazas se capturan aparte.
Se usa CDP directamente, sin las opciones ni la emulación de foco de Playwright.
Solo se inspecciona la página activa durante la medición. Las pestañas de fondo
se cuentan sin ejecutar JavaScript en ellas ni activarlas. En la serie
principal se abren juntas durante la preparación y solo se selecciona la
primera. Se añaden casos separados que visitan todas las pestañas antes de
cerrar, porque su recencia modifica la política de restauración.

Los perfiles llevan únicamente dos preferencias de control: la versión vista
se toma de `package.json` y la oferta de integración de escritorio se marca
como presentada. Así no aparece una oferta o una página de novedades sobre la
página medida. Se mantienen la comprobación de actualizaciones y los servicios
normales. Se usa `--password-store=basic` para no involucrar el llavero del usuario.

### Hitos distintos

| Hito | Qué significa | Qué no demuestra |
| --- | --- | --- |
| Inicio | Antes de crear el proceso hijo | No incluye el tiempo de pulsar el icono del escritorio |
| Puerto CDP | Chromium ya acepta una conexión de diagnóstico | No significa ventana ni página listas |
| Ventana gestionada | Openbox ha registrado el cliente X11; muestreo de 5 ms | No garantiza que el primer fotograma haya llegado al monitor |
| `navigationStart` | Se ha creado la navegación del documento | Todavía puede no haberse enviado la petición |
| Primera petición | El servidor local recibe la petición del documento | No incluye DNS/TLS de un sitio real |
| FCP | El documento ha pintado su primer contenido | No certifica LCP, INP, hidratación ni aplicación completamente lista |
| `load` | Ha terminado el evento de carga del documento | Puede preceder al primer pintado |
| Prueba de entrada | El botón de la página responde a eventos de ratón por CDP | Se comprueba después de la observación; no mide el instante mínimo de interactividad |

El tiempo de FCP se calcula con `performance.timeOrigin + FCP − inicio`,
no con el momento tardío en que la automatización consigue conectarse.
Se conservan los tiempos del documento y las peticiones para revisar esa cuenta.
Los relojes de pared se suponen estables durante cada arranque; una corrección
brusca del reloj invalidaría la muestra. No se confunde un FCP de 50 ms desde
la navegación con un arranque del proceso de 50 ms.

### Serie principal

Tiempos desde la creación del proceso, en milisegundos. En la entrega de un
enlace a un Fly existente, desde la creación del segundo proceso que lo entrega.
El JSON adjunto contiene cada muestra y los agregados, sin redondeo previo.

| Escenario | Muestras | FCP mediana | FCP mínimo–máximo | Primera petición, mediana |
| --- | ---: | ---: | ---: | ---: |
| Perfil nuevo, nueva pestaña | 5 | 533 | 481–1626 | — |
| Perfil nuevo, URL local | 5 | 413 | 373–1199 | 313 |
| Reabrir con nueva pestaña | 5 | 450 | 348–1232 | — |
| Restaurar 1 pestaña | 5 | 373 | 307–931 | 257 |
| Restaurar 12 pestañas | 5 | 415 | 353–962 | 268 |
| Restaurar 60 pestañas | 5 | 522 | 424–1270 | 370 |
| 12 pestañas, activa espera 1200 ms | 5 | 1502 | 1448–2081 | 281 |
| 12 pestañas, fondo espera 1200 ms | 5 | 377 | 320–1112 | 263 |
| Recuperar 12 tras cierre forzado | 5 | 427 | 376–1202 | 274 |
| Arrancar en privado | 5 | 426 | 404–1480 | — |
| Enlace externo con Fly abierto | 5 | 88 | 65–265 | 48 |

La carga de otros trabajos en la estación varió durante la serie. Los máximos
de alrededor de uno o dos segundos no son una regresión demostrada de Fly.
Una serie exploratoria anterior, con menos carga, daba aproximadamente
290–370 ms para los casos ligeros y 406–457 ms al restaurar 60 pestañas.
Se conserva como exploración, separada de la serie validada. No se usa para
atribuir una mejora a cambios de código.

No se ha medido un arranque tras reiniciar el sistema ni con caché de disco
vacía. «Proceso cerrado», «perfil nuevo» y «máquina fría» son condiciones
distintas. No se han vaciado las cachés del sistema mientras otros trabajos
están en marcha.

## Vista de pájaro

```text
Icono / comando / enlace externo
  ├─ Fly existente → ProcessSingleton → navegador existente → navegación
  └─ proceso nuevo
       → cargador del sistema, recursos y configuración inicial
       → toolkit, hilos, servicios, Local State
       → selección/carga de perfiles de Chromium
       → creación de BrowserView y ventana
            ├─ interfaz original de Chromium y precargas heredadas
            └─ FlyApp → PrepareProfile → sidebar, pie y controladores de Fly
       → sesión de Chromium → WebContents y selección restaurados
            ├─ página visible → navegación y carga inmediata
            └─ fondo → política escalonada de Chromium
       → red / caché / servicio worker / extensiones
       → documento, CSS, JavaScript, fuentes e imágenes
       → layout, composición y FCP → aplicación utilizable
```

Es un grafo con tareas paralelas. No se pueden sumar las duraciones inclusivas
de `StartupBrowserCreator::Start`, sus hijos y todos los procesos para obtener
el tiempo de espera del usuario. Hay que buscar el camino que impide presentar
la página visible. Tampoco toda tarea asíncrona es gratuita: consume CPU,
disco, colas e IPC aunque no bloquee directamente el hilo de interfaz.

## Recorrido, enlace por enlace

### 1. Qué se ejecuta realmente

El paquete instalado arranca directamente el navegador. El pequeño comando
`/usr/bin/fly-browser` añade las opciones de `fly-browser-flags.conf` y hace
`exec`. No consulta GitHub antes de abrir la ventana.

[launch.sh] es un camino de desarrollo diferente. Busca primero `FlyNative`,
después `FlyRelease`; no recompila y puede arrancar un binario más antiguo que
el código. `npm start` añade el coste de npm antes del script.

La modalidad descargada, `--release` o `--pr`, consulta metadatos/checksums de
GitHub incluso con una copia en caché. Sus `curl` no tienen límites explícitos
de conexión o duración total. Una red lenta puede retrasar la apertura antes
de que exista Chromium. La descarga, verificación y descompresión son coste
de distribución, no renderizado. Mejoras posibles: plazos de red y una política
explícita de frescura de la caché; arrancar primero una copia verificada y
comprobar después supone elegir cuándo se aplica una actualización.

En un microbenchmark separado de 20 ejecuciones medidas, sustituyendo el
navegador por un ejecutable vacío, la mediana fue 0.8 ms al invocarlo,
3.8 ms mediante `launch.sh` con compilación local y
84.5 ms mediante `npm start`. Se mide el coste de despacho,
no el arranque de Fly. Arrancar la instalación directamente evita npm; eso no
acelera el renderizador una vez iniciado. La modalidad de descarga no está
incluida en esas cifras.

### 2. Arranque del motor y perfil

Chromium inicializa el toolkit, Local State, servicios e hilos y crea el
perfil seleccionado. El launcher puede encontrar un proceso del mismo perfil
y entregarle el enlace mediante `ProcessSingleton`: por eso el caso de proceso
existente es mucho más corto. Mantener un proceso vivo deliberadamente para
conseguir ese resultado cambia el consumo en reposo y la semántica de cerrar
Fly; es una decisión de producto.

La selección del perfil y el arranque de los perfiles abiertos siguen
[StartupBrowserCreator]. La [integración de sesión] fuerza `LAST` en Fly cuando
corresponde, respetando políticas administradas e incógnito. La [integración
de perfiles] evita el selector de Chromium por defecto. No hay una segunda
base de datos de URLs de Fly para reconstruir la sesión.

### 3. Ventana original más interfaz de Fly

La [factoría de ventanas] crea un `BrowserView` de Chromium, inicializa su
widget y acopla `FlyApp` y `FlyWindowController`. El [marco de Fly] reserva
espacio para barra lateral y pie. Esto conserva las responsabilidades nativas
de ventanas, pestañas, seguridad y contenido.

Sin embargo, conservar `BrowserView` también conserva trabajo invisible.
En el binario publicado aparecen dos targets `browser_ui`:
`chrome://omnibox-popup.top-chrome/` y su variante `omnibox_popup_aim.html`.
La barra flotante de Fly utiliza `CommandPalette`, no esas páginas.
El constructor de `BrowserView` llama además a
`WebUIContentsPreloadManager::WarmupForBrowser`.

Se hizo una comparación adicional con cuatro muestras por configuración y
escenario, alternando qué configuración iba primero. El experimento desactiva
`WebUIOmniboxPopup`, `WebUIOmniboxAimPopup` y `PreloadTopChromeWebUI`.
Los dos targets desaparecen en todas sus muestras. No cambia el binario ni
se modifica la configuración del propietario.

| Escenario | FCP normal | FCP experimental | PSS normal → experimental | CPU normal → experimental |
| --- | ---: | ---: | ---: | ---: |
| URL local nueva | 332 ms | 305 ms | 462 → 426 MiB | 1.25 → 0.91 s |
| Nueva pestaña | 369 ms | 344 ms | 478 → 445 MiB | 1.19 → 0.94 s |
| Restaurar 60 | 450 ms | 444 ms | 455 → 420 MiB | 0.94 → 0.72 s |

PSS y CPU son las sumas de los procesos que CDP enumera y siguen vivos al
muestrear, aproximadamente dos segundos después de abrir el puerto. No son
un inventario completo de todos los ayudantes del navegador ni la CPU de
todos los procesos que ya hayan terminado. PSS es memoria proporcional;
CPU es tiempo acumulado de ejecución, no tiempo de espera del usuario.

En la web nueva, la diferencia entre medianas es 27 ms, alrededor del 8 %.
Los rangos se solapan: normal 317–401 ms y experimento 245–480 ms. Es una
señal favorable, no una garantía estadística ni de GPU física. Con 60 pestañas
la diferencia de 6 ms queda dentro de la variación. La reducción de unos
33–36 MiB de PSS enumerada es más consistente que la mejora de latencia.
Una exploración anterior con orden fijo produjo una diferencia de 55 ms en
la web; no se adopta esa cifra más optimista como resultado principal.

El cambio correcto debe estar limitado a las ventanas decoradas por Fly y
verificar que herramientas, ventanas de extensiones, popups y cualquier ruta
que conserve controles de Chromium siguen funcionando. Los flags del
experimento son una herramienta diagnóstica, no una modificación del producto.

### 4. Servicios y construcción de Fly

En [FlyApp], el primer acceso construye servicios de espacios, comandos,
preferencias, marcadores y sitios frecuentes. Registra fuentes, nueva pestaña,
historial, extensiones y contenido de usuario, instala observadores y aplica
preferencias. Después, `PrepareProfile` aplica preferencias al perfil y lo
registra con extensiones, scripts y zoom.

En [FlyWindowController], cada ventana construye sidebar, pie y controladores
de superficies; conecta historial, descargas, notificaciones y preferencias.
Las instancias de `CommandPalette` son principalmente controladores: sus
widgets y filas se crean al abrirlas. `KeyboardEditor` también pospone la
construcción del editor visible. **No hay evidencia para atribuir el arranque
a construir por adelantado todo el panel de Preferencias.**

Oportunidades concretas, con ahorro todavía sin aislar:

| Componente | Trabajo actual | Mejora compatible con la función |
| --- | --- | --- |
| [Fuentes] | Registra fuentes de aplicación una vez y cachea `FontList` por cara/tamaño/peso | Mantener lo ya ganado; perfilar fontconfig antes de cambiarlo |
| [Contenido de usuario] | Lee entradas, genera un hash y escribe cinco archivos atómicamente en cada generación, incluso sin cambios | Comparar versión/contenido persistido y reutilizar archivos completos; reparar archivos ausentes o dañados |
| [Recursos de nueva pestaña] | Calienta el lote completo a prioridad `USER_BLOCKING` al registrar la página, aunque el arranque vaya a una web | Separar registro de precarga, servir primero lo necesario y compartir lecturas pendientes; mantener primer fotograma correcto |
| Arte lateral | Lee tres PNG por ventana en un worker; decodifica en la respuesta del hilo UI | Compartir recursos inmutables por proceso y decodificar fuera de la UI con objetos transferibles seguros |
| Preferencias | `OnPreferencesChanged` puede llamar dos veces a `ApplyFlyPalette` cuando cambia el tema | Quitar la duplicación demostrada; beneficio probablemente pequeño |
| Catálogo de espacios | `WorkspaceService::Read` valida y clona el catálogo; múltiples consultas lo repiten | Snapshot validado e invalidación por revisión; conservar recuperación y precedencia de políticas |

Los recursos instalados de nueva pestaña suman 739744 bytes en 15 archivos;
no es una descarga de red, pero sí trabajo en disco/colas. Las fuentes ya
tienen guardia de registro único y caché. Proponer simplemente «cachear las
fuentes» repetiría una mejora que ya existe.

El contenido de usuario no se debe retrasar sin más hasta después del FCP:
puede tener scripts `document-start`, estilos y el manejador de correo.
Reutilizar una generación válida es distinto de dejar la primera navegación
sin sus reglas. La misma cautela aplica a la extensión de bloqueo incluida.

### 5. Reconstrucción de la sesión

[SessionRestore] lee la sesión del perfil, recrea ventanas y `WebContents`,
restaura selección, navegación, identidad y almacenamiento de sesión.
`ShowBrowser` inicia la pestaña seleccionada. Las demás se entregan a
[SessionRestoreDelegate] y a [BackgroundTabLoadingPolicy].

En el motor fijado, la política tiene un máximo de cuatro cargas simultáneas
de fondo, un mínimo objetivo de cuatro y un máximo de veinte cargas de fondo
para esta restauración, además de condiciones de memoria y antigüedad. Esos
valores son de la política, no «siempre se cargan veinte pestañas» ni un límite
para las páginas activadas por el usuario. Una carga no libera necesariamente
su hueco al recibir el HTML: se considera su estado de carga/actividad.

En nuestra observación de unos dos segundos solo se pidieron dos o tres
documentos al restaurar 60 pestañas. **Esto no demuestra que las restantes
permanezcan descargadas para siempre**. La observación larga y los eventos
de la política permiten ver la continuación de la cola.

La página activa empieza a navegar antes de acabar todo el trabajo accesorio.
Restaurar más pestañas puede retrasar su petición/FCP aunque la primera ventana
ya exista: el modelo, los observadores, la barra lateral, favicons, servicios y
cargas de fondo también usan el hilo UI y el renderer.

[TabSidebar] ya conserva filas, evita rehacerlas por cada cambio de título y
agrupa refrescos mediante tareas pendientes. Aun así, `Refresh` recorre el
modelo completo, calcula estado y consulta datos para cada fila. Conviene
separar cambios estructurales de actualizaciones de una fila y medir sesiones
grandes. No se ha demostrado aquí una complejidad cuadrática del arranque.

Fly corrige la ventana activa al recibir la notificación de restauración. Con
varios espacios, cada uno es un perfil y cada ventana tiene interfaz propia;
hay costes adicionales de perfil y ventana que una prueba de 60 pestañas en
una sola ventana no representa.

### 6. Perfil interrumpido y cierre limpio

La [integración de primera ejecución] evita pantallas de bienvenida ajenas a
Fly. Tras un cierre interrumpido también solicita la restauración que antes
ofrecía la burbuja de Chromium, mediante una tarea y un `CrashedLock`.
Se prueba por separado: no se debe «acelerar» marcando un perfil como cerrado
correctamente sin restaurar su estado.

El fixture prepara las sesiones y deja transcurrir el periodo de agrupación
de guardado antes de cerrar o matar el proceso. Después comprueba el número
exacto de pestañas restauradas. Se descartó una exploración con una espera
insuficiente que preparaba una sesión incompleta. Esa exploración no es
evidencia de pérdida de pestañas durante el uso normal de Fly.

### 7. Red, renderizador y página

Después de iniciarse la navegación intervienen resolución DNS, conexión,
TLS, redirecciones, caché, service workers, verificaciones de seguridad,
extensiones, respuesta del servidor, subrecursos, ejecución de scripts,
layout y composición. Algunos de esos pasos se solapan. Una página que
retiene 1200 ms su HTML añade aproximadamente esa espera al resultado; una
optimización de 40 ms en la ventana no elimina la espera del servidor.

La barra de Fly ya prepara conexiones al origen de la opción seleccionada.
La precarga completa de resultados es opcional y está desactivada por defecto;
no precarga búsquedas. Ese camino beneficia la navegación con Fly abierto,
no resuelve por sí solo la primera página restaurada.

Una posible conexión anticipada a la pestaña activa restaurada debería
respetar el perfil, proxy, privacidad, políticas y reglas de extensiones.
No se propone adelantar conexiones a todas las URLs de la sesión ni sustituir
las verificaciones de seguridad por una percepción de rapidez.

La conservación del contenido anterior durante una navegación, que ya tiene
Fly, ayuda cuando existe una página pintada. Al arrancar un proceso no hay
contenido anterior real que conservar. Una captura persistida puede dar
continuidad visual, pero añade almacenamiento, privacidad, invalidación y el
riesgo de presentar como interactivo algo que todavía no lo es.

### 8. Nueva pestaña e historial

La nueva pestaña tiene HTML/CSS iniciales con tema y geometría definitivos,
datos inertes escapados, precarga de fuentes y caché inmutable de recursos.
No espera el viaje `ready` para aplicar el aspecto correcto. El fondo nativo
también coincide con el de Fly. Se deben conservar estas garantías.

`NewTabHistory` evita consultas duplicadas y solo consulta cuando tiene
suscriptores. Sin embargo, su `EnsureQuery` llama a `HistoryService::Query`
con el valor por defecto de `loaded_only`: puede cargar perfiles de espacios
cerrados para obtener sus datos. Asíncrono no significa que no compita con la
página inicial. Limitarse a perfiles cargados sería más barato, pero cambiaría
los sitios frecuentes visibles: no es una mejora gratuita sin diseñar una
instantánea persistida, invalidación por borrado y límites entre perfiles.

| Caso adicional | Muestras | FCP mediana | FCP mínimo–máximo |
| --- | ---: | ---: | ---: |
| Restaurar 3 espacios/perfiles | 3 | 631 ms | 448–645 ms |
| Restaurar 4 ventanas del mismo perfil | 3 | 762 ms | 542–808 ms |
| 12 pestañas, 250 ms de JS en el fondo | 3 | 388 ms | 303–460 ms |
| Nueva pestaña con 2 espacios cerrados | 3 | 401 ms | 372–493 ms |
| Web con 2 espacios cerrados | 3 | 314 ms | 298–326 ms |
| 12 pestañas visitadas antes de cerrar | 3 | 313 ms | 301–354 ms |
| 60 pestañas visitadas antes de cerrar | 3 | 412 ms | 393–423 ms |
| HTML inicial rápido, final 1200 ms después | 3 | 255 ms | 240–329 ms |

Las trazas del caso de espacios cerrados muestran tres llamadas a
`Profile::CreateProfile` al abrir la nueva pestaña y una al restaurar la web.
Confirma la carga adicional de perfiles. La diferencia de FCP entre esos dos
casos no se puede atribuir exclusivamente al historial: sus documentos también
son diferentes.

En el documento transmitido por partes, la mediana de FCP es 255 ms y la de
`load` 1396 ms. Eso ilustra por qué medir únicamente cuándo deja de cargar
la página confunde contenido ya disponible con carga completa.

Una comprobación adicional con perfiles en SSD (`/var/tmp`), tres muestras por
caso, dio medianas de 306 ms para URL nueva y 304 ms para restaurar 12 pestañas.
No demuestra que SSD sea más rápido que RAM: se ejecutó después y con otra
carga del equipo. Sí comprueba el recorrido fuera de tmpfs; sigue siendo caché
del sistema caliente y un perfil sintético pequeño.

La observación larga de 60 pestañas distingue su uso previo: en diez segundos
se pidieron cinco documentos con las pestañas de fondo sin visitar y quince
cuando se habían activado todas antes de cerrar. La traza registra cuatro
inicios de fondo en el primer caso dentro de sus cinco segundos y continuación
de la cola en el segundo. La política usa la antigüedad, además de memoria y
concurrencia. **No extrapolar el número de cargas de fondo de una sesión
sintética a cualquier sesión habitual.**

### 9. Trabajo posterior a la primera ventana

`AttachWindow` publica `RunFirstWindowTasks` en la cola normal: novedades,
comprobación de actualización e integración de escritorio. `PostTask` no
garantiza ejecutarse después del primer FCP. El chequeo de actualización es
asíncrono, pero abre red y devuelve trabajo a la interfaz. Es candidato a
planificación después de la presentación inicial o con un límite de espera.
En una actualización, la pestaña de novedades puede ocupar el primer plano;
el estudio la suprime de forma explícita y no mide esa experiencia.

El temporizador de suspensión de Fly trabaja cada 30 segundos; no es el
candidato principal para los primeros cientos de milisegundos. Las preferencias
se reaplican con una clave de deduplicación para ajustes por perfil. La
lectura general de historial no ocurre simplemente por construir su controlador.

## Qué dicen las trazas sobre el coste de cada paso

Estas duraciones son de una ejecución instrumentada por caso, con cachés
calientes. Sirven para localizar trabajo; no son el resultado A/B sin tracing.

| Duración observada | Web nueva | Restaurar 60 pestañas |
| --- | ---: | ---: |
| Inicializar toolkit | 30,9 ms | 28,4 ms |
| Fase inicial `CreateProfile` | 33,8 ms | 30,3 ms |
| `StartupBrowserCreator::Start`, inclusivo | 101,7 ms | 150,1 ms |
| Mayor tarea de refresco de sidebar en UI | 2,1 ms | 18,7 ms |
| Respuesta de arte lateral en UI | 3,2 ms | 3,0 ms |
| Lectura del lote de nueva pestaña en worker | 0,31 ms | 0,32 ms |
| Generación de contenido de usuario en worker | 0,22–0,33 ms | 0,22 ms |

Las filas de toolkit/perfil/creador son fases inclusivas del motor y no se
suman entre sí ni con sus hijos. Los tiempos pequeños de archivos corresponden
a este perfil pequeño, tmpfs y caché caliente. No prueban que todo disco sea
barato al abrir tras reiniciar.

En las tres ventanas de espacios distintos, las respuestas de arte lateral
ocupan aproximadamente 3,2–3,5 ms de UI cada una. Los refrescos de sidebar de
60 pestañas acumulan unos 23 ms en cinco segundos, con una tarea inicial de
18,7 ms. En cambio, no hay evidencia de cientos de milisegundos dentro de la
lectura de los recursos de Fly en estas condiciones.

El campo `src_func` de una tarea de Chromium identifica **quién la publicó**.
Así, una tarea originada en `ScheduleRefresh` ejecuta el refresco publicado;
no mide el pequeño método que programa la tarea. Tampoco una tarea originada
en `AttachWindow` equivale a medir todo el constructor de una ventana. El
registro compacto separa los hilos y conserva esta distinción.

En la comparación equilibrada se obtuvo registro de ventana para las 24
muestras. Para URL nueva, la mediana pasó de 247 a 225 ms; la primera petición,
de 249 a 228 ms. Con 60 pestañas, la primera ventana rondó 215 ms en ambas
configuraciones y la primera petición rondó 299/295 ms. Son hitos distintos.
El observador exploratorio de la serie principal se interrumpió en una carrera
X11 al desaparecer una ventana; se corrigió antes de la comparación y no se
publica una mediana incompleta de ventana para esa serie.

## Qué mejorar primero

| Prioridad | Cambio | Evidencia actual | Coste o límite |
| --- | --- | --- | --- |
| 1 | Evitar WebUI de omnibox que Fly no usa | Targets observados y experimento separado | Validar todas las clases de ventana y controles alternativos |
| 2 | Refrescos por fila y snapshot validado del catálogo | Refresco de 18,7 ms en la sesión grande y recorridos identificados | Medir cierre, orden, foco, accesibilidad y sesiones con más espacios |
| 3 | Reutilizar generación válida de scripts/estilos | Cinco escrituras atómicas incondicionales; coste pequeño en tmpfs | Mantener reglas listas y reparar corrupción; beneficio en disco sin aislar |
| 4 | Caché compartida de arte y decodificación fuera de UI | Lectura por ventana y decodificación en respuesta UI | Ahorro de tiempo sin aislar; más interesante con muchas ventanas |
| 5 | Registrar nueva pestaña sin calentar siempre todo el lote | Precarga explícita incluso al arrancar una web | Coste medido pequeño con caché caliente; no empeorar su primera apertura |
| 6 | Trabajo de mantenimiento tras la presentación inicial | Tareas publicadas sin relación con primer pintado | Novedades/actualización deben seguir llegando con plazo acotado |
| 7 | Evaluar PGO y optimización de enlace en la distribución | La configuración de release desactiva PGO | Necesita perfiles compatibles, compilación y A/B del mismo motor |

«Gratis» significa conservar capacidades y garantías. No significa cero
ingeniería ni una mejora en milisegundos ya demostrada. No se atribuye un ahorro
numérico a los puntos que solo se han identificado por inspección.

La compilación distribuida ya es optimizada, estática y sin DCHECKs.
El control de desarrollo es una compilación por componentes y no sirve como
competidor para atribuir ganancias al producto. Chromium documenta que ese
modo tiene arranque más lento. La [documentación de PGO] describe cómo obtener
perfiles; aquí no se ha compilado ni medido una variante con PGO.

## Cambios de producto que podrían aportar más

1. **Restaurar primero la página de la ventana activa y aplazar el fondo.**
   Chromium ya tiene priorización. Un aplazamiento adicional hasta el FCP o un
   plazo corto debe conservar pestañas fijadas, comunicaciones de fondo,
   selección rápida, split view y comportamiento con varias ventanas.
2. **Restaurar diferidamente espacios cerrados o ventanas no prioritarias.**
   Puede ahorrar mucho más que optimizar botones, pero cambia cuándo reaparece
   una ventana y cuándo un sitio vuelve a estar conectado.
3. **Mantener un proceso residente tras cerrar la última ventana.**
   El caso de entrega de enlaces prueba el beneficio potencial de reutilizar
   el proceso. Su coste es mantener memoria y servicios activos deliberadamente.
4. **Mostrar una representación de la página anterior durante la carga.**
   Mejora continuidad visual, no la navegación, y exige indicar claramente
   cuándo la página real está disponible.

## Instrumentación que conviene añadir antes de cambiar la arquitectura

Añadir eventos propios de Fly a la traza: construcción de `FlyApp`, fuentes,
`PrepareProfile`, sidebar/pie, `BuildSurfaces`, carga/decodificación de arte,
refrescos de sidebar/espacios, registro/lectura de nueva pestaña, generación
de contenido de usuario y primer trabajo de mantenimiento. Incluir contadores
de perfiles, ventanas, pestañas y lecturas/escrituras, sin URLs del usuario.

Los eventos actuales permiten ver tareas cuyo origen es un archivo de Fly,
pero no adjudicar todo el tiempo inclusivo de crear una ventana a una función
concreta de Fly. Esa instrumentación debe preceder a prometer «el constructor
X cuesta Y ms». El próximo cambio debe comparar el mismo binario base,
misma máquina y fixture, con un único cambio causal, tanto con poca carga como
con competencia de CPU/disco.

## Reproducir y revisar

Se necesitan Node con `WebSocket` global, Python, Xvfb, Openbox y libX11.
El harness no instala herramientas ni añade checks obligatorios a CI.

```sh
python3 tests/chromium/startup-desktop.py --work-dir /tmp/fly-startup-desktop -- \
  node tests/chromium/startup-study.cjs \
  --binary /opt/fly-browser/fly-browser \
  --output /tmp/fly-startup-results --repeat 5
```

El wrapper abre un display nuevo, observa ventanas y lo cierra al terminar.
Si las herramientas viven fuera de `PATH`, admite `--xvfb` y `--openbox`.
Un Openbox instalado en un prefijo privado también necesita sus rutas
`LD_LIBRARY_PATH` y `XDG_DATA_DIRS`. No reutilizar el display de otra prueba.

Opciones del harness:

- `--scenarios` selecciona una lista separada por comas; los nombres están
  en `knownScenarios` al principio del script.
- `--profile-root` crea perfiles nuevos en otro volumen. Nunca reutiliza un perfil.
- `--settle-ms 10000` observa más tiempo la cola de fondo.
- `--trace` produce trazas JSON de cinco segundos, separadas de las cifras base.
- `--extra-arg` añade una opción al binario para un experimento explícito.
- `--compare-unused-omnibox` intercala la configuración normal y el experimento
  de WebUI; invierte su orden en las repeticiones impares.
- `--keep-profiles` conserva los perfiles sintéticos para investigar una ejecución.

Los logs y trazas completos se guardan fuera del repositorio. El [registro
compacto de medidas](https://github.com/Arzuparreta/fly-browser/blob/main/docs/chromium/startup-measurements.json) conserva muestras, condiciones,
agregados y resúmenes de trazas. No incluye perfiles del propietario ni URLs
de su sesión. Para emparejar la ventana, buscar el PID de cada muestra en
`windows.jsonl` y restar `startEpochMs`; con varias ventanas se toma la primera.

La captura de startup sigue el [mecanismo de tracing de Chromium]. Las trazas
reales pueden contener URLs y títulos; las de este estudio usan perfiles y
contenido sintéticos. No se ha convertido una medición en un escritorio Xvfb y
datos de prueba en aceptación de GPU física, Wayland, arranque tras reinicio,
perfil de meses de uso, sitios autenticados, red móvil ni Windows/macOS.

[launch.sh]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/launch.sh
[FlyApp]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/fly_app.cc
[FlyWindowController]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/fly_window_controller.cc
[marco de Fly]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/fly_browser_frame.cc
[factoría de ventanas]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/patches/0001-native-workspace-window.patch
[integración de sesión]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/patches/0009-native-session-startup.patch
[integración de perfiles]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/patches/0019-native-workspace-profile-startup.patch
[integración de primera ejecución]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/patches/0035-native-no-chromium-first-run.patch
[Fuentes]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/fly_theme.cc
[Contenido de usuario]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/user_content.cc
[Recursos de nueva pestaña]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/new_tab_ui.cc
[TabSidebar]: https://github.com/Arzuparreta/fly-browser/blob/3d7a8ae3656e3e99fcf823a995fd5694b29bf8c1/chromium/overlay/fly/ui/tab_sidebar.cc
[StartupBrowserCreator]: https://chromium.googlesource.com/chromium/src/+/5869fe313571d75152b77813cf0a4ee26a4e6df0/chrome/browser/ui/startup/startup_browser_creator.cc
[SessionRestore]: https://chromium.googlesource.com/chromium/src/+/5869fe313571d75152b77813cf0a4ee26a4e6df0/chrome/browser/sessions/session_restore.cc
[SessionRestoreDelegate]: https://chromium.googlesource.com/chromium/src/+/5869fe313571d75152b77813cf0a4ee26a4e6df0/chrome/browser/sessions/session_restore_delegate.cc
[BackgroundTabLoadingPolicy]: https://chromium.googlesource.com/chromium/src/+/5869fe313571d75152b77813cf0a4ee26a4e6df0/chrome/browser/performance_manager/policies/background_tab_loading_policy.cc
[documentación de PGO]: https://chromium.googlesource.com/chromium/src.git/+/HEAD/docs/pgo.md
[mecanismo de tracing de Chromium]: https://www.chromium.org/developers/how-tos/trace-event-profiling-tool/recording-tracing-runs/
