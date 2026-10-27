# Mejoras tras el estudio de arranque

Primera tanda de implementación del [estudio de arranque](./startup-study.md).
El seguimiento de los otros dos puntos está en
[Mantenimiento inicial y optimización de la distribución](./startup-maintenance-pgo.md).
Se elimina trabajo repetido conservando el modelo de sesión, la prioridad de
pestañas, los perfiles y las reglas de seguridad de Chromium.

## Cambios

- Las ventanas decoradas por Fly conservan el popup nativo de respaldo de
  Chromium, pero no construyen los dos omnibox web ni solicitan el precalentado
  de WebUI superior. La condición es por ventana: popups, aplicaciones,
  DevTools y Chromium sin `--fly-native` mantienen su ruta anterior. La barra
  de direcciones de Fly sigue usando su interfaz nativa.
- Los cambios de título, favicon y audio actualizan su fila. Un cambio de
  selección actualiza la antigua y la nueva. Las operaciones estructurales,
  fijadas, split, tema y cambios de modelo conservan el refresco completo,
  agrupado por tarea y con identidades débiles. Se mantienen foco, accesibilidad,
  cierre cancelado y estados de audio.
- El catálogo de espacios guarda una validación y una proyección tipada por
  revisión de la preferencia. Los cambios externos y de políticas invalidan la
  caché; las escrituras siguen comprobando permisos y preservando campos
  desconocidos. Un valor dañado sigue requiriendo recuperación.
- La generación de contenido de usuario compara los cinco archivos completos
  antes de escribir. Los intactos se reutilizan; los ausentes, truncados,
  dañados o enlazados se reemplazan atómicamente. Se conserva el registro de
  scripts y estilos, sus versiones, la recarga y su momento de ejecución.
- Las ventanas comparten una lectura y decodificación del arte lateral por
  proceso. El worker entrega bitmaps inmutables; las imágenes de interfaz se
  construyen y utilizan en el hilo UI. Los callbacks de ventanas cerradas son
  débiles.
- Registrar la nueva pestaña ya no lee sus recursos si se abre una web. Su
  primera petición inicia el lote y las peticiones concurrentes comparten esa
  lectura. Se conservan el HTML/CSS iniciales, tema, geometría, fuentes y datos
  privados separados.
- Un cambio de tema aplica la paleta una vez.

Se añaden eventos de traza `Fly.*` en la categoría `browser` para refrescos
completos/parciales, validación de espacios, generación y número de escrituras, recursos de nueva pestaña,
lectura/decodificación de arte, preparación de perfiles y mantenimiento inicial.
No incluyen URLs ni títulos.

## Decisiones conservadas

No se cambia la restauración progresiva ni se aplazan conexiones de fondo,
ventanas, espacios o scripts. No se introduce un proceso residente ni capturas
persistidas. El historial de nueva pestaña conserva los espacios cerrados;
limitarlo a perfiles cargados cambiaría su contenido. En esta primera tanda,
el mantenimiento inicial mantiene su calendario: aplazar una oferta que cambia
el primer plano requiere validar esa experiencia por separado.

PGO quedó pendiente en esta primera tanda de una compilación con perfiles
compatibles y una comparación controlada. Activarlo en la configuración sin
producir y verificar esa entrega no demostraría una mejora. El estudio tampoco
justifica sustituir fuentes ya cacheadas o rediseñar el almacenamiento de sesiones.

## Resultado medido

Cinco muestras por variante y escenario, alternando el orden. Ambas son
compilaciones estáticas optimizadas del mismo motor, con iguales argumentos,
recursos, sandbox y extensiones; se copia cada ejecutable a un inode independiente
en el mismo SSD. Así la PSS de la base no se reduce por compartir su código con
el Fly cotidiano que sigue abierto. Cada muestra usa un perfil nuevo en
`/var/tmp`, también en SSD. No se compila mientras se mide.

| Escenario | FCP base → cambio | PSS base → cambio | CPU base → cambio |
| --- | ---: | ---: | ---: |
| URL nueva | 401 → 284 ms | 473 → 425 MiB | 0,97 → 0,75 s |
| Nueva pestaña | 327 → 297 ms | 486 → 444 MiB | 1,03 → 0,80 s |
| Restaurar 12 pestañas | 308 → 294 ms | 437 → 403 MiB | 0,68 → 0,49 s |
| Restaurar 60 pestañas | 381 → 375 ms | 460 → 418 MiB | 0,79 → 0,58 s |
| Restaurar 3 espacios | 437 → 371 ms | 559 → 428 MiB | 1,38 → 0,69 s |
| Restaurar 4 ventanas | 445 → 355 ms | 545 → 433 MiB | 1,44 → 0,63 s |

Son medianas del conjunto de cambios, sin tracing. Las 60 muestras conservan
el número exacto de pestañas y ventanas, la página activa visible y la entrada
por ratón de la página sintética. Los targets de omnibox pasan de dos por
ventana a cero. La configuración de restauración de fondo no se modifica;
en algunas muestras termina más trabajo de fondo dentro del mismo plazo.

La URL nueva tiene variación considerable: base 315–483 ms y cambio 262–403 ms.
Una primera serie de 60 muestras, usando directamente el ejecutable instalado,
dio medianas 303 → 273 ms para esa URL; no se toma su PSS como evidencia por
la compartición con el proceso cotidiano. La reducción de 6 ms al restaurar
60 pestañas es pequeña. Estas cifras no prometen una ganancia fija en otra
máquina ni separan causalmente el ahorro de cada mejora individual.

PSS y CPU se suman solo para los procesos que enumera CDP y siguen vivos al
muestrear, unos dos segundos después de abrir su puerto. No son la memoria
total de todos los ayudantes ni la CPU de los procesos ya terminados. La
medición sigue limitada a Xvfb, caché del sistema caliente, documentos locales
pequeños del mismo sitio y perfiles sintéticos. No demuestra GPU física,
Wayland, arranque tras reiniciar, webs autenticadas ni otras plataformas.

Cuatro trazas separadas confirman una sola lectura/decodificación de arte al
restaurar tres espacios o cuatro ventanas, ninguna escritura de contenido de
usuario al reutilizar la sesión y ninguna lectura de recursos de nueva pestaña
cuando se arranca una web. En perfil nuevo se escriben los cinco archivos una
vez; una regeneración sin cambios escribe cero.

El [registro de muestras y trazas](./startup-improvements-measurements.json)
conserva las dos series completas, huellas SHA-256, agregados, condiciones y
contadores de trazas. Los logs completos quedan fuera del repositorio.

## Reproducción

El harness acepta `--compare-binary /ruta/al/binario/base` junto a `--binary`
para alternar dos compilaciones, invertir el orden por repetición y guardar
ambas huellas SHA-256. Cada compilación prepara su propia sesión sintética.
Los resultados de una compilación por componentes no son comparables con los
de la distribución estática optimizada. `--trace` incluye la categoría `browser`;
las trazas se capturan separadas de los tiempos A/B.
