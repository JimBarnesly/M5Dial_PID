#pragma once
#include <Preferences.h>
#include <WString.h>
#include "AppState.h"

class StorageManager {
public:
  void begin();
  bool load(PersistentConfig& cfg);
  void save(const PersistentConfig& cfg);

private:
  Preferences _prefs;
  String _lastSavedJson;
  void loadDefaults(PersistentConfig& cfg);
  bool encryptedStorageAvailable() const;
};
