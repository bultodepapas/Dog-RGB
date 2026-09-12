#include "display/status.h"
#include <stdio.h>

namespace display {
StatusText format_status(gps::ReceptionState gps, const LedStatusSnapshot &led) {
  StatusText text;
  const char *state = "Estado desconocido", *detail = "Sin observacion valida";
  switch (gps) {
    case gps::ReceptionState::NoData: state = "Sin datos"; detail = "Revisar receptor y cable"; break;
    case gps::ReceptionState::Receiving: state = "Recibiendo"; detail = "Esperando posicion valida"; break;
    case gps::ReceptionState::Searching: state = "Buscando posicion"; detail = "Probar a cielo abierto"; break;
    case gps::ReceptionState::Untrusted: state = "Calidad insuficiente"; detail = "Posicion aun no confiable"; break;
    case gps::ReceptionState::Fix: state = "Posicion confiable"; detail = "Datos GPS vigentes"; break;
    case gps::ReceptionState::Stale: state = "Datos vencidos"; detail = "Esperando datos recientes"; break;
  }
  snprintf(text.gps_status, sizeof(text.gps_status), "%s", state);
  snprintf(text.gps_detail, sizeof(text.gps_detail), "%s", detail);
  text.gps_valid = gps == gps::ReceptionState::Fix;
  state = "Estado desconocido";
  switch (led.intent) {
    case led::LedIntent::Welcome: state = "Bienvenida"; break;
    case led::LedIntent::DayStatus: state = "Modo dia"; break;
    case led::LedIntent::Idle: state = "Esperando posicion"; break;
    case led::LedIntent::HomeMissing: state = "Zona sin configurar"; break;
    case led::LedIntent::Range: state = "Efectos por rango"; break;
    case led::LedIntent::Show: state = "Show"; break;
    case led::LedIntent::Simple: state = "Simple"; break;
    case led::LedIntent::CriticalAlert: state = "Alerta de control"; break;
    case led::LedIntent::SceneManual: state = "Escena manual"; break;
  }
  const char *mode = "Desconocido";
  switch (led.mode) {
    case led::LedMode::Speed: mode = "Velocidad"; break;
    case led::LedMode::Geofence: mode = "Zona"; break;
    case led::LedMode::Show: mode = "Show"; break;
    case led::LedMode::Simple: mode = "Simple"; break;
  }
  snprintf(text.led_status, sizeof(text.led_status), "%s", led.transport_enabled ? state : "Salida pausada");
  if (!led.transport_enabled) snprintf(text.led_detail, sizeof(text.led_detail), "Envio a tiras detenido");
  else if (!led.body_enabled) snprintf(text.led_detail, sizeof(text.led_detail), "Efectos apagados");
  else snprintf(text.led_detail, sizeof(text.led_detail), "Modo: %s", mode);
  const char *notice = "Aviso no identificado";
  switch (led.alert) {
    case led::LedAlert::None: notice = "Avisos: ninguno"; break;
    case led::LedAlert::System: notice = "Aviso GPS / Wi-Fi"; break;
    case led::LedAlert::Geofence: notice = "Aviso de zona"; break;
  }
  text.led_alert = led.alert != led::LedAlert::None;
  snprintf(text.led_notice, sizeof(text.led_notice), "%s", notice);
  return text;
}
} // namespace display
