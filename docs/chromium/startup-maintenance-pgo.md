# Mantenimiento inicial y optimización de la distribución

Seguimiento de los puntos 6 y 7 del [estudio de arranque](./startup-study.md),
después de la [primera tanda de mejoras](./startup-improvements.md).

## Mantenimiento después de presentar la página

La comprobación de actualizaciones, la página de novedades y la oferta de
integración de escritorio esperan a que la página seleccionada haya pintado
contenido y el compositor haya presentado un fotograma posterior de la ventana.
Un documento que sigue descargándose puede cumplir esta condición: no se espera
a `load`. Una pestaña ya pintada también puede cumplirla sin navegar otra vez.

El observador sigue los cambios de pestaña y ventana seleccionadas. Solicita
un solo redibujado para que una página estática ya pintada pueda presentar
ese fotograma. No sondea, no activa pestañas de fondo ni mantiene un proceso
residente. Los callbacks pendientes se invalidan al cambiar de documento,
ocultar o destruir la ventana.

Hay un plazo de cinco segundos para páginas lentas, vacías, renderizadores
caídos o ventanas minimizadas. El mantenimiento se ejecuta una sola vez por
proceso. Durante un intento de salir queda pausado; si se cancela la salida,
se reanuda, incluso si ya ha vencido el plazo. Si se cierra Fly, se cancela.
Las novedades y la oferta conservan sus condiciones y preferencias anteriores.

Las trazas `Fly.StartupMaintenance.Scheduled`, `PagePainted` y `Ready` permiten
comprobar el orden; `Ready` identifica `presented` o `deadline`. No contienen
URLs ni títulos. El límite es una garantía de programación en el bucle de UI,
no una garantía de tiempo real si ese hilo o el sistema están bloqueados.

## Perfiles y compilación reproducible

La receta de distribución prepara dos familias de perfiles oficiales:

- El perfil de Chromium que selecciona `chrome/build/linux.pgo.txt` del motor
  fijado, descargado con su actualizador oficial.
- El perfil de builtins de V8 seleccionado por su versión, descargado con la
  herramienta del mismo checkout y comprobado contra la revisión V8 de `DEPS`.

Se rechazan perfiles vacíos y enlaces simbólicos. Antes de compilar se genera
la configuración y se valida el grafo completo de Ninja en seco, incluidos los
inputs del snapshot V8. `--prepare-only` permite ejecutar esa preparación sin
empezar una compilación larga. Después de compilar se comprueba que las fuentes
y los perfiles siguen correspondiendo a los inputs preparados.

El preset completo activa PGO, ThinLTO optimizado y LLD. `--optimization none`
conserva la configuración anterior sin PGO y con mold; `pgo` permite aislar una
futura comparación sin ThinLTO. La comparación del preset completo incluye
también el cambio de linker y los builtins optimizados de V8: no separa sus
contribuciones ni entrena específicamente el código añadido por Fly.

`fly-build.json` registra las huellas de perfiles, argumentos, overlay y
ejecutable. El empaquetado verifica el registro contra la salida actual y lo
incluye en `provenance.json`. Cada ejecutable debe conservar sus snapshots V8
y recursos generados: las cachés de código de los `.pak` pueden cambiar al
cambiar el compilador y no deben intercambiarse entre variantes.

## Validación

La compilación nativa de desarrollo con DCHECKs supera 94 pruebas de servicios
y 149 etapas de navegador: 145 casos principales y cuatro etapas `PRE`.
Cinco casos nuevos comprueban contenido en streaming antes de `load`, el plazo
para una página vacía, seleccionar una pestaña ya pintada, cancelar la salida
y destruir el observador. La prueba de streaming exige terminar antes del
fallback y conservar la carga en curso; usa salida real de píxeles.

El harness `startup-maintenance.cjs` añade siete casos sobre el navegador real:
página rápida, streaming, plazo, actualización, primera oferta de integración,
varias ventanas y salida temprana. Las trazas deben mostrar una ejecución tras
presentación o plazo, y ninguna al salir pronto. Usa perfiles, HOME y directorios
XDG desechables en un escritorio Xvfb/Openbox propio. Comprueba que se conserva
la página original y que no se instala ninguna entrada de escritorio.

Ambas distribuciones completas superan los siete casos. En la optimizada,
el streaming libera el mantenimiento 195 ms después de programarlo, mientras
el documento sigue cargando; el caso sin contenido lo libera por plazo a los
5000 ms. Son tiempos desde programar el observador, distintos del FCP desde
crear el proceso. Las capturas de novedades y de la oferta se revisaron también
visualmente en ambos paquetes. El checkbox de navegador predeterminado permanece
sin seleccionar y el harness no acepta instalar la integración.

## Resultado del preset completo

Cinco muestras por variante y escenario, alternando el orden: 60 arranques en
el mismo escritorio propio, con perfiles nuevos y ejecutables independientes
en el SSD. Ambas variantes tienen el mismo overlay nativo, motor fijado,
extensiones y ausencia de API key. Las dos incluyen el nuevo calendario de
mantenimiento; esta comparación mide el preset de compilación, no el ahorro
causal de aplazar el mantenimiento. No se compila durante la serie.

