#include "sim.cpp"
#include "../firmware/obdojo/obdojo_fw.cpp"
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("  FAIL: %s\n", msg); fails++; } else printf("  ok: %s\n", msg); } while (0)
static bool has(const String &s, const char *x) { return s.s.find(x) != std::string::npos; }
static String run(const char *c) { return runCommand(c, CH_WEB); }
static void spin(uint32_t ms) { uint32_t t = millis() + ms; while (millis() < t) loop(); }
static String E(const char *c) {
  SerialBT.out.clear(); SerialBT.in = std::string(c) + "\r"; SerialBT.pos = 0;
  loop();
  std::string o = SerialBT.out;
  for (auto &ch : o) if (ch == '\r') ch = '|';
  return String(o);
}
static float num(const String &j, const char *key) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = j.s.find(k);
  if (p == std::string::npos) return -9999;
  return atof(j.s.c_str() + p + k.size());
}
static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

int main() {
  puts("== boot and connect");
  setup();
  CHECK(WiFi.softAPIP().toString() == "1.1.1.1", "hotspot address 1.1.1.1");
  spin(3000);
  String st = run("status");
  CHECK(has(st, "\"ecu\":true"), "ECU found");
  CHECK(has(st, "\"ecu_id\":\"21175-0861\""), "ECU part number from 1A");
  CHECK(has(st, "\"kb\":\"EA 8F\""), "keywords EA 8F");
  CHECK(has(st, "\"echo\":1"), "transceiver echo seen");
  CHECK(near(num(st, "init_ms"), 25.0f, 0.3f), "wake-up pulse measured at 25 ms");
  CHECK(has(st, "\"tester\":\"F1\""), "tester F1 first");
  CHECK(has(st, "\"supp\":\"02 04 05 06 07 08 09 0A 0B 0C 44 56 5B 5E 5F 61 62\""), "supported registers from the bitmaps");
  CHECK(g_badWakes == 0, "no bad wake-up pulses");

  puts("== live data");
  bike.rpm = 6050; bike.tps = 548; bike.kmh = 72.5f; bike.gear = 3; bike.ect = 92; bike.iat = 31;
  bike.mapKpa = 55; bike.baroKpa = 100.5f; bike.vbat = 14.2f; bike.subV = 2.5f;
  run("live"); spin(8000);
  String lv = liveJson(); puts(lv.c_str());
  CHECK(near(num(lv, "rpm"), 6050, 1), "RPM (A*100 + B*100/256)");
  CHECK(near(num(lv, "tps"), 50.0f, 0.3f), "throttle % from raw 548 between 201 and 895");
  CHECK(near(num(lv, "tpsv"), 548 * 5.0f / 1024, 0.01f), "throttle volts");
  CHECK(near(num(lv, "spd"), 72.5f, 0.1f), "speed");
  CHECK(near(num(lv, "gear"), 3, 0), "gear straight from the ECU");
  CHECK(near(num(lv, "ect"), 92, 0.7f), "coolant");
  CHECK(near(num(lv, "iat"), 31, 0.7f), "intake air");
  CHECK(near(num(lv, "map"), 55, 0.2f), "MAP kPa");
  CHECK(near(num(lv, "baro"), 100.5f, 0.6f), "atmospheric kPa");
  CHECK(near(num(lv, "vbat"), 14.2f, 0.1f), "battery from register 0A");
  CHECK(near(num(lv, "sub"), 2.5f, 0.03f), "sub-throttle volts");
  CHECK(has(lv, "\"pk\":{\"rpm\":6050"), "peak rpm");
  CHECK(num(lv, "rate") > 5, "at least 5 requests a second");
  printf("  (rate %.1f req/s)\n", num(lv, "rate"));

  puts("== settings that change decoding");
  CHECK(has(run("set rpm_form 1"), "\"ok\":true"), "rpm formula 1");
  run("live"); spin(3000);
  CHECK(near(num(liveJson(), "rpm"), 6000 + 128, 1), "RPM with A*100 + B");
  run("set rpm_form 0");
  CHECK(has(run("set regs iat=05 map=07"), "\"ok\":true"), "swap iat/map registers");
  run("live"); spin(8000);
  lv = liveJson();
  CHECK(near(num(lv, "map"), 48.5f, 0.3f) && near(num(lv, "iat"), 39.4f, 0.4f), "swapped registers decode the other values");
  CHECK(has(run("set regs map=zz"), "register must be"), "bad register rejected");
  CHECK(has(run("set regs bogus=05"), "Unknown name"), "unknown name rejected");
  run("set regs " REGS_DEF);
  CHECK(has(run("set regs sub=--"), "\"ok\":true"), "turn sub-throttle off");
  run("live"); spin(8000);
  CHECK(has(liveJson(), "\"sub\":null"), "sub-throttle off -> null");
  run("set regs " REGS_DEF);

  puts("== custom signal");
  CHECK(has(run("set sig1 5E:0:1:b:0.01953:0:Tilt:V"), "\"ok\":true"), "sig1 accepted");
  CHECK(has(run("set sig2 hello"), "Format"), "bad signal rejected");
  CHECK(has(run("set sig2 ZZ:0:1:b:1:0:x:y"), "hex"), "bad register rejected");
  run("live"); spin(8000);
  CHECK(near(num(liveJson(), "c1"), 0xCF * 0.01953f, 0.01f), "custom signal from register 5E");

  puts("== codes");
  bike.cur = {{0x11, 0x04}}; bike.sto = {{0x11, 0x04}, {0x15, 0x08}};
  String c = run("codes"); puts(c.c_str());
  CHECK(has(c, "\"current\":[{\"code\":\"11\",\"desc\":\"Main throttle sensor\",\"st\":\"04\"}]"), "current code 11");
  CHECK(has(c, "\"code\":\"15\",\"desc\":\"Atmospheric pressure sensor\""), "stored code 15");
  CHECK(has(c, "\"count\":2"), "failure count register 56");
  CHECK(has(c, "\"fi\":true"), "FI on with a current code");
  String fz = run("freeze"); puts(fz.c_str());
  CHECK(has(fz, "\"count\":2") && has(fz, "\"code\":\"15\""), "freeze records");
  CHECK(has(run("clear"), "confirm"), "clear needs yes");
  CHECK(has(run("clear yes"), "\"ok\":true") && bike.cur.empty(), "clear codes");
  c = run("codes");
  CHECK(has(c, "\"current\":[]") && has(c, "\"fi\":false"), "no codes after clear");

  puts("== ELM327 over Bluetooth");
  bike.cur = {{0x13, 0x00}};
  run("codes");
  String r = E("ATZ"); CHECK(has(r, "ELM327 v1.5"), "ATZ");
  E("ATE0");
  r = E("ATDP"); CHECK(has(r, "ISO 15765-4"), "ATDP");
  r = E("0100"); puts(r.c_str()); CHECK(has(r, "41 00 88 3A 80 11"), "0100 bitmap (01 05 0B 0C 0D 0F 11 1C 20)");
  r = E("0140"); CHECK(has(r, "41 40 40 00 00 00"), "0140 bitmap (42)");
  bike.rpm = 4000; spin(1500);
  r = E("010C"); puts(r.c_str()); CHECK(has(r, "41 0C 3E 80"), "010C = 4000 rpm");
  r = E("0105"); CHECK(has(r, "41 05 84"), "0105 coolant 92 C");
  r = E("010D"); CHECK(has(r, "41 0D 49"), "010D 73 km/h (72.5 rounded)");
  r = E("0111"); CHECK(has(r, "41 11 7F") || has(r, "41 11 80"), "0111 throttle 50%");
  r = E("0142"); CHECK(has(r, "41 42 37"), "0142 battery ~14.2 V");
  r = E("010C0D"); CHECK(has(r, "41 0C 3E 80 0D 49"), "two PIDs in one request");
  r = E("0101"); CHECK(has(r, "41 01 81"), "0101 MIL + 1 code");
  r = E("03"); puts(r.c_str()); CHECK(has(r, "43 01 10 13"), "03 -> P1013");
  r = E("2104"); CHECK(has(r, "61 04 02 24"), "2104 passes through to the ECU");
  r = E("22F010"); CHECK(has(r, "62 F0 10 03"), "22F010 gear");
  r = E("22F012"); CHECK(has(r, "62 F0 12 09 C4"), "22F012 sub-throttle mV");
  r = E("2701"); CHECK(has(r, "NO DATA"), "security access not passed through");
  r = E("04"); CHECK(has(r, "44") && bike.cur.empty(), "04 clears codes");
  r = E("ATRV"); CHECK(has(r, "14.2V"), "ATRV from register 0A");

  puts("== Bluetooth app");
  SerialBT.out.clear(); SerialBT.in = "!status\r"; SerialBT.pos = 0; loop();
  CHECK(SerialBT.out.rfind("{\"t\":\"status\"", 0) == 0, "!status -> JSON line");

  puts("== register sweep");
  CHECK(has(run("sweep start"), "\"on\":true"), "sweep starts");
  for (int i = 0; i < 60 && !has(run("sweep"), "\"done\":true"); i++) spin(1000);
  String sw = run("sweep"); puts(sw.s.substr(0, 300).c_str());
  CHECK(has(sw, "\"done\":true"), "sweep finishes");
  CHECK(has(sw, "\"found\":21"), "21 registers answer (incl. bitmaps 00 20 40 60)");
  CHECK(has(sw, "\"r\":\"04\",\"nm\":\"Main throttle sensor\""), "register 04 named");
  CHECK(has(sw, "\"refused\":235"), "the rest refused");
  CHECK(has(run("status"), "\"ecu\":true"), "still connected after the sweep");

  puts("== register monitor");
  CHECK(has(run("mon start 04 09"), "\"on\":true"), "monitor 04 and 09");
  spin(3000);
  run("mon mark");
  bike.tps = 700;
  spin(3000);
  String mn = run("mon"); puts(mn.c_str());
  CHECK(has(mn, "\"r\":\"04\"") && has(mn, "\"d\":\"02 BC\""), "monitor sees throttle 02BC");
  CHECK(has(mn, "\"m\":3") || has(mn, "\"m\":2") || has(mn, "\"m\":1"), "throttle bytes marked as changed");
  run("mon stop");
  CHECK(has(run("mon start all"), "\"r\":\"62\""), "monitor all uses the sweep list");
  run("mon stop");

  puts("== watch, raw, blocked services");
  String pk = run("peek 21 0B"); CHECK(has(pk, "\"hex\":\"61 0B 03\""), "peek gear register");
  CHECK(has(run("peek 14 01"), "only allows read"), "peek refuses clear");
  CHECK(has(run("raw 21 04"), "Raw commands are off"), "raw off by default");
  run("set allow_raw 1");
  CHECK(has(run("raw 21 04"), "\"hex\":\"61 04 02 BC\""), "raw read");
  CHECK(has(run("raw 27 01"), "Blocked"), "security access blocked");
  CHECK(has(run("raw 34 00 00"), "Blocked"), "download blocked");
  CHECK(has(run("raw 10 85"), "Blocked"), "programming session blocked");
  run("set allow_raw 0");

  puts("== throttle learn");
  bike.tps = 230;
  CHECK(has(run("tps closed"), "\"min\":230"), "learn closed");
  bike.tps = 880;
  CHECK(has(run("tps open"), "\"max\":880"), "learn open");
  bike.tps = 555; run("live"); spin(3000);
  CHECK(near(num(liveJson(), "tps"), 50.0f, 0.3f), "throttle % with learned range");
  run("tps reset");

  puts("== ident, kline, ABS");
  String id = run("ident"); puts(id.c_str());
  CHECK(has(id, "21175-0861") && has(id, "39.9 h"), "ident: part number and run time");
  String kl = run("kline");
  CHECK(has(kl, "\"trace\":[") && has(kl, "\"d\":\"T\"") && has(kl, "\"d\":\"R\""), "frame trace");
  String ab = run("abs"); puts(ab.c_str());
  CHECK(has(ab, "\"id\":\"ABS-SIM\"") && has(ab, "\"code\":\"42\""), "ABS ECU id and code");
  spin(3000);
  st = run("status");
  CHECK(has(st, "\"ecu\":true") && has(st, "\"target\":\"11\""), "back on the engine ECU after ABS");

  puts("== key off / on");
  simc.keyOn = false;
  spin(4000);
  CHECK(has(run("status"), "\"ecu\":false"), "ECU lost with the key off");
  CHECK(has(liveJson(), "\"rpm\":null"), "live values cleared");
  r = E("010C"); CHECK(has(r, "NO DATA"), "ELM NO DATA with key off");
  simc.keyOn = true;
  spin(12000);
  CHECK(has(run("status"), "\"ecu\":true"), "reconnects at key on");

  puts("== ktest");
  String kt = run("ktest"); puts(kt.c_str());
  CHECK(has(kt, "25.0 ms low") && has(kt, "Our bytes came back") && has(kt, "Keywords EA 8F") && has(kt, "21 09 -> 61 09"), "ktest all steps pass");

  puts("== tester address: ECU only answers F2");
  simc.needTester = 0xF2;
  simc.keyOn = false; spin(4000); simc.keyOn = true;
  spin(15000);
  st = run("status");
  CHECK(has(st, "\"ecu\":true") && has(st, "\"tester\":\"F2\""), "auto mode found tester F2");
  simc.needTester = 0;

  puts("== MCP2003A cuts the wake-up at 22 ms");
  simc.cutMs = 22;
  run("ktest");
  st = run("status");
  CHECK(has(st, "\"ecu\":false"), "no connection with a cut pulse");
  CHECK(has(st, "cut to 22.0 ms"), "error explains the cut pulse");
  CHECK(near(num(st, "init_ms"), 22.0f, 0.2f), "measured 22 ms");
  puts("   ...fit the init MOSFET on GPIO25");
  run("set kl_init 25"); applyKCfg();
  spin(12000);
  CHECK(has(run("status"), "\"ecu\":true"), "connects with the MOSFET init");
  simc.cutMs = 0; run("set kl_init 0"); applyKCfg();

  puts("== transceiver not powered");
  simc.xcvr = false;
  run("ktest");
  st = run("status");
  CHECK(has(st, "\"ecu\":false") && has(st, "No echo"), "no echo reported");
  simc.xcvr = true;
  spin(12000);
  CHECK(has(run("status"), "\"ecu\":true"), "recovers");

  puts("== logging");
  run("log start"); run("live");
  for (int i = 0; i < 10; i++) { run("live"); spin(500); }
  String lr = run("logrows 0 3"); puts(lr.c_str());
  CHECK(has(lr, "seconds,rpm,tps_pct") && has(lr, "\"n\":3"), "log rows with header");
  run("log stop");

  printf("%d failure(s)\n", fails);
  return fails ? 1 : 0;
}
