// Компонент: принимает 65-байтовые ответы Ballu/TCL и публикует состояние в ESPHome.

#include "tcl_climate.h"

#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tcl_climate {

static constexpr uint8_t REQ_CMD[] = {0xBB, 0x00, 0x01, 0x04, 0x02, 0x01, 0x00, 0xBD};
static constexpr int MAX_LINE_LENGTH = 100;
static constexpr int UPDATE_INTERVAL_MS = 3000;

static_assert(sizeof(TCLClimate::get_cmd_resp_t) == 65, "Ответ Ballu/TCL должен занимать ровно 65 байт");
static_assert(sizeof(TCLClimate::set_cmd_t) == 35, "Команда Ballu/TCL должна занимать ровно 35 байт");

void TCLClimate::set_current_temperature(float current_temperature) {
  // Функция: обновляет текущую температуру только при реальном изменении.
  if (std::abs(this->current_temperature - current_temperature) < 0.1f)
    return;

  ESP_LOGD("TCL", "Current temperature updated to: %.1f°C", current_temperature);
  this->is_changed = true;
  this->current_temperature = current_temperature;
}
// Пример вызова: this->set_current_temperature(24.5f);

void TCLClimate::set_custom_fan_mode(StringRef fan_mode) {
  // Функция: обновляет пользовательский режим вентилятора.
  StringRef current(this->get_custom_fan_mode());
  if (!current.empty() && fan_mode == current.c_str())
    return;

  ESP_LOGI("TCL", "Fan mode changed to: %s", fan_mode.c_str());
  this->is_changed = true;
  this->set_custom_fan_mode_(fan_mode.c_str());
}
// Пример вызова: this->set_custom_fan_mode(StringRef("Automatic"));

void TCLClimate::set_mode(climate::ClimateMode mode) {
  // Функция: обновляет режим работы кондиционера.
  if (this->mode == mode)
    return;

  const char *mode_str = "";
  switch (mode) {
    case climate::CLIMATE_MODE_OFF:
      mode_str = "OFF";
      break;
    case climate::CLIMATE_MODE_COOL:
      mode_str = "COOL";
      break;
    case climate::CLIMATE_MODE_HEAT:
      mode_str = "HEAT";
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      mode_str = "FAN ONLY";
      break;
    case climate::CLIMATE_MODE_DRY:
      mode_str = "DRY";
      break;
    case climate::CLIMATE_MODE_AUTO:
      mode_str = "AUTO";
      break;
    default:
      mode_str = "UNKNOWN";
      break;
  }

  ESP_LOGI("TCL", "Climate mode changed to: %s", mode_str);
  this->is_changed = true;
  this->mode = mode;
}
// Пример вызова: this->set_mode(climate::CLIMATE_MODE_COOL);

void TCLClimate::set_swing_mode(climate::ClimateSwingMode swing_mode) {
  // Функция: обновляет общий режим качания жалюзи.
  if (this->swing_mode == swing_mode)
    return;

  const char *swing_str = "";
  switch (swing_mode) {
    case climate::CLIMATE_SWING_OFF:
      swing_str = "OFF";
      break;
    case climate::CLIMATE_SWING_BOTH:
      swing_str = "BOTH";
      break;
    case climate::CLIMATE_SWING_VERTICAL:
      swing_str = "VERTICAL";
      break;
    case climate::CLIMATE_SWING_HORIZONTAL:
      swing_str = "HORIZONTAL";
      break;
    default:
      swing_str = "UNKNOWN";
      break;
  }

  ESP_LOGI("TCL", "Swing mode changed to: %s", swing_str);
  this->is_changed = true;
  this->swing_mode = swing_mode;
}
// Пример вызова: this->set_swing_mode(climate::CLIMATE_SWING_VERTICAL);

void TCLClimate::set_hswing_pos(const std::string &hswing_pos) {
  // Функция: сохраняет фактическую горизонтальную позицию жалюзи.
  if (this->hswing_pos == hswing_pos)
    return;

  ESP_LOGI("TCL", "Horizontal swing position: %s", hswing_pos.c_str());
  this->hswing_pos = hswing_pos;
}
// Пример вызова: this->set_hswing_pos("Fix mid");

