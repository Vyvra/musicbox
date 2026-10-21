// Wi-Fi provisioning by config stick: parsing wifi.txt, and the NVS round trip.
//
// The radio itself is not here (it needs WiFi.h); the parts that can be wrong silently —
// what the file means and whether it survives a power cycle — are tested.
#include <cstdio>
#include <string>

#include "mounted.h"
#include "wifi_config.h"

static int failures = 0;

static void expect(bool ok, const std::string& what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) failures++;
}

int main(int argc, char** argv) {
  if (argc < 5) {
    printf("usage: %s <wifi.img> <alias.img> <nosssid.img> <plain.img>\n", argv[0]);
    return 2;
  }

  Mounted wifi(argv[1]);
  WifiConfig config;
  String error;
  expect(readWifiConfig(wifi.storage.fs(), "/", config, &error), "reads wifi.txt");
  expect(config.ssid == "MyNetwork", "ssid parsed");
  expect(config.password == "correct horse battery staple",
         "password parsed, spaces intact");
  expect(error.length() == 0, "no error reported on success");

  // Storage: the stick is only needed once, so this must survive a reboot.
  clearWifiConfig();
  WifiConfig loaded;
  expect(!loadWifiConfig(loaded), "nothing stored before provisioning");
  expect(saveWifiConfig(config), "credentials saved");
  expect(loadWifiConfig(loaded) && loaded.ssid == config.ssid &&
             loaded.password == config.password,
         "credentials round trip through NVS");
  clearWifiConfig();
  expect(!loadWifiConfig(loaded), "clear removes them");

  // An open network has an empty password; that must store and load, not read as failure.
  WifiConfig openNet;
  openNet.ssid = "OpenNet";
  openNet.password = "";
  expect(saveWifiConfig(openNet), "open network saves");
  WifiConfig back;
  expect(loadWifiConfig(back) && back.ssid == "OpenNet" && back.password.length() == 0,
         "empty password survives the round trip");

  Mounted alias(argv[2]);
  WifiConfig aliasCfg;
  expect(readWifiConfig(alias.storage.fs(), "/", aliasCfg) && aliasCfg.ssid == "fritzbox" &&
             aliasCfg.password == "geheim",
         "network=/psk= spellings accepted");

  Mounted noSsid(argv[3]);
  WifiConfig bad;
  String why;
  expect(!readWifiConfig(noSsid.storage.fs(), "/", bad, &why) && why == "wifi.txt has no ssid",
         "a wifi.txt without an ssid is rejected, with a reason");
  expect(!bad.complete(), "nothing partial is returned on failure");

  Mounted plain(argv[4]);
  WifiConfig none;
  String whyNot;
  expect(!readWifiConfig(plain.storage.fs(), "/", none, &whyNot) &&
             whyNot == "no wifi.txt on the stick",
         "an ordinary music stick is not mistaken for a config stick");

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
