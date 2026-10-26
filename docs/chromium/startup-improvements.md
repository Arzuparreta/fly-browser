# Mejoras tras el estudio de arranque

Seguimiento del [estudio de arranque](./startup-study.md). Se elimina trabajo
repetido conservando el modelo de sesión, la prioridad de pestañas, los perfiles
y las reglas de seguridad de Chromium.

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

Se añaden eventos de traza `Fly.*` en la categoría `browser` para refrescos completos/parciales, validación
de espacios, generación y número de escrituras, recursos de nueva pestaña,
lectura/decodificación de arte, preparación de perfiles y mantenimiento inicial.
No incluyen URLs ni títulos.

## Decisiones conservadas

No se cambia la restauración progresiva ni se aplazan conexiones de fondo,
ventanas, espacios o scripts. No se introduce un proceso residente ni capturas
persistidas. El historial de nueva pestaña conserva los espacios cerrados;
limitarlo a perfiles cargados cambiaría su contenido. El mantenimiento inicial
mantiene su calendario: aplazar una oferta que cambia el primer plano requiere
validar esa experiencia por separado.

PGO queda pendiente de una compilación con perfiles compatibles y una comparación
controlada. Activarlo en la configuración sin producir y verificar esa entrega
no demostraría una mejora. El estudio tampoco justifica sustituir fuentes ya
cacheadas o rediseñar el almacenamiento de sesiones.

## Reproducción

El harness acepta `--compare-binary /ruta/al/binario/base` junto a `--binary`
para alternar dos compilaciones, invertir el orden por repetición y guardar
ambas huellas SHA-256. Cada compilación prepara su propia sesión sintética.
Los resultados de una compilación por componentes no son comparables con los
de la distribución estática optimizada. `--trace` incluye la categoría `browser`;
las trazas se capturan separadas de los tiempos A/B.