void TCLClimate::set_vswing_pos(const std::string &vswing_pos) {
  // Функция: сохраняет фактическую вертикальную позицию жалюзи.
  if (this->vswing_pos == vswing_pos)
    return;

  ESP_LOGI("TCL", "Vertical swing position: %s", vswing_pos.c_str());
  this->vswing_pos = vswing_pos;
}
// Пример вызова: this->set_vswing_pos("Fix upper");

void TCLClimate::set_target_temperature(float target_temperature) {
  // Функция: обновляет установленную температуру.
  if (std::abs(this->target_temperature - target_temperature) < 0.1f)
    return;

  ESP_LOGI("TCL", "Target temperature changed to: %.1f°C", target_temperature);
  this->is_changed = true;
  this->target_temperature = target_temperature;
}
// Пример вызова: this->set_target_temperature(24.0f);

void TCLClimate::build_set_cmd(get_cmd_resp_t *get_cmd_resp) {
  // Функция: строит 35-байтовую команду из последнего корректного состояния кондиционера.
  memcpy(this->m_set_cmd.raw, this->set_cmd_base, sizeof(this->m_set_cmd.raw));

  this->m_set_cmd.data.power = get_cmd_resp->data.power;
  this->m_set_cmd.data.off_timer_en = 0;
  this->m_set_cmd.data.on_timer_en = 0;
  // Бит 5 управляющего byte_7: звуковое подтверждение команды.
  this->m_set_cmd.data.beep = this->beeper_enabled_ ? 1 : 0;

  // Бит 6 управляющего byte_7: дисплей внутреннего блока.
  // При выключенном кондиционере дисплей также выключаем.
  this->m_set_cmd.data.disp =
      (this->display_enabled_ && get_cmd_resp->data.power) ? 1 : 0;
  this->m_set_cmd.data.eco = 0;
  this->m_set_cmd.data.turbo = get_cmd_resp->data.turbo;
  this->m_set_cmd.data.mute = get_cmd_resp->data.mute;

  static constexpr uint8_t MODE_MAP[] = {
      0x00,  // 0x00 — не используется
      0x03,  // ответ 0x01 COOL     -> команда 0x03
      0x07,  // ответ 0x02 FAN_ONLY -> команда 0x07
      0x02,  // ответ 0x03 DRY      -> команда 0x02
      0x01,  // ответ 0x04 HEAT     -> команда 0x01
      0x08   // ответ 0x05 AUTO     -> команда 0x08
  };

  if (get_cmd_resp->data.mode < sizeof(MODE_MAP))
    this->m_set_cmd.data.mode = MODE_MAP[get_cmd_resp->data.mode];

  // В команде TCL байт №9 кодируется по формуле: T = 0x6F - byte_9.
  // В ответе низкие 4 бита byte_8 содержат смещение температуры от 16°C.
  const uint8_t target_temperature =
      static_cast<uint8_t>(16 + get_cmd_resp->data.temp);
  this->m_set_cmd.data.temp =
      static_cast<uint8_t>(0x6F - target_temperature);

  static constexpr uint8_t FAN_MAP[] = {
      0x00,  // auto
      0x02,  // скорость 1
      0x03,  // скорость 3
      0x05,  // скорость 5
      0x06,  // скорость 2
      0x07   // скорость 4
  };

  if (get_cmd_resp->data.fan < sizeof(FAN_MAP))
    this->m_set_cmd.data.fan = FAN_MAP[get_cmd_resp->data.fan];

  if (get_cmd_resp->data.vswing_mv) {
    this->m_set_cmd.data.vswing = 0x07;
    this->m_set_cmd.data.vswing_fix = 0;
    this->m_set_cmd.data.vswing_mv = get_cmd_resp->data.vswing_mv;
  } else if (get_cmd_resp->data.vswing_fix) {
    this->m_set_cmd.data.vswing = 0;
    this->m_set_cmd.data.vswing_fix = get_cmd_resp->data.vswing_fix;
    this->m_set_cmd.data.vswing_mv = 0;
  }

  if (get_cmd_resp->data.hswing_mv) {
    this->m_set_cmd.data.hswing = 0x01;
    this->m_set_cmd.data.hswing_fix = 0;
    this->m_set_cmd.data.hswing_mv = get_cmd_resp->data.hswing_mv;
  } else if (get_cmd_resp->data.hswing_fix) {
    this->m_set_cmd.data.hswing = 0;
    this->m_set_cmd.data.hswing_fix = get_cmd_resp->data.hswing_fix;
    this->m_set_cmd.data.hswing_mv = 0;
  }

  this->m_set_cmd.data.half_degree = 0;

  // В управляющем byte_33 старший бит всегда должен быть установлен:
  // 0x80 — горизонтальные жалюзи выключены/зафиксированы.
  this->m_set_cmd.data.byte_33_bit_6_7 = 0x02;

  uint8_t xor_byte = 0;
  for (size_t i = 0; i < sizeof(this->m_set_cmd.raw) - 1; i++)
    xor_byte ^= this->m_set_cmd.raw[i];

  this->m_set_cmd.raw[sizeof(this->m_set_cmd.raw) - 1] = xor_byte;
}
// Пример вызова: this->build_set_cmd(&this->m_get_cmd_resp);