| Escenario | FCP base → optimizado | PSS base → optimizado | CPU base → optimizado |
| --- | ---: | ---: | ---: |
| URL nueva | 425.6 → 375.9 ms | 476.5 → 503.8 MiB | 2.31 → 2.08 s |
| Nueva pestaña | 507.4 → 435.7 ms | 494.0 → 530.1 MiB | 2.38 → 2.12 s |
| Restaurar 12 pestañas | 483.9 → 406.6 ms | 430.9 → 456.0 MiB | 1.05 → 0.90 s |
| Restaurar 60 pestañas | 651.1 → 556.6 ms | 444.4 → 474.3 MiB | 1.20 → 1.02 s |
| Restaurar 3 espacios | 636.7 → 548.0 ms | 469.8 → 514.5 MiB | 1.23 → 1.13 s |
| Restaurar 4 ventanas | 614.8 → 526.5 ms | 433.3 → 459.9 MiB | 1.06 → 0.89 s |

Son medianas, sin tracing. El FCP mejora entre un 11,7 % y un 16,0 % y la CPU
enumerada baja entre un 8,1 % y un 16,0 %. **La PSS aumenta entre 25,1 y
44,6 MiB**, aproximadamente un 5,7–9,5 %. El ejecutable pasa de 337,3 a
345,8 MiB. El preset queda como predeterminado por su mejora de latencia y CPU
en los seis escenarios, con este coste de memoria explícito para la revisión.
No se presenta como un ahorro de RAM. La opción `none` conserva el control
anterior; la opción `pgo` existe, pero no tiene una serie A/B medida aquí.

Hay variación y valores extremos: URL nueva ocupa 413–2005 ms en la base y
357–405 ms en el preset; nueva pestaña, 462–630 ms y 394–892 ms respectivamente.
Los cinco valores de cada celda y sus rangos están en el
[registro compacto](./startup-maintenance-pgo-measurements.json). No se han
eliminado las muestras lentas. La estación comparte carga con otras aplicaciones;
el orden alternado reduce ese sesgo, pero cinco muestras no prueban una ganancia
fija en otro equipo. Las cifras absolutas tampoco son comparables con las de
la primera tanda, realizada en otro momento y con otra carga.

El número de páginas y ventanas se conserva en las 60 muestras; las páginas
sintéticas medidas aceptan entrada por ratón. PSS y CPU suman únicamente los
procesos vivos que enumera CDP aproximadamente dos segundos después de abrir
el puerto. No cubren procesos ya terminados ni todos los ayudantes. El registro
incluye cuántos procesos permitieron leer PSS, carga inicial del host, huellas
binarias, perfiles, procedencia y resúmenes de aceptación. En una muestra de
URL nueva optimizada no se pudo leer PSS de uno de los nueve procesos; se
conserva esa lectura parcial y no se sustituye por una estimación.

La auditoría compara 1445 archivos de runtime no ELF por variante: 1442 son
idénticos. Difieren los dos snapshots V8 y `resources.pak`. De los 5991 recursos
del `.pak`, solo cambian 276 entradas identificadas como cachés de código;
los otros 5715 recursos son idénticos. Se conserva cada snapshot y caché con
su ejecutable correspondiente. La serie incluye esos productos de optimización
y no intercambia recursos generados entre paquetes. Las huellas de manifiestos
y de la auditoría completa quedan en el registro compacto; los logs y trazas
completos permanecen fuera del repositorio.

Estos paquetes locales carecen de la API key de Google. La validación del
paquete con la configuración de producción se realiza mediante el dry run
existente de Release en la rama; su resultado y los checks del último commit
se enlazan en la [PR de implementación](https://github.com/Arzuparreta/fly-browser/pull/185).

## Reproducción

Preparar dos distribuciones completas desde el mismo overlay, con la misma
configuración de API y el mismo motor:

```sh
python3 scripts/chromium/release.py --root "$FLY_ENGINE_ROOT" \
  --optimization pgo-thinlto --prepare-only
python3 scripts/chromium/release.py --root "$FLY_ENGINE_ROOT" \
  --optimization none --dist /ruta/paquete-base
python3 scripts/chromium/release.py --root "$FLY_ENGINE_ROOT" \
  --optimization pgo-thinlto --dist /ruta/paquete-optimizado
```

Extraer cada paquete en su propio directorio del mismo volumen. Cuando no haya
compilaciones en marcha, ejecutar la aceptación y la comparación en escritorios
propios con `startup-desktop.py`:

```sh
python3 tests/chromium/startup-desktop.py --work-dir /tmp/fly-acceptance -- \
  node tests/chromium/startup-maintenance.cjs \
  --binary /ruta/paquete-optimizado/fly-browser/app/fly-browser \
  --output /tmp/fly-maintenance-results
python3 tests/chromium/startup-desktop.py --work-dir /tmp/fly-comparison -- \
  node tests/chromium/startup-study.cjs \
  --binary /ruta/paquete-optimizado/fly-browser/app/fly-browser \
  --compare-binary /ruta/paquete-base/fly-browser/app/fly-browser \
  --profile-root /var/tmp --output /tmp/fly-comparison-results --repeat 5 \
  --scenarios fresh-url,fresh-ntp,restore-12,restore-60,restore-3-workspaces,restore-4-windows
```

Los escritorios virtuales y documentos sintéticos no certifican GPU física,
Wayland, sitios autenticados, arranque tras reiniciar ni otras plataformas.
El dry run de Release en una rama comprueba el paquete de producción y conserva
artefactos; no publica una release ni instala el navegador.
