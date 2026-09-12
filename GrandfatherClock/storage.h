/* =====================================================================
   storage.h  —  Onboard flash filesystem (LittleFS) — no SD card
   ---------------------------------------------------------------------
   Rev012.1: the microSD reader was dropped from the build. Settings and
   the chime log now live in the ESP8266's own flash via LittleFS.

   IDE REQUIREMENT: select a flash layout that reserves filesystem space,
   e.g.  Tools -> Flash Size -> "4MB (FS:2MB)".  With "FS:none" the
   filesystem cannot mount and logging/settings are disabled.
   ===================================================================== */
#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>

void storageBegin();    // mounts LittleFS (auto-formats on first boot)
bool storageReady();    // true if the filesystem mounted

// Append one chime record, rotating when the file grows large.
// Stores epoch MILLISECONDS (not seconds) so downloads can render real
// clock time to ms. Rows written before v2.10.0 hold seconds; readers
// detect that by magnitude (values < 1e12 are seconds).
void logChime(uint64_t epochMs, float peak);

// Split epoch milliseconds into local date and time strings, kept SEPARATE
// so spreadsheets parse each column as a real date / time instead of text.
//   date -> "YYYY-MM-DD"      (>= 12 bytes)
//   time -> "HH:MM:SS.mmm"    (>= 14 bytes)
void formatLocalParts(uint64_t epochMs, char* date, size_t dn,
                      char* timeOut, size_t tn);

#endif // STORAGE_H