void TCLClimate::setup() {
  // Функция: инициализирует период опроса и доступные скорости вентилятора.
  this->set_update_interval(UPDATE_INTERVAL_MS);
  this->set_supported_custom_fan_modes({"Turbo", "Mute", "Automatic", "1", "2", "3", "4", "5"});
}
// Пример вызова: ESPHome вызывает setup() автоматически при запуске.

void TCLClimate::send_current_state_command_() {
  // Функция: повторно отправляет текущее состояние с новыми настройками звука/дисплея.
  if (!this->has_valid_state_) {
    ESP_LOGW("TCL", "Command ignored: no valid 65-byte state received yet");
    return;
  }

  get_cmd_resp_t state = {0};
  memcpy(state.raw, this->m_get_cmd_resp.raw, sizeof(state.raw));

  this->build_set_cmd(&state);
  this->ready_to_send_set_cmd_flag = false;
  this->write_array(this->m_set_cmd.raw, sizeof(this->m_set_cmd.raw));
}
// Пример вызова: this->send_current_state_command_();

void TCLClimate::set_beeper_enabled(bool enabled) {
  // Функция: включает или отключает звуковое подтверждение команд кондиционером.
  if (this->beeper_enabled_ == enabled)
    return;

  this->beeper_enabled_ = enabled;
  ESP_LOGI("TCL", "Command beeper: %s", enabled ? "ON" : "OFF");
  this->send_current_state_command_();
}
// Пример вызова: this->set_beeper_enabled(false);

void TCLClimate::set_display_enabled(bool enabled) {
  // Функция: включает или отключает дисплей внутреннего блока.
  if (this->display_enabled_ == enabled)
    return;

  this->display_enabled_ = enabled;
  ESP_LOGI("TCL", "Indoor unit display: %s", enabled ? "ON" : "OFF");
  this->send_current_state_command_();
}
// Пример вызова: this->set_display_enabled(false);

