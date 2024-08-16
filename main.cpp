#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ftxui/component/captured_mouse.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <hidapi/hidapi.h>
#include <iostream>
#include <memory>
#include <optional>
#include <signal.h>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace std;
using namespace ftxui;

constexpr uint16_t VENDOR_ID = 0x31b2;
constexpr uint16_t PRODUCT_ID = 0x0111;
constexpr int INTERFACE_INDEX = 3; // hid?
constexpr int ENDPOINT_READ = 0x82;
constexpr int ENDPOINT_WRITE = 0x02;

constexpr int BAND_COUNT = 5;

//"4b 00 00 00 00 53 00 00 00 00 00"
const array<uint8_t, 11> CMD_SAVE_SETTINGS = {
    0x4b, 0x00, 0x00, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00,
};
unique_ptr<array<uint8_t, 11>> getCmdSetGainFreq(uint8_t band, float gain,
                                                 uint16_t freq) {
  if (band > 4)
    throw invalid_argument("Band must be between 0 and 4");
  if (abs(gain) > 12)
    throw invalid_argument("Gain must be between -12 and 12");
  if (freq > 20000)
    throw invalid_argument(
        "Frequency must be between 0 and 20000"); // 不知道是不是这样,
                                                  // 但是20000应该够了
  int16_t gain_int = static_cast<int16_t>(gain * 10);
  freq /= 2;
  // 4b 26 00 00 00 57 00 50 00 1a 00 //80 26 //Gain 8, Freq 26x2
  auto cmd = make_unique<array<uint8_t, 11>>();
  cmd->at(0) = 0x4b;
  cmd->at(1) = 0x26 + band * 2;
  cmd->at(2) = 0x00;
  cmd->at(3) = 0x00;
  cmd->at(4) = 0x00;
  cmd->at(5) = 0x57;
  cmd->at(6) = 0x00;
  cmd->at(7) = gain_int & 0xff;
  cmd->at(8) = (gain_int >> 8) & 0xff;
  cmd->at(9) = freq & 0xff;
  cmd->at(10) = (freq >> 8) & 0xff;
  return cmd;
}

unique_ptr<array<uint8_t, 11>> getCmdSetQVal(uint8_t band, float qval) {
  if (band > 4)
    throw invalid_argument("Band must be between 0 and 4");
  if (qval < 0.25 || qval > 12)
    throw invalid_argument("Q value must be between 0.2 and 10");
  int16_t qval_int = static_cast<int16_t>(qval * 1000);
  // "4b 27 00 00 00 57 00 2c 01 00 00", //300   //Q 0.3
  auto cmd = make_unique<array<uint8_t, 11>>();
  cmd->at(0) = 0x4b;
  cmd->at(1) = 0x27 + band * 2;
  cmd->at(2) = 0x00;
  cmd->at(3) = 0x00;
  cmd->at(4) = 0x00;
  cmd->at(5) = 0x57;
  cmd->at(6) = 0x00;
  cmd->at(7) = qval_int & 0xff;
  cmd->at(8) = (qval_int >> 8) & 0xff;
  cmd->at(9) = 0x00;
  cmd->at(10) = 0x00;
  return cmd;
}

unique_ptr<array<uint8_t, 11>> getCmdGetGainFreq(uint8_t band) {
  if (band > 4)
    throw invalid_argument("Band must be between 0 and 4");
  // "4b 28 00 00 00 52 00 00 00 00 00", //Read
  auto cmd = make_unique<array<uint8_t, 11>>();
  cmd->at(0) = 0x4b;
  cmd->at(1) = 0x26 + band * 2;
  cmd->at(2) = 0x00;
  cmd->at(3) = 0x00;
  cmd->at(4) = 0x00;
  cmd->at(5) = 0x52;
  cmd->at(6) = 0x00;
  cmd->at(7) = 0x00;
  cmd->at(8) = 0x00;
  cmd->at(9) = 0x00;
  cmd->at(10) = 0x00;
  return cmd;
}

