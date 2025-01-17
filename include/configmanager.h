#ifndef _CONFIGMANAGER_H
#define _CONFIGMANAGER_H

#include "globals.h"
#include <SPIFFS.h>

// Function declarations
void saveConfiguration(void);
void loadConfiguration(void);
void eraseConfiguration(void);
void initialize_config(void);

#endif // _CONFIGMANAGER_H