void TCLClimate::control_vertical_swing(const std::string &swing_mode) {
  // Функция: включает вертикальное качание либо фиксирует жалюзи в выбранном положении.
  if (!this->has_valid_state_) {
    ESP_LOGW("TCL", "Vertical louver command ignored: no valid 65-byte state received yet");
    return;
  }

  ESP_LOGI("TCL", "Vertical louver command: %s", swing_mode.c_str());

  get_cmd_resp_t get_cmd_resp = {0};
  memcpy(get_cmd_resp.raw, this->m_get_cmd_resp.raw, sizeof(get_cmd_resp.raw));

  get_cmd_resp.data.vswing = 0;
  get_cmd_resp.data.vswing_mv = 0;
  get_cmd_resp.data.vswing_fix = 0;

  if (swing_mode == "Move full") {
    get_cmd_resp.data.vswing = 1;
    get_cmd_resp.data.vswing_mv = 0x01;
  } else if (swing_mode == "Move upper") {
    get_cmd_resp.data.vswing = 1;
    get_cmd_resp.data.vswing_mv = 0x02;
  } else if (swing_mode == "Move lower") {
    get_cmd_resp.data.vswing = 1;
    get_cmd_resp.data.vswing_mv = 0x03;
  } else if (swing_mode == "Fix top") {
    get_cmd_resp.data.vswing_fix = 0x01;
  } else if (swing_mode == "Fix upper") {
    get_cmd_resp.data.vswing_fix = 0x02;
  } else if (swing_mode == "Fix mid") {
    get_cmd_resp.data.vswing_fix = 0x03;
  } else if (swing_mode == "Fix lower") {
    get_cmd_resp.data.vswing_fix = 0x04;
  } else if (swing_mode == "Fix bottom") {
    get_cmd_resp.data.vswing_fix = 0x05;
  } else if (swing_mode != "Off") {
    ESP_LOGW("TCL", "Unknown vertical louver command: %s", swing_mode.c_str());
    return;
  }

  // Сохраняем выбранное положение, чтобы следующая команда его не отменила.
  memcpy(this->m_get_cmd_resp.raw, get_cmd_resp.raw,
         sizeof(this->m_get_cmd_resp.raw));

  this->build_set_cmd(&get_cmd_resp);

  // Команда уходит немедленно.
  this->ready_to_send_set_cmd_flag = false;
  this->write_array(this->m_set_cmd.raw, sizeof(this->m_set_cmd.raw));
}
// Пример вызова: this->control_vertical_swing("Fix mid");


void TCLClimate::control_horizontal_swing(const std::string &swing_mode) {
  // Функция: включает горизонтальное качание либо фиксирует жалюзи в выбранном положении.
  if (!this->has_valid_state_) {
    ESP_LOGW("TCL", "Horizontal louver command ignored: no valid 65-byte state received yet");
    return;
  }

  ESP_LOGI("TCL", "Horizontal louver command: %s", swing_mode.c_str());

  get_cmd_resp_t get_cmd_resp = {0};
  memcpy(get_cmd_resp.raw, this->m_get_cmd_resp.raw, sizeof(get_cmd_resp.raw));

  get_cmd_resp.data.hswing = 0;
  get_cmd_resp.data.hswing_mv = 0;
  get_cmd_resp.data.hswing_fix = 0;

  if (swing_mode == "Move full") {
    get_cmd_resp.data.hswing = 1;
    get_cmd_resp.data.hswing_mv = 0x01;
  } else if (swing_mode == "Move left") {
    get_cmd_resp.data.hswing = 1;
    get_cmd_resp.data.hswing_mv = 0x02;
  } else if (swing_mode == "Move mid") {
    get_cmd_resp.data.hswing = 1;
    get_cmd_resp.data.hswing_mv = 0x03;
  } else if (swing_mode == "Move right") {
    get_cmd_resp.data.hswing = 1;
    get_cmd_resp.data.hswing_mv = 0x04;
  } else if (swing_mode == "Fix left") {
    get_cmd_resp.data.hswing_fix = 0x01;
  } else if (swing_mode == "Fix mid left") {
    get_cmd_resp.data.hswing_fix = 0x02;
  } else if (swing_mode == "Fix mid") {
    get_cmd_resp.data.hswing_fix = 0x03;
  } else if (swing_mode == "Fix mid right") {
    get_cmd_resp.data.hswing_fix = 0x04;
  } else if (swing_mode == "Fix right") {
    get_cmd_resp.data.hswing_fix = 0x05;
  } else if (swing_mode != "Off") {
    ESP_LOGW("TCL", "Unknown horizontal louver command: %s", swing_mode.c_str());
    return;
  }

  // Сохраняем выбранное положение, чтобы следующая команда его не отменила.
  memcpy(this->m_get_cmd_resp.raw, get_cmd_resp.raw,
         sizeof(this->m_get_cmd_resp.raw));

  this->build_set_cmd(&get_cmd_resp);

  // Команда уходит немедленно.
  this->ready_to_send_set_cmd_flag = false;
  this->write_array(this->m_set_cmd.raw, sizeof(this->m_set_cmd.raw));
}
// Пример вызова: this->control_horizontal_swing("Fix mid");


