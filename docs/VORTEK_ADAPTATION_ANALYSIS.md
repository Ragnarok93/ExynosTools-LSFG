# Análisis de Adaptación de Vortek para ExynosTools

Fecha: 2026-04-20  
Objetivo: identificar qué partes de Vortek merece la pena adaptar a ExynosTools y cuáles no

## Resumen ejecutivo

Después de revisar Vortek a fondo, la conclusión principal es esta:

- no merece la pena adaptar su arquitectura cliente-servidor a ExynosTools
- no merece la pena importar su serializer completo tal cual
- sí merece mucho la pena aprovechar su conocimiento estructural de Vulkan

La pieza más valiosa de Vortek no es el IPC.  
La pieza más valiosa es `vortek_serializer.h`, porque codifica cómo recorrer, medir, clonar y reconstruir estructuras Vulkan complejas, incluyendo `pNext`, arrays anidados, subestructuras y variantes modernas tipo `*2`.

## Qué contiene Vortek realmente

### Cobertura estructural

En `include/vortek_serializer.h` hay:

- **376** funciones `vt_sizeof_Vk*` para estructuras Vulkan
- **254** funciones `vt_sizeof_vk*` para comandos Vulkan completos

Eso confirma que el serializer es, en la práctica, un mapa enorme de la API Vulkan.

### Piezas importantes del repo

- `include/vortek_serializer.h`
  - núcleo del conocimiento estructural
  - funciones `vt_sizeof_*`, `vt_serialize_*`, `vt_unserialize_*`
  - resolución de `pNext` mediante `switch (sType)`

- `include/vortek.h`
  - helpers reutilizables de `pNext`
  - `MemoryPool`
  - macros base y utilidades comunes

- `src/descriptor_update_template.c`
  - construcción compacta de `VkWriteDescriptorSet`
  - lógica reutilizable para descriptor templates

- `src/vk_object.c` y `src/vk_object_pool.c`
  - wrappers de handles y pools simples de objetos

- `src/ring_buffer.c`
  - transporte IPC rápido
  - útil como referencia arquitectónica, pero no prioritario para ExynosTools

- `src/vulkan_calls.c`
  - wrapper enorme de llamadas Vulkan sobre el serializer
  - útil como referencia, pero no para adaptarlo directamente

## Qué sí nos sirve para ExynosTools

## 1. Conocimiento estructural de Vulkan

Este es el bloque más importante.

Vortek ya resolvió cómo trabajar con structs como:

- `VkDeviceCreateInfo`
- `VkImageCreateInfo`
- `VkImageViewCreateInfo`
- `VkBufferImageCopy2`
- `VkImageCopy2`
- `VkCopyImageInfo2`
- `VkBlitImageInfo2`
- `VkCopyBufferToImageInfo2`
- `VkComputePipelineCreateInfo`
- `VkGraphicsPipelineCreateInfo`

Eso encaja muy bien con ExynosTools porque ya interceptamos o tocamos partes relacionadas con:

- `vkCreateDevice`
- `vkCreateImage`
- `vkCreateImageView`
- `vkCmdCopyImage2`
- `vkCmdCopyBufferToImage2`
- futuros workarounds en `vkCreateComputePipelines`
- futuros workarounds en `vkCreateGraphicsPipelines`

### Lo que hay que extraer de esa idea

No necesitamos un serializer de red.
Necesitamos una librería interna para:

- calcular tamaño estructural útil
- hacer deep-copy de structs Vulkan
- clonar cadenas `pNext`
- clonar arrays anidados
- mantener ownership y lifetime correctos durante la llamada al driver

## 2. Helpers de `pNext`

Esto ya empezó a aprovecharse en ExynosTools, y fue una buena dirección.

La parte valiosa de Vortek aquí es:

- `findNextVkStructure`
- `invertVkStructuresChain`
- `removeNextVkStructure`

Esto encaja directamente con:

- `src/layer/layer_vk_struct_utils.h`
- `src/layer/layer_entry.cpp`
- `src/layer/layer_format_virtualization.cpp`

Conclusión:

- esta línea sí merece seguir ampliándose
- es una adaptación correcta y útil

## 3. Descriptor update template

`src/descriptor_update_template.c` es una pieza mucho más útil para ExynosTools que el IPC.

Su valor está en:

- precalcular cuántos `VkDescriptorImageInfo` hacen falta
- precalcular cuántos `VkDescriptorBufferInfo` hacen falta
- rellenar `VkWriteDescriptorSet` de forma ordenada
- evitar reconstrucciones manuales repetidas

Esto encaja con lo que ya venimos haciendo en:

- `src/layer/layer_descriptor_write_builder.h`
- `src/layer/layer_descriptor_write_builder.cpp`
- `src/layer/layer_compute_runtime.cpp`

Conclusión:

- esta adaptación sí merece seguir profundizándose
- es una inspiración correcta y de bajo riesgo

## 4. Pools y arenas ligeras

Vortek usa ideas sencillas y prácticas de pooling y memoria temporal.

Lo útil para ExynosTools no es copiar su sistema completo, sino su filosofía:

