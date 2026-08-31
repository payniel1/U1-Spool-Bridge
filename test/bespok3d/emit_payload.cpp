// Emit the exact JSON u1BuildPayload() produces, for both backends, so it can
// be run through Bespok3d's own validator logic.
#include <cstdio>
#include <WiFi.h>
#include "settings.h"
#include "spool_data.h"
#include "u1_client.h"

WiFiClass WiFi;
EspClass  ESP;

int main() {
  g_settings.loadDefaults();
  g_settings.sendCardUid = true;

  SpoolData d{};
  snprintf(d.vendor,   sizeof d.vendor,   "%s", "SUNLU");
  snprintf(d.mainType, sizeof d.mainType, "%s", "PLA");
  snprintf(d.subType,  sizeof d.subType,  "%s", "Basic");
  d.rgb = 0x1A2B3C; d.rgb2 = 0x445566; d.alpha = 255;
  d.hotendMin = 200; d.hotendMax = 230; d.bedTemp = 60;
  d.uid[0]=0x04; d.uid[1]=0x23; d.uid[2]=0x12; d.uid[3]=0xFD;
  d.uid[4]=0x40; d.uid[5]=0x02; d.uid[6]=0x89; d.uidLen = 7;
  snprintf(d.cardType, sizeof d.cardType, "%s", "NTAG215");
  d.sku = 12345;

  String ext, stock;
  u1BuildPayload(d, 0, ext,   U1_BACKEND_EXTENDED);
  u1BuildPayload(d, 0, stock, U1_BACKEND_STOCK);
  printf("{\"extended\": %s, \"stock\": %s}\n", ext.c_str(), stock.c_str());
  return 0;
}