void TCLClimate::control(const climate::ClimateCall &call) {
  // Функция: принимает команды Home Assistant и отправляет их только после получения валидного состояния.
  if (!this->has_valid_state_) {
    ESP_LOGW("TCL", "Control command ignored: no valid 65-byte state received yet");
    return;
  }

  get_cmd_resp_t get_cmd_resp = {0};
  memcpy(get_cmd_resp.raw, this->m_get_cmd_resp.raw, sizeof(get_cmd_resp.raw));

  bool should_build_cmd = false;

  if (call.get_mode().has_value()) {
    climate::ClimateMode climate_mode = *call.get_mode();
    ESP_LOGI("TCL", "Received mode control command: %d", static_cast<int>(climate_mode));

    if (climate_mode == climate::CLIMATE_MODE_OFF) {
      get_cmd_resp.data.power = 0x00;
    } else {
      get_cmd_resp.data.power = 0x01;

      switch (climate_mode) {
        case climate::CLIMATE_MODE_COOL:
          get_cmd_resp.data.mode = 0x01;
          break;
        case climate::CLIMATE_MODE_DRY:
          get_cmd_resp.data.mode = 0x03;
          break;
        case climate::CLIMATE_MODE_FAN_ONLY:
          get_cmd_resp.data.mode = 0x02;
          break;
        case climate::CLIMATE_MODE_HEAT:
        case climate::CLIMATE_MODE_HEAT_COOL:
          get_cmd_resp.data.mode = 0x04;
          break;
        case climate::CLIMATE_MODE_AUTO:
          get_cmd_resp.data.mode = 0x05;
          break;
        default:
          break;
      }
    }

    should_build_cmd = true;
  }

  if (call.get_target_temperature().has_value()) {
    float temp = *call.get_target_temperature();
    ESP_LOGI("TCL", "Received temperature control command: %.1f°C", temp);

    if (temp < 16.0f)
      temp = 16.0f;
    if (temp > 31.0f)
      temp = 31.0f;

    get_cmd_resp.data.temp = static_cast<uint8_t>(std::lround(temp)) - 16;
    should_build_cmd = true;
  }

  if (call.get_swing_mode().has_value()) {
    // Функция: преобразует стандартный swing_mode Home Assistant
    // в реальные байты движения TCL.
    climate::ClimateSwingMode swing_mode = *call.get_swing_mode();

    get_cmd_resp.data.hswing = 0;
    get_cmd_resp.data.vswing = 0;
    get_cmd_resp.data.hswing_mv = 0;
    get_cmd_resp.data.vswing_mv = 0;
    get_cmd_resp.data.hswing_fix = 0;
    get_cmd_resp.data.vswing_fix = 0;

    switch (swing_mode) {
      case climate::CLIMATE_SWING_OFF:
        break;

      case climate::CLIMATE_SWING_BOTH:
        get_cmd_resp.data.hswing = 1;
        get_cmd_resp.data.vswing = 1;
        get_cmd_resp.data.hswing_mv = 0x01;
        get_cmd_resp.data.vswing_mv = 0x01;
        break;

      case climate::CLIMATE_SWING_VERTICAL:
        get_cmd_resp.data.vswing = 1;
        get_cmd_resp.data.vswing_mv = 0x01;
        break;

      case climate::CLIMATE_SWING_HORIZONTAL:
        get_cmd_resp.data.hswing = 1;
        get_cmd_resp.data.hswing_mv = 0x01;
        break;
    }

    should_build_cmd = true;
  }

  StringRef custom_fan_mode(call.get_custom_fan_mode());
  if (!custom_fan_mode.empty()) {
    std::string fan_mode(custom_fan_mode.c_str());
    ESP_LOGI("TCL", "Received fan mode control command: %s", fan_mode.c_str());

    get_cmd_resp.data.turbo = 0x00;
    get_cmd_resp.data.mute = 0x00;

    static const std::map<std::string, std::pair<uint8_t, uint8_t>> FAN_MODE_MAP = {
        {"Turbo", {0x03, 0x01}},
        {"Mute", {0x01, 0x01}},
        {"Automatic", {0x00, 0x00}},
        {"1", {0x01, 0x00}},
        {"2", {0x04, 0x00}},
        {"3", {0x02, 0x00}},
        {"4", {0x05, 0x00}},
        {"5", {0x03, 0x00}},
    };

    auto it = FAN_MODE_MAP.find(fan_mode);
    if (it != FAN_MODE_MAP.end()) {
      get_cmd_resp.data.fan = it->second.first;

      if (fan_mode == "Turbo")
        get_cmd_resp.data.turbo = 0x01;
      else if (fan_mode == "Mute")
        get_cmd_resp.data.mute = 0x01;
    }

    should_build_cmd = true;
  }

  if (should_build_cmd) {
    ESP_LOGI("TCL", "Building and scheduling command to AC unit");

    // Сохраняем желаемое состояние локально. Это важно, когда Home Assistant
    // отправляет режим и температуру двумя отдельными командами подряд.
    memcpy(this->m_get_cmd_resp.raw, get_cmd_resp.raw,
           sizeof(this->m_get_cmd_resp.raw));

    this->build_set_cmd(&get_cmd_resp);

    // Команда Home Assistant уходит немедленно.
    // Опрос состояния кондиционера по-прежнему выполняется раз в 3 секунды.
    this->ready_to_send_set_cmd_flag = false;
    this->write_array(this->m_set_cmd.raw, sizeof(this->m_set_cmd.raw));
  }
}
// Пример вызова: ESPHome вызывает control() при изменении climate-сущности в Home Assistant.

