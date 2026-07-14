#include "GameAction.hpp"

static void writeUint32(std::vector<uint8_t> &data, uint32_t value) {
  data.push_back((uint8_t) (value & 0xFF));
  data.push_back((uint8_t) ((value >> 8) & 0xFF));
  data.push_back((uint8_t) ((value >> 16) & 0xFF));
  data.push_back((uint8_t) ((value >> 24) & 0xFF));
}

static bool readUint32(const std::vector<uint8_t> &data, size_t &offset, uint32_t &value) {
  if (offset + 4 > data.size()) {
    return false;
  }
  value = (uint32_t) data[offset] | ((uint32_t) data[offset + 1] << 8) | ((uint32_t) data[offset + 2] << 16) | ((uint32_t) data[offset + 3] << 24);
  offset += 4;
  return true;
}

std::vector<uint8_t> serializeGameAction(const GameAction &action) {
  std::vector<uint8_t> data;
  writeUint32(data, (uint32_t) action.type);
  writeUint32(data, (uint32_t) action.x);
  writeUint32(data, (uint32_t) action.y);
  writeUint32(data, (uint32_t) action.value);
  writeUint32(data, (uint32_t) action.text.size());
  for (char character : action.text) {
    data.push_back((uint8_t) character);
  }
  return data;
}

bool deserializeGameAction(const std::vector<uint8_t> &data, GameAction &action) {
  size_t offset = 0;
  uint32_t type = 0;
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t value = 0;
  uint32_t text_size = 0;
  if (!readUint32(data, offset, type) || !readUint32(data, offset, x) ||
      !readUint32(data, offset, y) || !readUint32(data, offset, value) ||
      !readUint32(data, offset, text_size)) {
    return false;
  }
  if (offset + text_size > data.size()) {
    return false;
  }
  action.type = (GameActionType) type;
  action.x = (int32_t) x;
  action.y = (int32_t) y;
  action.value = (int32_t) value;
  action.text = std::string((const char *) data.data() + offset, text_size);
  return true;
}
