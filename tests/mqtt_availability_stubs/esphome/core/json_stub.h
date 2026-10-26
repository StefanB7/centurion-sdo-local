#pragma once

// Minimal stand-in for the few ArduinoJson 7 APIs used by
// components/centurion_ble/mqtt_availability.h. Test-only; not a JSON library.

#include <map>
#include <string>
#include <vector>

class JsonObject;
class JsonArray;

class JsonValue {
 public:
  enum Type { NUL, STRING, ARRAY, OBJECT };
  Type type{NUL};
  std::string string;
  std::vector<JsonValue> items;
  std::map<std::string, JsonValue> members;
};

class JsonArray {
 public:
  explicit JsonArray(JsonValue *value = nullptr) : value_(value) {}
  bool is_null() const { return this->value_ == nullptr || this->value_->type != JsonValue::ARRAY; }
  size_t size() const { return this->is_null() ? 0 : this->value_->items.size(); }
  JsonValue *at(size_t index) const { return this->is_null() ? nullptr : &this->value_->items[index]; }

  template<typename T> T add();

 private:
  JsonValue *value_;
};

class JsonObject {
 public:
  explicit JsonObject(JsonValue *value = nullptr) : value_(value) {}

  class Member {
   public:
    explicit Member(JsonValue *value) : value_(value) {}

    template<typename T> T to();

    // A null pointer serialises as JSON null, as in ArduinoJson.
    Member &operator=(const char *value) {
      if (value == nullptr) {
        this->value_->type = JsonValue::NUL;
        this->value_->string.clear();
        return *this;
      }
      this->value_->type = JsonValue::STRING;
      this->value_->string = value;
      return *this;
    }
    Member &operator=(const std::string &value) {
      this->value_->type = JsonValue::STRING;
      this->value_->string = value;
      return *this;
    }

    JsonValue *value_;
  };

  Member operator[](const char *key) { return Member(&this->value_->members[std::string(key)]); }

  JsonValue *find(const std::string &key) const {
    auto it = this->value_->members.find(key);
    return it == this->value_->members.end() ? nullptr : const_cast<JsonValue *>(&it->second);
  }

 private:
  JsonValue *value_;
};

template<> inline JsonArray JsonObject::Member::to<JsonArray>() {
  this->value_->type = JsonValue::ARRAY;
  return JsonArray(this->value_);
}

template<> inline JsonObject JsonArray::add<JsonObject>() {
  this->value_->items.emplace_back();
  this->value_->items.back().type = JsonValue::OBJECT;
  return JsonObject(&this->value_->items.back());
}
