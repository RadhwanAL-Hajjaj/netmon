#pragma once
// Saved reports in flash: one file per slot, as src/core/report.h lays them
// out. LittleFS is mounted by settings_load(), which runs first at boot.
#include <Arduino.h>
#include <FS.h>
#include <cstddef>

#include "../core/report.h"

// Reads every slot's first line: which network, when, how many devices.
void reports_load(ReportSlot (&slots)[kReportSlots]);

// The rows of the report in slot `i`, into rows[0..cap). How many were read;
// 0 when there is no report there or it cannot be read.
size_t report_read_rows(size_t i, ReportRow* rows, size_t cap);

// Writes the report into slot `i`, beside the old one and then renamed over
// it, so a power cut leaves one or the other whole. `vendor_of` names each
// row's maker. On success `slot` describes what was written.
bool report_write(size_t i, const ReportMeta& m, const ReportRow* rows, size_t n,
                  const char* (*vendor_of)(const Mac&), ReportSlot& slot);

bool report_remove(size_t i);
String report_path(size_t i);

// What is left of the file system for reports and everything else.
size_t reports_free_bytes();