climate::ClimateTraits TCLClimate::traits() {
  // Функция: сообщает Home Assistant поддерживаемые режимы и диапазон температур.
  auto traits = climate::ClimateTraits();

  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);

  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_FAN_ONLY,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_AUTO,
  });

  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_BOTH,
      climate::CLIMATE_SWING_VERTICAL,
      climate::CLIMATE_SWING_HORIZONTAL,
  });

  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(31.0f);
  traits.set_visual_target_temperature_step(1.0f);

  return traits;
}
// Пример вызова: ESPHome вызывает traits() автоматически при регистрации сущности.

void TCLClimate::update() {
  // Функция: запрашивает актуальное состояние кондиционера раз в 3 секунды.
  this->write_array(REQ_CMD, sizeof(REQ_CMD));
}
// Пример вызова: ESPHome вызывает update() автоматически каждые 3 секунды.

int TCLClimate::read_data_line(int readch, uint8_t *buffer, int len) {
  // Функция: накапливает один полный TCL-пакет по значению его поля длины.
  static int pos = 0;
  static bool wait_len = false;
  static int skipch = 0;

  if (readch < 0)
    return -1;

  if (readch == 0xBB && skipch == 0 && !wait_len) {
    pos = 0;
    skipch = 3;
    wait_len = true;

    if (pos < len)
      buffer[pos++] = static_cast<uint8_t>(readch);
  } else if (skipch == 0 && wait_len) {
    if (pos < len)
      buffer[pos++] = static_cast<uint8_t>(readch);

    skipch = readch + 1;
    wait_len = false;

    if (skipch + pos > len) {
      ESP_LOGW("TCL", "Incoming frame is larger than receive buffer");
      pos = 0;
      skipch = 0;
      wait_len = false;
    }
  } else if (skipch > 0) {
    if (pos < len)
      buffer[pos++] = static_cast<uint8_t>(readch);

    if (--skipch == 0 && !wait_len)
      return pos;
  }

  return -1;
}
// Пример вызова: int packet_len = this->read_data_line(byte, buffer, 100);

