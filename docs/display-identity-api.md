# Identidad local — API v1

VIS-2. Solo perfiles `DOG_RGB_DISPLAY_LVGL`; Classic responde capability negativa
y mantiene excluido el código de `display/`. JSON local, sin servicio externo.

## GET /api/identity

HTTP 200. Classic: `{"schema_version":1,"supported":false}`.
Display:

```json
{
  "schema_version": 1,
  "supported": true,
  "configured": true,
  "name": "FREYA",
  "phone": "+100000000000",
  "qr_kind": "whatsapp",
  "qr_payload": "https://wa.me/100000000000",
  "generation": 1
}
```

Número sintético no marcable. `configured` requiere nombre y teléfono válidos.
`qr_kind`: `whatsapp`, `call`, `disabled`; payload derivado del mismo teléfono,
nunca URL editable. Identidad ausente: nombre/teléfono/payload vacíos, canal
`disabled`, generación 0. La generación también puede ser 0 tras rollover;
no usarla como indicador de configuración. La capability acredita el editor y
store; la integración de la página física corresponde a VIS-3.

## POST /api/identity

Guardas existentes `write_allowed()`/`X-Dog-Portal: 1`; PIN opcional sin cambios.
Cuerpo JSON de hasta 512 bytes, exactamente cuatro campos, sin campos parciales:

```json
{"name":"FREYA","phone":"+100000000000","qr_kind":"whatsapp","expected_generation":1}
```

Nombre: 48 bytes UTF-8/24 puntos de código, alfabeto de
`display/identity.h`. El editor convierte NFC; firmware rechaza nombres
descompuestos, UTF-8 inválido, caracteres no soportados y espacios iniciales,
finales o dobles. Teléfono: `+`, 7–15 dígitos, primer dígito no cero; se aceptan
espacios/paréntesis/guiones. No infiere indicativo ni valida asignación/cuenta.
Se rechazan NUL embebidos. `expected_generation` es entero uint32 de la última
lectura: conflicto no sobrescribe. Para borrar, enviar nombre/teléfono vacíos
y `qr_kind:"disabled"` con la generación actual.

| Respuesta | Contrato |
| --- | --- |
| 200 | Misma forma que GET; datos canónicos y generación actual. No-op no escribe NVS ni incrementa |
| 400 | `status:error`, reason `body`, `fields`, `name`, `phone` o `qr_kind` |
| 401/403 | Guardas existentes del portal |
| 404 | `unsupported` en Classic |
| 409 | `conflict`; recargar explícitamente, conservar edición local |
| 413 | `body_size`; cota de aplicación, no límite previo de recepción WebServer |
| 500 | `storage`; no se confirmó escritura/lectura posterior; conservar edición y recargar |

El editor tiene guardado/borrado propios. No los mezcla con «Guardar cambios»
de LED/GPS; cada borrador participa separadamente en el aviso de salida.
La vista previa es texto/teléfono/destino, no una reproducción del layout LVGL.

## Persistencia

Claves `id_a`/`id_b` dentro del namespace cfg existente; no modifica `ConfigRecord`
ni particiones. Registro little-endian packed de 84 bytes: magic `0x49475244`,
versión uint16=1, tamaño uint16=84, generación uint32, nombre[49], teléfono[17],
canal uint8, reservado=0 y CRC32 IEEE sobre los primeros 80 bytes. Enum serializado
0=disabled, 1=whatsapp, 2=call. Payload canónico, padding cero, NUL y CRC comprobados.

Carga el banco válido más nuevo con comparación modular; ambos inválidos dejan
identidad vacía sin escribir automáticamente. Guarda en el banco alterno y
publica RAM después de comparar lectura completa. Una escritura truncada/corrupta
conserva el banco previo. Si la escritura fue completa pero falla solo su lectura
de comprobación, el resultado es indeterminado: RAM conserva lo verificado,
pero un reinicio puede elegir el nuevo registro completo. Nunca se comunica
éxito en ese caso. No afirmar garantía de contacto anterior tras todo error de I/O.

`identity::load()` se ejecuta después de storage al iniciar el portal;
`identity::get()` permite copiar el snapshot para VIS-3 en el mismo task.
Restaurar la configuración LED/GPS no borra identidad; «Borrar placa» guarda
un registro vacío válido. Recuperación por corrupción puede volver al banco
anterior, incluido su contacto: no es un borrado seguro de datos.
