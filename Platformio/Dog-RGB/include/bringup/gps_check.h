#pragma once

class Print;
namespace bringup::gps_check {
// Writes to the existing bounded log queue, not directly to USB.
void report(Print &sink);
} // namespace bringup::gps_check
