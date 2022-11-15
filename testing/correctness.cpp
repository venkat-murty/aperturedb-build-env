

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
#include <unordered_set>

using namespace VDMS;
using namespace std::chrono;
using json = nlohmann::json;

const json nullValue(nlohmann::json::value_t::null);
const json arrayValue(nlohmann::json::value_t::array);
const json objectValue(nlohmann::json::value_t::object);

const char *SET = "RandomSet";
const char *LABEL = "RandomSet";
constexpr unsigned DIMS = 64;
constexpr unsigned VECTORS = 100000;

static std::string REF(sizeof(float) * DIMS, '\0');

struct T {
  decltype(high_resolution_clock::now()) start;

  T() { start = high_resolution_clock::now(); }
  unsigned duration() const {
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    return (duration.count() / 1000);
  }
};

std::string descriptor(uint64_t v) {
  std::string result{REF};
  float *desc = reinterpret_cast<float *>(result.data());
  for (unsigned int i = 0; i < DIMS; ++i, v = v >> 1) {
    if (v & 0x1)
      desc[i] = 1.0f;
    else
      desc[i] = 0.0f;
  }
  return result;
}

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

json add_desc(uint64_t x) {
  json j(objectValue);

  auto cmd = "AddDescriptor";
  j[cmd]["set"] = SET;
  j[cmd]["label"] = LABEL;
  j[cmd]["properties"]["idx"] = x;
  j[cmd]["if_not_found"]["idx"] = arrayValue;
  j[cmd]["if_not_found"]["idx"].push_back("==");
  j[cmd]["if_not_found"]["idx"].push_back(x);

  return j;
}

json exec(VDMSClient *client, std::string name, std::string command,
          std::vector<std::string> blobs) {

  std::vector<std::string *> param;
  std::transform(blobs.begin(), blobs.end(), std::back_inserter(param),
                 [](std::string &str) { return &str; });

  T t;
  auto r = client->query(command, std::move(param));
  auto m = t.duration();

  auto j = json::parse(r.json);
  std::map<int, unsigned> status = status_and_count(j);

  if (status.size() > 1 || status.find(0) == status.end()) {
    std::cerr << name << std::endl
              << json::parse(command).dump(2) << std::endl
              << j.dump(2) << std::endl;
  } else {
    for (auto s : status) {
      // std::cerr << name << " - commands = " << s.second << " in " << m <<
      // "ms" << std::endl;
    }
  }
  return j;
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

  std::vector<std::string> blobs;
  for (uint64_t i = 0; i < count; ++i) {
    if (p2.contains(i + x)) {
      blobs.emplace_back(descriptor(i + x));
      request.push_back(add_desc(i+x));
    }
  }
  exec(client, std::string{"Delete Descriptors"}, request.dump(),
       std::move(blobs));
}

void remove_from_low(VDMSClient *client) {
  unsigned count = 1023;
  unsigned total = 0;

  T t;
  while (total < VECTORS) {
    remove(client, total, std::min(count, VECTORS - total));
    total += count;
  }
  std::cerr << "Remove all from low index in " << t.duration() << "ms" << std::endl;
}

void remove_from_high(VDMSClient *client) {
  unsigned count = 1023;
  unsigned total = 0;

  T t;
  while (total < VECTORS) {
    unsigned cnt = std::min(count, VECTORS - total);
    remove(client, VECTORS - total - cnt, cnt);
    total += count;
  }
  std::cerr << "Remove all from high index in " << t.duration() << "ms" << std::endl;
}

void add(VDMSClient *client, uint64_t x, unsigned count) {
  std::vector<std::string> blobs{};

  const auto &p2 = pow2();

  json request(arrayValue);
  for (unsigned i = 0; i < count; ++i) {
    if (!p2.contains(i + x)) {
      blobs.emplace_back(descriptor(i + x));
      request.push_back(add_desc(i+x));
    }
  }
  exec(client, std::string{"Add Descriptors"}, request.dump(),
       std::move(blobs));
}

