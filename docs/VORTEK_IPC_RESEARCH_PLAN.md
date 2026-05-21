# Plan de Investigación Futura: Arquitectura IPC/Servidor y Serialización Vulkan

Fecha: 2026-04-20  
Objetivo: preparar una línea futura posterior a ExynosTools 3.1, idealmente 3.2+ o una rama separada de investigación

## Por qué existe este documento

Vortek muestra una arquitectura radicalmente distinta a la de ExynosTools:

- ExynosTools hoy:
  - es una Vulkan Layer
  - trabaja en el mismo proceso
  - tiene un overhead bajo
  - está limitada por las restricciones normales de una layer

- Vortek:
  - usa un wrapper Vulkan cliente-servidor
  - emplea ring buffers en memoria compartida
  - usa un canal de control por socket Unix
  - serializa una parte muy amplia de Vulkan
  - gana libertad del lado servidor para JNI, inspección y transformaciones pesadas

La idea es potente, pero también es demasiado grande y arriesgada para meterla directamente en ExynosTools 3.1.  
Antes de construir algo así, necesitamos pruebas claras de que esa arquitectura compensa su coste.

Este documento define ese camino de investigación.

## Principio base

No hay que empezar serializando toda Vulkan.

Primero debemos demostrar tres cosas:

1. que el overhead del IPC es aceptable para el caso de uso objetivo
2. que un subconjunto pequeño serializado basta para validar la idea
3. que el beneficio es lo bastante fuerte como para justificar salir del diseño actual en proceso

Si alguna de esas tres falla, conviene parar pronto.

## Cómo sería un resultado exitoso

Una arquitectura IPC/servidor solo merece la pena si nos da al menos una de estas ventajas:

- inspección de shaders o SPIR-V que una layer normal no pueda hacer de forma suficientemente limpia
- análisis pesado fuera del proceso sin perjudicar el hilo principal de la app
- futuras transformaciones del lado host demasiado invasivas para una layer en proceso
- mejor compatibilidad en un escenario concreto tipo Winlator o contenedor

Si lo único que aporta es curiosidad arquitectónica, no es suficiente.

## Qué no hacer al principio

No debemos empezar con:

- serialización completa de toda la API Vulkan
- un wrapper cliente-servidor de todos los comandos
- mover el decode BCn a otro proceso
- reemplazar el camino actual de ExynosTools
- importar el serializer de Vortek entero

Eso metería una complejidad enorme antes de tener pruebas reales.

## Fases de investigación

### Fase 0: definir el límite del problema

Antes de escribir código IPC, hay que definir el problema más pequeño que merezca la pena.

Preguntas que debemos responder:

- ¿Qué tarea exacta queremos sacar fuera del proceso?
- ¿Esa tarea necesita handles Vulkan, estructuras completas o solo metadata?
- ¿Esa tarea es síncrona o puede diferirse?
- ¿Esa tarea necesita JNI, acceso a archivos o aislamiento real en otro proceso?

Candidatos recomendados para empezar:

1. inspección de metadata de shaders o SPIR-V
2. agregación offline de profiling o telemetría
3. espejado estrecho de algunos comandos solo para investigación

Candidatos no recomendados para empezar:

1. decode BCn
2. ejecución de comandos de copia
3. creación de imágenes o image views

Esos caminos son demasiado sensibles a la latencia.

### Fase 1: viabilidad de serialización sin IPC

Objetivo:

- demostrar que podemos serializar y deserializar un subconjunto pequeño de Vulkan de forma limpia

Construir un serializer mínimo para solo unas pocas estructuras:

1. `VkImageCopy`
2. `VkBufferImageCopy`
3. `VkImageSubresourceLayers`
4. un subconjunto mínimo de `VkDeviceCreateInfo` con algunas estructuras de `pNext`

Qué medir:

- corrección del round-trip
- tamaño de implementación
- mantenibilidad del código
- coste de CPU por serialización y deserialización

Criterio para seguir:

- el serializer mínimo es fácil de mantener y lo bastante rápido

Criterio para parar:

- aparece demasiado boilerplate incluso para el subconjunto pequeño
- el manejo de `pNext` es frágil o demasiado difícil de extender

### Fase 2: prototipo de transporte sin ejecución Vulkan

Objetivo:

