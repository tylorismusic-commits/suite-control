// Suite Control — LV2 metadata generator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emits lv2/manifest.ttl and lv2/suite_control.ttl from the parameter table so
// the ports and the UI notifications can never drift.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "params.hpp"

namespace {

constexpr const char* kUri =
    "https://github.com/tylorismusic-commits/suite-control";
constexpr const char* kUiUri =
    "https://github.com/tylorismusic-commits/suite-control#ui";
constexpr const char* kName = "Suite Control";
constexpr const char* kBinary = "suite_control.so";
constexpr const char* kUiBinary = "suite_control_ui.so";
constexpr uint32_t kFirstParam = 4;
constexpr uint32_t kMeterBase = kFirstParam + suite::P_COUNT;

struct Meter {
  const char* symbol;
  const char* name;
};

constexpr Meter kMeters[] = {
    {"gr", "Gain Reduction"},
    {"in_peak_l", "Input Peak L"},
    {"in_peak_r", "Input Peak R"},
    {"out_peak_l", "Output Peak L"},
    {"out_peak_r", "Output Peak R"},
    {"latency", "Latency"},
};

std::string pluginTtl() {
  std::string out;
  out += "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n";
  out += "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
  out += "@prefix doap: <http://usefulinc.com/ns/doap#> .\n";
  out += "@prefix rdf:  <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n";
  out += "@prefix units: <http://lv2plug.in/ns/ext/units#> .\n";
  out += "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .\n\n";
  out += "<" + std::string(kUri) + ">\n";
  out += "    a lv2:Plugin , lv2:CompressorPlugin , lv2:EQPlugin ;\n";
  out += "    doap:name \"" + std::string(kName) + "\" ;\n";
  out += "    doap:license <http://opensource.org/licenses/GPL-3.0> ;\n";
  out += "    lv2:optionalFeature lv2:hardRTCapable ;\n";
  out += "    lv2:port\n";

  std::vector<std::string> ports;
  auto audio = [&](uint32_t index, const char* symbol, const char* name,
                   bool input) {
    std::string s = "        [ a lv2:AudioPort , ";
    s += input ? "lv2:InputPort ;\n" : "lv2:OutputPort ;\n";
    s += "          lv2:index " + std::to_string(index) + " ;\n";
    s += "          lv2:symbol \"" + std::string(symbol) + "\" ;\n";
    s += "          lv2:name \"" + std::string(name) + "\" ]";
    ports.push_back(s);
  };
  audio(0, "in_l", "In L", true);
  audio(1, "in_r", "In R", true);
  audio(2, "out_l", "Out L", false);
  audio(3, "out_r", "Out R", false);

  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    const suite::ParamSpec& p = suite::kParamSpecs[i];
    std::string s = "        [ a lv2:ControlPort , lv2:InputPort ;\n";
    s += "          lv2:index " + std::to_string(kFirstParam + i) + " ;\n";
    s += "          lv2:symbol \"" + std::string(p.symbol) + "\" ;\n";
    s += "          lv2:name \"" + std::string(p.name) + "\" ;\n";
    char num[64];
    std::snprintf(num, sizeof(num), "          lv2:default %g ;\n", p.def);
    s += num;
    std::snprintf(num, sizeof(num), "          lv2:minimum %g ;\n          lv2:maximum %g ;\n",
                  p.min, p.max);
    s += num;
    if (p.unit && p.unit[0]) {
      const char* unit = nullptr;
      if (!std::strcmp(p.unit, "dB")) unit = "units:db";
      else if (!std::strcmp(p.unit, "%")) unit = "units:pc";
      else if (!std::strcmp(p.unit, "ms")) unit = "units:ms";
      else if (!std::strcmp(p.unit, "s")) unit = "units:s";
      else if (!std::strcmp(p.unit, "Hz")) unit = "units:hz";
      if (unit) s += "          units:unit " + std::string(unit) + " ;\n";
    }
    if (p.stepped) {
      s += (p.max <= 1.0) ? "          lv2:portProperty lv2:toggled ;\n"
                          : "          lv2:portProperty lv2:integer ;\n";
    }
    if (p.valueLabels.count > 0) {
      s += "          lv2:scalePoint\n";
      for (int k = 0; k < p.valueLabels.count; ++k) {
        s += "            [ rdfs:label \"" + std::string(p.valueLabels.names[k]) +
             "\" ; rdf:value " + std::to_string(k) + " ]";
        s += (k + 1 == p.valueLabels.count) ? " ;\n" : " ,\n";
      }
    }
    s += "        ]";
    ports.push_back(s);
  }

