#pragma once
#include <Preferences.h>
#include "AppState.h"

class StorageManager {
public:
  void begin();
  bool load(PersistentConfig& cfg);
  void save(const PersistentConfig& cfg);

private:
  Preferences _prefs;
  void loadDefaults(PersistentConfig& cfg);
};