unique_ptr<array<uint8_t, 11>> getCmdGetQval(uint8_t band) {
  if (band > 4)
    throw invalid_argument("Band must be between 0 and 4");
  // "4b 28 00 00 00 52 00 00 00 00 00", //Read
  auto cmd = make_unique<array<uint8_t, 11>>();
  cmd->at(0) = 0x4b;
  cmd->at(1) = 0x27 + band * 2;
  cmd->at(2) = 0x00;
  cmd->at(3) = 0x00;
  cmd->at(4) = 0x00;
  cmd->at(5) = 0x52;
  cmd->at(6) = 0x00;
  cmd->at(7) = 0x00;
  cmd->at(8) = 0x00;
  cmd->at(9) = 0x00;
  cmd->at(10) = 0x00;
  return cmd;
}

tuple<float, uint16_t> parseGainFreq(const array<uint8_t, 11> &arr) {
  if (arr.at(0) != 0x4b || arr.at(5) != 0x52)
    throw invalid_argument("Invalid packet");
  int16_t gain = arr.at(7) | (arr.at(8) << 8);
  uint16_t freq = arr.at(9) | (arr.at(10) << 8);
  return make_tuple(static_cast<float>(gain) / 10, freq * 2);
}

float parseQval(const array<uint8_t, 11> &arr) {
  if (arr.at(0) != 0x4b || arr.at(5) != 0x52)
    throw invalid_argument("Invalid packet");
  int16_t qval = arr.at(7) | (arr.at(8) << 8);
  return static_cast<float>(qval) / 1000;
}

template <size_t N> string hexDump(const array<uint8_t, N> &arr) {
  stringstream ss;
  for (auto &i : arr) {
    ss << hex << static_cast<int>(i) << " ";
  }
  return ss.str();
}

hid_device *devHandle = nullptr;

int openDevice() {
  devHandle = hid_open(VENDOR_ID, PRODUCT_ID, nullptr);
  if (devHandle == nullptr) {
    cerr << "hid_open failed" << endl;
    return -1;
  }
  return 0;
}

int closeDevice() {
  hid_close(devHandle);
  return 0;
}

optional<tuple<float, uint16_t, float>> readBand(int index) {
  auto cmd = getCmdGetGainFreq(index);
  uint8_t *data = cmd->data();
  int res = hid_write(devHandle, data, 11);
  if (res < 0) {
    cerr << "hid_write failed" << endl;
    return nullopt;
  }
  array<uint8_t, 11> buf;
  res = hid_read(devHandle, buf.data(), 11);
  if (res < 0) {
    cerr << "hid_read failed" << endl;
    return nullopt;
  }
  auto [gain, freq] = parseGainFreq(buf);
  cmd = getCmdGetQval(index);
  data = cmd->data();
  res = hid_write(devHandle, data, 11);
  if (res < 0) {
    cerr << "hid_write failed" << endl;
    return nullopt;
  }
  res = hid_read(devHandle, buf.data(), 11);
  if (res < 0) {
    cerr << "hid_read failed" << endl;
    return nullopt;
  }
  auto qval = parseQval(buf);
  return make_tuple(gain, freq, qval);
}

int writeBand(int index, float gain, uint16_t freq, float qval) {
  auto cmd = getCmdSetGainFreq(index, gain, freq);
  uint8_t *data = cmd->data();
  int res = hid_write(devHandle, data, 11);
  if (res < 0) {
    cerr << "hid_write failed" << endl;
    return -1;
  }
  array<uint8_t, 11> buf;
  res = hid_read(devHandle, buf.data(), 11);
  if (res < 0) {
    cerr << "hid_read failed" << endl;
    return -1;
  }
  cmd = getCmdSetQVal(index, qval);
  data = cmd->data();
  res = hid_write(devHandle, data, 11);
  if (res < 0) {
    cerr << "hid_write failed" << endl;
    return -1;
  }
  res = hid_read(devHandle, buf.data(), 11);
  if (res < 0) {
    cerr << "hid_read failed" << endl;
    return -1;
  }
  return 0;
}

