#include "util/geo.h"

#include <math.h>

float haversine_m(float lat1, float lon1, float lat2, float lon2) {
  const float r = 6371000.0f;
  const float to_rad = 0.01745329252f;
  const float dlat = (lat2 - lat1) * to_rad;
  const float dlon = (lon2 - lon1) * to_rad;
  const float a = sinf(dlat * 0.5f) * sinf(dlat * 0.5f) +
                  cosf(lat1 * to_rad) * cosf(lat2 * to_rad) *
                  sinf(dlon * 0.5f) * sinf(dlon * 0.5f);
  const float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
  return r * c;
}
