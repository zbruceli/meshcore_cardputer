#pragma once
// Region presets from the official MeshCore app config (https://api.meshcore.nz/api/v1/config).
// Only entries inside the Cap LoRa-1262 band (868-923 MHz); deprecated and 433 MHz presets dropped.

struct RegionPreset {
  const char* title;
  float freq;
  float bw;
  uint8_t sf;
  uint8_t cr;
  uint8_t path_hash_size;   // bytes per path hop (1..3)
};

static const RegionPreset PRESETS[] = {
  { "Australia",               915.800, 250,  10, 5, 1 },
  { "Australia (Narrow)",      916.575, 62.5,  7, 7, 1 },
  { "Australia (Mid)",         915.075, 125,   9, 5, 1 },
  { "Australia: SA, WA",       923.125, 62.5,  8, 8, 1 },
  { "Australia: QLD",          923.125, 62.5,  8, 5, 1 },
  { "Brazil",                  923.125, 62.5,  8, 8, 1 },
  { "Canada",                  910.525, 62.5,  7, 5, 3 },
  { "Costa Rica",              910.525, 125,  11, 5, 1 },
  { "EU/UK (Narrow)",          869.618, 62.5,  8, 8, 1 },
  { "Czech Republic (Narrow)", 869.432, 62.5,  7, 5, 2 },
  { "Hungary",                 869.618, 62.5,  7, 5, 2 },
  { "Netherlands",             869.618, 62.5,  7, 5, 1 },
  { "Netherlands (Limburg)",   869.618, 62.5,  8, 8, 2 },
  { "New Zealand (Narrow)",    917.375, 62.5,  7, 5, 2 },
  { "New Zealand (Gisborne)",  917.375, 250,  11, 5, 1 },
  { "Portugal 868",            869.618, 62.5,  7, 6, 1 },
  { "Slovakia",                869.618, 62.5,  7, 5, 2 },
  { "Switzerland",             869.618, 62.5,  8, 8, 1 },
  { "Thailand",                921.000, 62.5,  7, 5, 1 },
  { "USA",                     910.525, 62.5,  7, 5, 1 },
  { "Vietnam (Narrow)",        920.250, 62.5,  8, 5, 1 },
};
static const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);