- reciclar objetos simples
- usar arenas temporales pequeñas
- minimizar churn de heap en caminos repetidos

Esto ya encaja con lo que tenemos o acabamos de meter en:

- `src/layer/layer_temp_arena.h`
- `src/layer/layer_command_buffer_resources.cpp`
- `src/layer/layer_telemetry.cpp`

Conclusión:

- también merece seguirse
- pero como infraestructura ligera, no como framework completo

## Qué NO merece la pena adaptar

## 1. Arquitectura cliente-servidor

Para ExynosTools, esta arquitectura probablemente sería una degradación y no una mejora.

Motivos:

- añade latencia
- añade complejidad de lifecycle
- añade más puntos de fallo
- complica la depuración
- complica Android y packaging
- no encaja con rutas calientes muy sensibles como decode/copy

Conclusión:

- descartar como dirección principal

## 2. Serialización completa de toda Vulkan

No necesitamos las 46.000 líneas enteras.

El serializer de Vortek existe porque Vortek necesita:

- IPC
- transporte entre procesos
- empaquetado de comandos completos
- reconstrucción de handles
- cobertura muy amplia de la API

ExynosTools no necesita eso.

Conclusión:

- no copiar el serializer completo
- no intentar una adaptación masiva

## 3. `vulkan_calls.c`

Ese archivo es esencial para Vortek, pero no para ExynosTools.

Su propósito es:

- envolver llamadas Vulkan
- convertir argumentos a payloads serializados
- enviarlos al servidor

Conclusión:

- útil como referencia histórica
- no útil como base directa de implementación

## 4. Ring buffer e IPC

`src/ring_buffer.c` está bien hecho, pero no es prioritario para ExynosTools.

Conclusión:

- mantenerlo solo como referencia arquitectónica futura
- no meterlo en el roadmap cercano

## Qué deberíamos adaptar primero

## Fase A: módulo de deep-copy Vulkan limitado

Crear:

- `src/layer/layer_vk_struct_clone.h`
- `src/layer/layer_vk_struct_clone.cpp`

Objetivo:

- clonado profundo de un subconjunto pequeño y útil de structs Vulkan

Primer subconjunto recomendado:

1. `VkBufferImageCopy2`
2. `VkImageCopy2`
3. `VkCopyImageInfo2`
4. `VkImageBlit2`
5. `VkBlitImageInfo2`
6. `VkCopyBufferToImageInfo2`

Este subconjunto es el mejor punto de entrada porque:

- encaja con los hooks actuales de ExynosTools
- es manejable
- ya usamos muchos de esos caminos
- permite probar la idea sin meternos aún en pipelines gráficos

## Fase B: structs de creación que ExynosTools ya toca

Después del bloque anterior, ampliar soporte a:

1. `VkDeviceCreateInfo`
2. `VkImageCreateInfo`
3. `VkImageViewCreateInfo`

Esto permitiría:

- parches más seguros
- clonados limpios
- menos código artesanal al tocar `pNext`

## Fase C: pipelines

Solo cuando lo anterior esté estable, añadir:

1. `VkPipelineShaderStageCreateInfo`
2. `VkSpecializationInfo`
3. `VkComputePipelineCreateInfo`
4. `VkGraphicsPipelineCreateInfo`

Orden recomendado:

- primero `VkComputePipelineCreateInfo`
- después `VkGraphicsPipelineCreateInfo`

Motivo:

- compute es más pequeño y fácil de validar
- graphics es mucho más profundo y peligroso

## Qué porcentaje de Vortek usaríamos de verdad

La estimación razonable no es 70%.

Si lo hacemos bien, lo más probable es:

- usar **5% a 15% del conocimiento útil**
- quizá **20%** si ampliamos bastante la cobertura

Lo importante no es copiar líneas.
Lo importante es copiar el patrón de resolución:

- cómo medir
- cómo recorrer
- cómo clonar
- cómo reconstruir `pNext`
- cómo gestionar arrays y subestructuras

## Propuesta realista para ExynosTools

## Lo que sí conviene hacer

1. seguir mejorando `layer_vk_struct_utils.h`
2. crear `layer_vk_struct_clone.*`
3. empezar por structs `*2` y por create infos ya usados
4. reutilizar `layer_temp_arena.h` para clones temporales
5. dejar graphics pipelines para una fase posterior

## Lo que no conviene hacer

1. importar `vortek_serializer.h` entero
2. portar `vulkan_calls.c`
3. meter IPC
4. copiar el sistema completo de handles de Vortek

## Conclusión final

Vortek sí aporta muchísimo valor a ExynosTools, pero no por su parte cliente-servidor.

Su valor real para nosotros está en esto:

- conocimiento estructural de Vulkan
- manejo sistemático de `pNext`
- deep-copy de structs complejos
- tratamiento consistente de arrays, subestructuras y variantes modernas

La adaptación correcta no es “usar Vortek entero”.
La adaptación correcta es construir en ExynosTools una librería propia, pequeña y dirigida, inspirada en ese conocimiento.

Ese es el camino que más puede acercar ExynosTools a una capa de compatibilidad más seria, robusta y capaz, sin convertirla en otra cosa distinta.
