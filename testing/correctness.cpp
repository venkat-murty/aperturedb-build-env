

#include <aperturedb/VDMSClient.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <list>
#include <stdlib.h>
#include <thread>

using namespace VDMS;
using namespace std::chrono;
using json = nlohmann::json;

void fill(std::vector<float> &v, uint64_t v) {
  for (unsigned int i = 0; i < 64; ++i, v = v >> 1) {
    if (v & 0x1)
      v[i] = 1.0f;
    else
      v[i] = 0.0f;
  }
}

const json nullValue(nlohmann::json::value_t::null);
const json arrayValue(nlohmann::json::value_t::array);
const json objectValue(nlohmann::json::value_t::object);

const char *SET = "RandomSet";
const unsigned DIMS = 64;

struct T {
  const char *name;
  decltype(high_resolution_clock::now()) start;

  T(const char *s) {
    name = s;
    start = high_resolution_clock::now();
  }
  unsigned duration() const {
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    return (duration.count() / 1000);
  }
};

std::map<int, unsigned> status_and_count(const json &j) {
  std::map<int, unsigned> map;
  if (j.is_object() && j.find("status") != j.end()) {
    ++map[j.at("status").get<int>()];
  } else {
    for (const auto &c : j) {
      if (c.is_object() && c.find("status") != c.end()) {
        ++map[c.at("status").get<int>()];
      } else {
        for (const auto &x : c) {
          if (x.is_object() && x.find("status") != x.end()) {
            ++map[x.at("status").get<int>()];
          } else {
            for (const auto &y : x) {
              if (y.is_object() && y.find("status") != y.end()) {
                ++map[y.at("status").get<int>()];
              }
            }
          }
        }
      }
    }
  }
  return map;
}

void exec(VDMSClient *client, std::string name, std::string command,
          std::vector<std::string *> blobs) {
  auto r = client->query(command, std::move(blobs));
  auto m = t.duration();

  auto j = json::parse(r.json);
  std::map<int, unsigned> status = status_and_count(j);

  for (auto s : status) {
    std::cerr << name << " - Status = " << s.first << " commands = " << s.second
              << " in " << m << "ms" << std::endl;
  }
}

const std::unordered_set<uint64_t> &pow2() {
  static std::atomic<std::unordered_set<uint64_t> *> map{nullptr};

  if (map == nullptr) {
    std::unordered_set<uint64_t> *v = new std::unordered_set<uint64_t>{};
    v->insert(0);
    uint64_t x = 0x1;
    while (!v->contains(x)) {
      v->insert(x);
      x = x << 1;
    }
    map = v;
  }
  return *(map);
}

void remove(VDMSClient *client, uint64_t x, unsigned count) {
  std::string ref(sizeof(float) * DIMS, '\0');

  std::vector<std::string> mem(count, ref);
  std::vector<std::string *> blobs;

  const auto &p2 = pow2();

  json request(arrayValue);

  json del_desc(objectValue);
  del_desc["DeleteDescriptor"]["set"] = SET;
  del_desc["DeleteDescriptor"]["constraints"]["idx"] = arrayValue;
  del_desc["DeleteDescriptor"]["constraints"]["idx"].push_back(">=");
  del_desc["DeleteDescriptor"]["constraints"]["idx"].push_back(x);
  del_desc["DeleteDescriptor"]["constraints"]["idx"].push_back("<");
  del_desc["DeleteDescriptor"]["constraints"]["idx"].push_back(x + count);

  request.push_back(del_desc);

  for (uint64_t i = 0; i < count; ++i) {
    if (p2.contains(i + x)) {
      blobs.push_back(&mem[i]);
      fill(*(blobs.back()), i + x);

      json add_desc(objectValue);

      add_desc["AddDescriptor"]["set"] = SET;
      add_desc["AddDescriptor"]["label"] = LABEL;
      add_desc["AddDescriptor"]["idx"] = i + x;
      request.push_back(add_desc);
    }
  }
  exec(client, std::string{"Delete Descriptors"}, request.dump(),
       std::move(blobs));
}

void add(VDMSClient *client, uint64_t x, unsigned count) {
  std::string ref(sizeof(float) * DIMS, '\0');

  std::vector<std::string> mem(count, ref);
  std::vector<std::string *> blobs{};

  const auto &p2 = pow2();

  json request(arrayValue);
  for (unsigned i = 0; i < count; ++i) {
    if (!p2.contains(i + x)) {
      blobs.push_back(&mem[i]);
      fill(*(blobs.back()), i + x);

      json add_desc(objectValue);

      add_desc["AddDescriptor"]["set"] = SET;
      add_desc["AddDescriptor"]["label"] = LABEL;
      add_desc["AddDescriptor"]["idx"] = i + x;

      request.push_back(std::move(add_desc));
    }
  }
  exec(client, std::string{"Add Descriptors"}, request.dump(),
       std::move(blobs));
}

void init_set(VDMSClient *client) {
  json request(arrayValue);
  {
    json add_desc_set(objectValue);
    add_desc_set["AddDescriptorSet"] = objectValue;
    add_desc_set["AddDescriptorSet"]["name"] = SET;
    add_desc_set["AddDescriptorSet"]["dimensions"] = DIMS;
    add_desc_set["AddDescriptorSet"]["segment_size"] = 4096;
    add_desc_set["AddDescriptorSet"]["max_segments"] = 5;

    request.push_back(add_desc_set);

    json index(objectValue);
    index["CreateIndex"] = objectValue;
    index["CreateIndex"]["index_type"] = "entity";
    index["CreateIndex"]["class"] = "_Descriptor";
    index["CreateIndex"]["property_key"] = "_create_txn";

    request.push_back(index);
  }
  exec(client, std::string{"Init Descriptor Set"}, request.dump(), {});
}

void init_vectors(VDMSClient *client) {

  std::string ref(sizeof(float) * DIMS, '\0');

  std::vector<std::string> mem;
  std::vector<std::string *> blobs{};

  const auto &p2 = pow2();

  json request(arrayValue);
  for (auto i : p2) {
    mem.push_back(ref);
    blobs.push_back(&mem.back());
    fill(*(blobs.back()), i);

    json add_desc(objectValue);

    add_desc["AddDescriptor"]["set"] = SET;
    add_desc["AddDescriptor"]["label"] = LABEL;
    add_desc["AddDescriptor"]["idx"] = i;

    request.push_back(std::move(add_desc));
  }
  exec(client, std::string{"Init Descriptors"}, request.dump(),
       std::move(blobs));
}

void init (VDMSClient *client) {
  init_set (client);
  init_vectors (client);
}

void query (VMDSClient* client) {
  json request (arrayValue);

}

int main(int argc, char *argv[]) {
  std::shared_ptr<VDMSClient> conn =
      std::make_shared<VDMSClient>("admin", "admin");

  init (client);
}