- medir el coste real del modelo IPC por sí mismo

Construir un prototipo cliente-servidor mínimo con:

- ring buffer en memoria compartida
- canal de control
- mensajes de tamaño fijo y variable

Tipos de mensaje a probar:

1. mensajes pequeños de metadata
2. lotes medianos de estructuras
3. payloads más grandes representativos de resultados de inspección

Qué medir:

- latencia de ida
- latencia de ida y vuelta
- throughput
- overhead de CPU en cliente y servidor
- contención bajo bursts repetidos

Criterio para seguir:

- la latencia es lo bastante baja para el caso de uso objetivo

Criterio para parar:

- el coste del IPC ya es demasiado alto incluso antes de meter lógica Vulkan real

### Fase 3: prueba de integración estrecha con ExynosTools

Objetivo:

- demostrar que ExynosTools puede hablar con un helper process sin cambiar todavía la arquitectura principal

No hay que proxyear Vulkan todavía.

En vez de eso:

- mantener la Vulkan Layer tal como está
- enviar solo un payload pequeño de análisis a un helper process
- recibir una respuesta pequeña

La mejor primera prueba sería:

1. una petición de inspección de metadata de shaders
2. un lote pequeño de eventos de telemetría o profiling

Esta fase nos dirá si ExynosTools puede alojar de forma segura un camino externo auxiliar.

### Fase 4: puerta de decisión

En este punto habrá que decidir entre tres futuros:

1. mantener ExynosTools completamente en proceso y cerrar la línea IPC
2. añadir un sidecar o helper process solo para análisis o profiling
3. empezar una rama de investigación real de wrapper Vulkan cliente-servidor

Esta decisión debe salir de métricas, no del entusiasmo.

## Backlog mínimo de experimentos

Estas son las primeras pruebas que deberíamos hacer.

### Experimento A: microtest de round-trip de estructuras

Implementar:

- `VkImageCopy` -> serialize -> deserialize -> compare
- `VkBufferImageCopy` -> serialize -> deserialize -> compare

Salida esperada:

- número exacto de bytes
- éxito o fallo del round-trip
- tiempo de CPU por iteración

### Experimento B: serializer pequeño de `pNext`

Implementar soporte para:

- `VkPhysicalDeviceFeatures2`
- `VkPhysicalDeviceDescriptorBufferFeaturesEXT`
- `VkPhysicalDeviceBufferDeviceAddressFeatures`

Objetivo:

- demostrar que podemos serializar cadenas de features con `pNext` sin que la complejidad explote

### Experimento C: benchmark local de transporte por memoria compartida

Implementar:

- el cliente escribe lotes en memoria compartida
- el servidor lee y confirma

Medir:

- paquetes pequeños
- paquetes medianos
- comportamiento con bursts

### Experimento D: sidecar mínimo para ExynosTools

Implementar:

- un helper process opcional solo para debug o investigación
- ExynosTools envía un payload sintético pequeño
- el helper responde con un resultado pequeño

Objetivo:

- validar lifecycle, arranque, timeout y fallback

## Riesgos

### Riesgos técnicos

- la latencia del IPC puede matar cualquier beneficio
- la complejidad de sincronización puede crecer demasiado rápido
- los handles Vulkan entre procesos pueden volverse difíciles de razonar
- el soporte de `pNext` y de estructuras de extensiones puede escalar mal
- el packaging y lifecycle de Android puede volverse mucho más doloroso

### Riesgos de producto

- el proyecto puede dejar de ser una Vulkan Layer limpia y volverse mucho más difícil de distribuir
- depurar puede volverse más lento y más costoso
- el riesgo de release puede subir mucho para una ganancia poco visible

## Recomendación actual

Camino recomendado:

1. mantener ExynosTools 3.1 y 3.x en proceso
2. construir la investigación IPC en una rama o carpeta aparte
3. serializar primero solo un subconjunto mínimo de Vulkan
4. no intentar una serialización completa estilo Vortek hasta que las pruebas pequeñas funcionen

## Siguientes pasos inmediatos

1. crear una rama de investigación para IPC/servidor
2. añadir una prueba mínima de serializer para `VkImageCopy` y `VkBufferImageCopy`
3. añadir un benchmark mínimo de transporte local
4. registrar resultados antes de ampliar el alcance
