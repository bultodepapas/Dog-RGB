#include "display/connection.h"
#include <stdio.h>

namespace display {
namespace {
void network_name(char *out, size_t size, const char (&name)[33]) {
  // Built-in Montserrat covers printable ASCII, not arbitrary SSID bytes.
  // Do not silently display a different network name or cut a UTF-8 sequence.
  for (unsigned i = 0; i < 32 && name[i]; ++i) {
    const auto c = static_cast<unsigned char>(name[i]);
    if (c < 32 || c > 126) {
      snprintf(out, size, "Nombre no compatible");
      return;
    }
  }
  snprintf(out, size, "%.32s", name);
}
bool has_ip(const char (&ip)[16]) {
  return ip[0] && !(ip[0] == '0' && ip[1] == '.');
}
}
ConnectionText format_connection(const ConnectionSnapshot &s) {
  ConnectionText v;
  if (s.radio_off) {
    snprintf(v.ap_status, sizeof(v.ap_status), "Wi-Fi apagado");
    snprintf(v.ap_address, sizeof(v.ap_address), "Portal no disponible");
    snprintf(v.sta_status, sizeof(v.sta_status), "Sin conexion a red");
    snprintf(v.summary, sizeof(v.summary), "Wi-Fi apagado");
    return v;
  }
  if (s.ap_enabled) {
    snprintf(v.ap_status, sizeof(v.ap_status), s.clients ? "Portal: %u conectado%s" : "Portal disponible",
        s.clients, s.clients == 1 ? "" : "s");
    network_name(v.ap_name, sizeof(v.ap_name), s.ap_ssid);
    snprintf(v.ap_address, sizeof(v.ap_address), "%s", has_ip(s.ap_ip) ? s.ap_ip : "Direccion pendiente");
  } else {
    snprintf(v.ap_status, sizeof(v.ap_status), "Red del collar inactiva");
  }
  if (s.sta_connected) {
    snprintf(v.sta_status, sizeof(v.sta_status), "Red conectada");
    network_name(v.sta_name, sizeof(v.sta_name), s.sta_ssid);
    snprintf(v.sta_address, sizeof(v.sta_address), "%s", has_ip(s.sta_ip) ? s.sta_ip : "Direccion pendiente");
  } else if (s.sta_connecting) {
    snprintf(v.sta_status, sizeof(v.sta_status), "Conectando a red");
    network_name(v.sta_name, sizeof(v.sta_name), s.sta_ssid);
  } else {
    snprintf(v.sta_status, sizeof(v.sta_status), s.sta_ssid[0] ? "Red no conectada" : "Sin red configurada");
    network_name(v.sta_name, sizeof(v.sta_name), s.sta_ssid);
  }
  snprintf(v.summary, sizeof(v.summary), "%s", s.ap_enabled ? "Portal disponible" :
      s.sta_connected ? "Red conectada" : s.sta_connecting ? "Conectando a red" : "Sin conexion Wi-Fi");
  return v;
}
} // namespace display