  uint32_t port = kMeterBase;
  for (const Meter& m : kMeters) {
    std::string s = "        [ a lv2:ControlPort , lv2:OutputPort ;\n";
    s += "          lv2:index " + std::to_string(port++) + " ;\n";
    s += "          lv2:symbol \"" + std::string(m.symbol) + "\" ;\n";
    s += "          lv2:name \"" + std::string(m.name) + "\" ;\n";
    s += "          lv2:default 0 ;\n          lv2:minimum -120 ;\n";
    s += "          lv2:maximum 120 ]";
    ports.push_back(s);
  }

  for (size_t i = 0; i < ports.size(); ++i) {
    out += ports[i];
    out += (i + 1 == ports.size()) ? " .\n\n" : " ,\n";
  }

  out += "<" + std::string(kUiUri) + ">\n";
  out += "    a ui:X11UI ;\n";
  out += "    ui:binary <" + std::string(kUiBinary) + "> ;\n";
  out += "    rdfs:label \"" + std::string(kName) + " UI\" ;\n";
  out += "    lv2:requiredFeature ui:parent ;\n";
  out += "    lv2:optionalFeature ui:scaleFactor , ui:idleInterface , ui:resize ;\n";
  out += "    lv2:extensionData ui:idleInterface ;\n";
  out += "    ui:portNotification\n";
  std::vector<std::string> notes;
  for (uint32_t i = 0; i < suite::P_COUNT; ++i) {
    notes.push_back("        [ ui:plugin <" + std::string(kUri) +
                    "> ; ui:symbol \"" +
                    std::string(suite::kParamSpecs[i].symbol) +
                    "\" ; ui:protocol ui:floatProtocol ]");
  }
  for (const Meter& m : kMeters) {
    notes.push_back("        [ ui:plugin <" + std::string(kUri) +
                    "> ; ui:symbol \"" + std::string(m.symbol) +
                    "\" ; ui:protocol ui:floatProtocol ]");
  }
  for (size_t i = 0; i < notes.size(); ++i) {
    out += notes[i];
    out += (i + 1 == notes.size()) ? " .\n" : " ,\n";
  }
  return out;
}

std::string manifestTtl() {
  std::string out;
  out += "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n";
  out += "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n";
  out += "@prefix ui:   <http://lv2plug.in/ns/extensions/ui#> .\n\n";
  out += "<" + std::string(kUri) + ">\n";
  out += "    a lv2:Plugin ;\n";
  out += "    lv2:binary <" + std::string(kBinary) + "> ;\n";
  out += "    rdfs:seeAlso <suite_control.ttl> ;\n";
  out += "    ui:ui <" + std::string(kUiUri) + "> .\n\n";
  out += "<" + std::string(kUiUri) + ">\n";
  out += "    a ui:X11UI ;\n";
  out += "    ui:binary <" + std::string(kUiBinary) + "> .\n";
  return out;
}

bool writeFile(const std::string& path, const std::string& body, bool check) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f) {
    std::string have;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) have.append(buf, n);
    std::fclose(f);
    if (have == body) return true;
    if (check) {
      std::fprintf(stderr, "gen_ttl: %s is out of date\n", path.c_str());
      return false;
    }
  } else if (check) {
    std::fprintf(stderr, "gen_ttl: missing %s\n", path.c_str());
    return false;
  }
  FILE* o = std::fopen(path.c_str(), "wb");
  if (!o) {
    std::perror(path.c_str());
    return false;
  }
  std::fwrite(body.data(), 1, body.size(), o);
  std::fclose(o);
  return true;
}

} // namespace

int main(int argc, char** argv) {
  bool check = false;
  std::string root = ".";
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--check")) check = true;
    else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
  }
  bool ok = true;
  ok &= writeFile(root + "/lv2/suite_control.ttl", pluginTtl(), check);
  ok &= writeFile(root + "/lv2/manifest.ttl", manifestTtl(), check);
  if (check) return ok ? 0 : 1;
  std::printf("gen_ttl: wrote %s/lv2\n", root.c_str());
  return 0;
}