bool TCLClimate::is_valid_xor(uint8_t *buffer, int len) {
  // Функция: проверяет XOR-контрольную сумму принятого пакета.
  if (len < 1)
    return false;

  uint8_t xor_byte = 0;
  for (int i = 0; i < len - 1; i++)
    xor_byte ^= buffer[i];

  return xor_byte == buffer[len - 1];
}
// Пример вызова: bool valid = this->is_valid_xor(buffer, packet_len);

void TCLClimate::print_hex_str(uint8_t *buffer, int len) {
  // Функция: печатает принятый пакет в шестнадцатеричном виде.
  if (len <= 0)
    return;

  char str[MAX_LINE_LENGTH * 3] = {0};
  char *pstr = str;

  for (int i = 0; i < len && (pstr - str) < static_cast<int>(sizeof(str)) - 3; i++)
    pstr += sprintf(pstr, "%02X ", buffer[i]);

  ESP_LOGD("TCL", "Received 65-byte frame: %s", str);
}
// Пример вызова: this->print_hex_str(buffer, packet_len);

void TCLClimate::loop() {
  // Функция: читает UART, принимает валидный 65-байтовый ответ и публикует состояние.
  static uint8_t buffer[MAX_LINE_LENGTH];

  while (this->available()) {
    const int len = this->read_data_line(this->read(), buffer, MAX_LINE_LENGTH);

    if (len <= 0)
      continue;

    if (len != static_cast<int>(sizeof(this->m_get_cmd_resp))) {
      ESP_LOGV("TCL", "Ignored frame length: %d; expected: %u", len,
               static_cast<unsigned>(sizeof(this->m_get_cmd_resp)));
      continue;
    }

    if (buffer[3] != 0x04) {
      ESP_LOGV("TCL", "Ignored frame type: 0x%02X", buffer[3]);
      continue;
    }

    if (!this->is_valid_xor(buffer, len)) {
      ESP_LOGW("TCL", "Invalid XOR for 65-byte frame");
      continue;
    }

    const bool first_valid_state = !this->has_valid_state_;

    memcpy(this->m_get_cmd_resp.raw, buffer, sizeof(this->m_get_cmd_resp.raw));
    this->has_valid_state_ = true;
    this->print_hex_str(buffer, len);

    const uint16_t raw_temperature =
        (static_cast<uint16_t>(buffer[17]) << 8) | static_cast<uint16_t>(buffer[18]);
    const float current_temperature =
        (static_cast<float>(raw_temperature) / 374.0f - 32.0f) / 1.8f;

    this->is_changed = false;

    if (this->m_get_cmd_resp.data.power == 0x00) {
      this->set_mode(climate::CLIMATE_MODE_OFF);
    } else {
      static const std::map<uint8_t, climate::ClimateMode> MODE_MAP = {
          {0x01, climate::CLIMATE_MODE_COOL},
          {0x03, climate::CLIMATE_MODE_DRY},
          {0x02, climate::CLIMATE_MODE_FAN_ONLY},
          {0x04, climate::CLIMATE_MODE_HEAT},
          {0x05, climate::CLIMATE_MODE_AUTO},
      };

      auto mode_it = MODE_MAP.find(this->m_get_cmd_resp.data.mode);
      if (mode_it != MODE_MAP.end())
        this->set_mode(mode_it->second);
    }

    static const std::map<uint8_t, std::string> FAN_MODE_MAP = {
        {0x00, "Automatic"},
        {0x01, "1"},
        {0x04, "2"},
        {0x02, "3"},
        {0x05, "4"},
        {0x03, "5"},
    };

    if (this->m_get_cmd_resp.data.turbo) {
      this->set_custom_fan_mode(StringRef("Turbo"));
    } else if (this->m_get_cmd_resp.data.mute) {
      this->set_custom_fan_mode(StringRef("Mute"));
    } else {
      auto fan_it = FAN_MODE_MAP.find(this->m_get_cmd_resp.data.fan);
      if (fan_it != FAN_MODE_MAP.end()) {
        StringRef current_fan(this->get_custom_fan_mode());

        if (current_fan.empty() || current_fan != fan_it->second)
          this->set_custom_fan_mode(StringRef(fan_it->second.c_str(), fan_it->second.size()));
      }
    }

    if (this->m_get_cmd_resp.data.hswing && this->m_get_cmd_resp.data.vswing)
      this->set_swing_mode(climate::CLIMATE_SWING_BOTH);
    else if (!this->m_get_cmd_resp.data.hswing && !this->m_get_cmd_resp.data.vswing)
      this->set_swing_mode(climate::CLIMATE_SWING_OFF);
    else if (this->m_get_cmd_resp.data.vswing)
      this->set_swing_mode(climate::CLIMATE_SWING_VERTICAL);
    else if (this->m_get_cmd_resp.data.hswing)
      this->set_swing_mode(climate::CLIMATE_SWING_HORIZONTAL);

    if (this->m_get_cmd_resp.data.vswing_mv == 0x01)
      this->set_vswing_pos("Move full");
    else if (this->m_get_cmd_resp.data.vswing_mv == 0x02)
      this->set_vswing_pos("Move upper");
    else if (this->m_get_cmd_resp.data.vswing_mv == 0x03)
      this->set_vswing_pos("Move lower");
    else if (this->m_get_cmd_resp.data.vswing_fix == 0x01)
      this->set_vswing_pos("Fix top");
    else if (this->m_get_cmd_resp.data.vswing_fix == 0x02)
      this->set_vswing_pos("Fix upper");
    else if (this->m_get_cmd_resp.data.vswing_fix == 0x03)
      this->set_vswing_pos("Fix mid");
    else if (this->m_get_cmd_resp.data.vswing_fix == 0x04)
      this->set_vswing_pos("Fix lower");
    else if (this->m_get_cmd_resp.data.vswing_fix == 0x05)
      this->set_vswing_pos("Fix bottom");
    else
      this->set_vswing_pos("Last position");

    if (this->m_get_cmd_resp.data.hswing_mv == 0x01)
      this->set_hswing_pos("Move full");
    else if (this->m_get_cmd_resp.data.hswing_mv == 0x02)
      this->set_hswing_pos("Move left");
    else if (this->m_get_cmd_resp.data.hswing_mv == 0x03)
      this->set_hswing_pos("Move mid");
    else if (this->m_get_cmd_resp.data.hswing_mv == 0x04)
      this->set_hswing_pos("Move right");
    else if (this->m_get_cmd_resp.data.hswing_fix == 0x01)
      this->set_hswing_pos("Fix left");
    else if (this->m_get_cmd_resp.data.hswing_fix == 0x02)
      this->set_hswing_pos("Fix mid left");
    else if (this->m_get_cmd_resp.data.hswing_fix == 0x03)
      this->set_hswing_pos("Fix mid");
    else if (this->m_get_cmd_resp.data.hswing_fix == 0x04)
      this->set_hswing_pos("Fix mid right");
    else if (this->m_get_cmd_resp.data.hswing_fix == 0x05)
      this->set_hswing_pos("Fix right");
    else
      this->set_hswing_pos("Last position");

    this->set_target_temperature(static_cast<float>(this->m_get_cmd_resp.data.temp + 16));
    this->set_current_temperature(current_temperature);

    if (this->is_changed || first_valid_state)
      this->publish_state();
  }
}
// Пример вызова: ESPHome вызывает loop() автоматически в основном цикле.

}  // namespace tcl_climate
}  // namespace esphome
