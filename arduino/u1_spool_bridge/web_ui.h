#pragma once

#include <Arduino.h>

#include "config.h"
#include "send_gate.h"
#include "spool_data.h"
#include "u1_client.h"

// Defined in main.cpp: arm the radio-off self test for the next boot.
extern bool g_bandFellBack;
extern U1Slot g_slots[4];
extern bool   g_slotsKnown;
extern String g_slotsErr;

void requestRadioTest();

void webBegin();
void webLoop();

void webBroadcastTag(uint8_t reader, const SpoolData &d, const String &note);
void webBroadcastStatus();
void webLog(const String &msg, const char *level = "");

// "2.4 GHz" / "5 GHz", or "" when not associated.
const char *wifiBandName();

// Work queued by the web handlers and executed from loop(), so the AsyncTCP
// task never blocks on a socket to the printer.
struct PendingWork {
  bool      send = false;
  bool      ping = false;
  bool      reboot = false;
  bool      smList = false;   // fetch the spool picker list
  bool      smLink = false;   // attach the current tag UID to smLinkId
  bool      smPing = false;
  bool      fleet = false;
  bool      slots = false;    // re-read what the printer says is loaded    // browse the other boxes over mDNS
  bool      groupApply = false;  // push a setting out to the other boxes
  bool      smRefile = false;    // re-file our loaded spools under a new location
  bool      dump = false;        // read out every sector of the tag on the reader
  bool      arm = false;      // arm a slot (TRIG_ARMED)
  uint8_t   armChannel = 0;
  uint32_t  smLinkId = 0;
  SpoolData spool;
  uint8_t   channel = 0;
  uint8_t   reader = 0;  // which lane a queued send/link belongs to
};
// One lane per reader. A dual-slot drybox runs two readers off one board, and
// they must not share state — each has its own spool, its own gate and its own
// slot binding.
struct Lane {
  GateState gate;
  SpoolData spool;
  bool      firstTagSeen = false;
};

extern PendingWork g_work;
extern Lane        g_lanes[MAX_READERS];
extern uint8_t     g_activeLane;  // whichever reader last had something happen
extern bool        g_printerOk;
extern bool        g_spoolmanOk;

// The spool the web UI is currently looking at.
static inline SpoolData &activeSpool() { return g_lanes[g_activeLane].spool; }

// Printer slot occupancy (defined in main.cpp)
extern bool       g_chanPresent[4];
extern bool       g_chanKnown;
GateConfig        gateCfg();

// OTA progress, pushed to every open browser.
// state: "start" | "progress" | "done" | "error"
// Progress of a box-to-box firmware push, one line per peer.
void webFleetPush(const String &peer, const char *state, const String &msg);

// The result of a card dump, as pasteable text. Also retained on the box and
// served from GET /api/dump, so a browser that dropped during the read (it
// takes minutes on an unknown tag) can still collect it afterwards.
// Sent to whoever is listening AND kept.
void webDumpResult(const String &text);
void webDumpProgress(uint8_t done, uint8_t total);

// Renaming a group writes the new name to every box in it. Done from the box
// rather than the browser, for the same reason the firmware push is.
void fleetSetApply(const String &payloadJson, const String &peersCsv, bool refile);
void fleetSetRun();

void webOtaEvent(const char *state, int pct, const char *msg);

void webBroadcastSpoolList(const String &jsonArray, const String &err);
void webBroadcastFleet(const String &jsonArray, const String &err);
// The compact status one node publishes to the others.
void webBriefJson(String &out);
