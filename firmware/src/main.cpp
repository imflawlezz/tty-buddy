#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

#include "osd.h"
#include "terminal.h"

static constexpr uint32_t LINK_TIMEOUT_MS = 12000;

TFT_eSPI tft;
Terminal term;

enum class LinkUI : uint8_t { Waiting, Live, Lost };

static LinkUI link_ui = LinkUI::Waiting;
static uint32_t last_flush_ms = 0;

static void paintWaiting() {
  term.showBanner("Waiting for daemon", "systemctl start tty-buddy", TFT_CYAN);
}

static void paintLost() {
  term.showBanner("Lost connection to daemon", "waiting to reconnect...",
                  TFT_ORANGE);
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(200);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  term.begin(&tft);
  osd::begin(&tft, &term);
  paintWaiting();
  link_ui = LinkUI::Waiting;
}

void loop() {
  uint32_t now = millis();
  osd::tick(now);

  for (;;) {
    int avail = Serial.available();
    if (avail <= 0)
      break;
    uint8_t buf[1024];
    int n = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
    int got = Serial.readBytes(buf, n);
    if (got > 0)
      term.ingest(buf, (size_t)got);
  }

  // lastGoodFrameMs() is stamped during ingest — must not compare against a
  // pre-ingest millis() (unsigned underflow looks like a link timeout).
  now = millis();
  const bool bye = term.takeBye();
  const bool alive =
      term.linked() && (now - term.lastGoodFrameMs() <= LINK_TIMEOUT_MS);

  if (bye || (link_ui == LinkUI::Live && !alive)) {
    paintLost();
    link_ui = LinkUI::Lost;
  } else if (alive) {
    link_ui = LinkUI::Live;
  }

  if (link_ui == LinkUI::Live && now - last_flush_ms >= 16) {
    last_flush_ms = now;
    term.flush();
  }

  // Always last so content paint cannot overwrite the OSD.
  osd::paint();
}