void add_all(VDMSClient *client) {
  unsigned count = 10;
  unsigned total = 0;

  T t;
  while (total < VECTORS) {
    add(client, total, std::min(count, VECTORS - total));
    std::this_thread::sleep_for (std::chrono::seconds(2));
    total += count;
    count *= 2;
  }
  std::cerr << "Add all in " << t.duration() << "ms" << std::endl;
}

void del_set(VDMSClient *client) {
  json request(arrayValue);
  {
    json del_desc_set(objectValue);
    del_desc_set["DeleteDescriptorSet"] = objectValue;
    del_desc_set["DeleteDescriptorSet"]["with_name"] = SET;

    request.push_back(del_desc_set);

    json index(objectValue);
    index["RemoveIndex"] = objectValue;
    index["RemoveIndex"]["index_type"] = "entity";
    index["RemoveIndex"]["class"] = "_Descriptor";
    index["RemoveIndex"]["property_key"] = "_create_txn";

    request.push_back(index);

    index["RemoveIndex"] = objectValue;
    index["RemoveIndex"]["index_type"] = "entity";
    index["RemoveIndex"]["class"] = "_Descriptor";
    index["RemoveIndex"]["property_key"] = "idx";

    request.push_back(index);
  }
  exec(client, std::string{"Delete DescriptorSet / Index"}, request.dump(), {});
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

    index["CreateIndex"]["index_type"] = "entity";
    index["CreateIndex"]["class"] = "_Descriptor";
    index["CreateIndex"]["property_key"] = "idx";

    request.push_back(index);
  }
  exec(client, std::string{"Init Descriptor Set"}, request.dump(), {});
}

void init_vectors(VDMSClient *client) {

  std::vector<std::string> blobs;

  const auto &p2 = pow2();

  json request(arrayValue);
  for (auto i : p2) {
    blobs.emplace_back(descriptor(i));
    request.push_back(add_desc(i));
  }
  exec(client, std::string{"Init Descriptors"}, request.dump(),
       std::move(blobs));
}

void init(VDMSClient *client) {
  del_set(client);
  init_set(client);
  init_vectors(client);
}

bool query(VDMSClient *client) {
  json request(arrayValue);

  json q(objectValue);
  auto cmd = "FindDescriptor";

  q[cmd]["set"] = SET;
  q[cmd]["k_neighbors"] = 10;
  q[cmd]["knn_first"] = false;
  q[cmd]["results"]["list"] = arrayValue;
  q[cmd]["results"]["list"].push_back("idx");

  request.push_back(q);

  std::vector blobs{1, descriptor(0)};
  auto response =
      exec(client, std::string{"Query"}, request.dump(), std::move(blobs));

  std::vector<uint64_t> result;
  if (response.is_array()) {
    for (auto &x : response[0]["FindDescriptor"]["entities"]) {
      result.push_back(x["idx"]);
    }
  }
  unsigned invalid = 0;
  for (auto id : result) {
    if (pow2().contains(id))
      continue;
    ++invalid;
  }
  return result.size() == 10 && invalid == 0;
}

void exercise(VDMSClient *client) {
  unsigned faults = 0;
  add_all(client);
  if (!query(client))
    faults++;
  remove_from_low(client);
  if (!query(client))
    faults++;
  add_all(client);
  if (!query(client))
    faults++;
  remove_from_high(client);
  if (!query(client))
    faults++;

  std::cerr << "One exercise with " << faults << " faults" << std::endl;
}

void q_thread ()
{
  std::cerr << "Q thread" << std::endl;

  std::shared_ptr<VDMSClient> conn =
      std::make_shared<VDMSClient>("admin", "admin");
  auto client = conn.get();

  unsigned faults = 0;
  unsigned count = 0;
  while (true) {
    if (!query(client)) faults++;
    count++;

    if (count == 1024) {
      std::cerr << "Count " << count << " with faults " << faults << std::endl;
      count = 0;
      faults = 0;
    }
  }
}

int main(int argc, char *argv[]) {
  std::shared_ptr<VDMSClient> conn =
      std::make_shared<VDMSClient>("admin", "admin");

  auto client = conn.get();
  init(client);
  query(client);

  std::thread th(q_thread);

  while (true)
    exercise(client);

  th.join();
}