void sigint_handler(int signum) {
  cout << "Exiting..." << endl;
  closeDevice();
  hid_exit();
  exit(0);
}

struct Band {
  float gain;
  uint16_t freq;
  float qval;

private:
  float last_gain;
  uint16_t last_freq;
  float last_qval;

public:
  bool changed() {
    if (gain != last_gain || freq != last_freq || qval != last_qval) {
      last_gain = gain;
      last_freq = freq;
      last_qval = qval;
      return true;
    }
    return false;
  }
  Band(float gain, uint16_t freq, float qval)
      : gain(gain), freq(freq), qval(qval), last_gain(gain), last_freq(freq),
        last_qval(qval) {}
  Band()
      : gain(0), freq(0), qval(0), last_gain(0), last_freq(0), last_qval(0) {}
  bool operator<(const Band &other) const { return freq < other.freq; }
};

int main(int argc, char *argv[]) {
  signal(SIGINT, sigint_handler);
  int res = hid_init();
  res = openDevice();
  if (res < 0) {
    cerr << "Failed to open device" << endl;
    return -1;
  }

  array<Band, 5> bands;
  for (int i = 0; i < 5; i++) {
    auto opt = readBand(i);
    if (opt) {
      auto [gain, freq, qval] = opt.value();
      bands[i] = {gain, freq, qval};
    } else {
      cout << "Failed to read band " << i << endl;
      return -1;
    }
  }
  string txt = "nope";
  array<string, 5> freqStrings;
  vector<Component> bandComponents;
  auto updateBand = [&](int i) {
    writeBand(i, bands[i].gain, bands[i].freq, bands[i].qval);
  };
  for (int i = 0; i < 5; i++) {
    freqStrings[i] = to_string(bands[i].freq);
    auto option = make_unique<InputOption>();
    option->on_enter = [&, i]() {
      bands[i].freq = stoi(freqStrings[i]);
      txt = "freq " + to_string(i) + " set to " + to_string(bands[i].freq);
      // writeBand(i, bands[i].gain, bands[i].freq, bands[i].qval);
    };
    auto thisInput = Input(&freqStrings[i], "Freq" + to_string(i), *option);
    auto thisGainSlider = Slider("Gain", &bands[i].gain, -12, 12, 0.1);
    auto thisQvalSlider = Slider("Q Value", &bands[i].qval, 0.2, 12, 0.1);
    bandComponents.push_back(Renderer(
        Container::Vertical({thisInput, thisGainSlider, thisQvalSlider}),
        [=, &bands, &txt]() {
          if (bands[i].changed()) {
            txt = "updated band " + to_string(i);
            updateBand(i);
            this_thread::sleep_for(chrono::milliseconds(50));
          } else {
            txt = to_string(bands[0].qval);
          }

          return hbox({
              text("Band " + to_string(i) + ": "),
              thisInput->Render() | size(WIDTH, EQUAL, 6),
              text("Hz"),
              separator(),
              thisGainSlider->Render(),
              text("(" + to_string(bands[i].gain).substr(0, 5) + ")"),
              separator(),
              thisQvalSlider->Render(),
              text("(" + to_string(bands[i].qval).substr(0, 5) + ")"),
          });
        }));
  }

  cout << "Loaded" << endl;
  auto screen = ScreenInteractive::Fullscreen();
  auto components = Container::Vertical(bandComponents);
  auto renderer = Renderer(components, [&]() {
    return vbox({
        text(txt) | vcenter | bold,
        separator(),
        components->Render(),
    })|border;
  });
  int sz = 20;
  screen.Loop(renderer);

  return 0;
  cout << "Done" << endl;
  return 0;
}
