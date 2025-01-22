#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include "libpax_helpers.h"
#include <WiFiManager.h>

extern bool portalActive;
extern WiFiManager wifiManager;

void startConfigPortal();

#endif // CONFIG_PORTAL_H 