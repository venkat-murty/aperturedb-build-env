

#include "VDMSClient.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdlib.h>

using namespace VDMS;
using namespace std::chrono;
using json = nlohmann::json;

const json nullValue(nlohmann::json::value_t::null);
const json arrayValue(nlohmann::json::value_t::array);
const json objectValue(nlohmann::json::value_t::object);

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

std::tuple<std::string, unsigned> exec(VDMSClient *client, std::string command,
                                       std::vector<std::string *> blobs) {
  T t("load");
  auto r = client->query(command, std::move(blobs));
  auto m = t.duration();
  return std::make_tuple(std::move(r.json), m);
}

const char *SET = "RandomSet";
const char *LABEL = "label";

const unsigned BATCH = 100000;
const unsigned SIZE = BATCH * 17;
const unsigned DIMS = 128;

std::vector<unsigned> timings;

float r (double d)
{
    if (std::isnan(d)) return 0.0f;
    if (std::isinf(d)) return 0.0f;
    if (d >= std::numeric_limits<float>::max()) return std::numeric_limits<float>::max();
    if (d <= std::numeric_limits<float>::lowest()) return std::numeric_limits<float>::lowest();
    return static_cast<float> (d);
}

unsigned CNT = 0;
void batch_load(VDMSClient *client) {
  std::string ref(sizeof(float) * DIMS, '\0');

  std::vector<std::string> mem(BATCH, ref);
  std::vector<std::string *> blobs(BATCH, nullptr);

  json request(arrayValue);
  for (unsigned i = 0; i < BATCH; ++i) {
    blobs[i] = &mem[i];

    float *v = reinterpret_cast<float *>(mem[i].data());
    for (int d = 0; d < DIMS; ++d) {
      v[d] = r(drand48());
    }

    json add_desc(objectValue);

    add_desc["AddDescriptor"]["set"] = SET;
    add_desc["AddDescriptor"]["label"] = LABEL;

    request.push_back(std::move(add_desc));
  }
  auto [response, milliseconds] =
      exec(client, request.dump(), std::move(blobs));
  timings.push_back(milliseconds);

  auto j = json::parse(response);
  std::map<int, unsigned> status;
  if (j.is_object() && j.find("status") != j.end()) {
    ++status[j.at("status").get<int>()];
  } else {
    for (const auto &c : j) {
      if (c.is_object() && c.find("status") != c.end()) {
        ++status[c.at("status").get<int>()];
      } else {
        for (const auto &x : c) {
          if (x.is_object() && x.find("status") != x.end()) {
            ++status[x.at("status").get<int>()];
          } else {
            for (const auto &y : x) {
              if (y.is_object() && y.find("status") != y.end()) {
                ++status[y.at("status").get<int>()];
              }
            }
          }
        }
      }
    }
  }

  for (auto s : status) {
    std::cout << CNT << " Status = " << s.first << " count = " << s.second << "/"
              << BATCH << " in " << milliseconds << "ms" << std::endl;
  }
}

void load(VDMSClient *client) {
  for (unsigned i = 0; i < SIZE; i += BATCH) {
      CNT = i;
    batch_load(client);
  }
}

int main(int argc, char *argv[]) {
  std::shared_ptr<VDMSClient> conn =
      std::make_shared<VDMSClient>("admin", "admin");
    
  json idx(arrayValue);
  {
    json index(objectValue);
    index["CreateIndex"] = objectValue;
    index["CreateIndex"]["index_type"] = "entity";
    index["CreateIndex"]["class"] = "_Descriptor";
    index["CreateIndex"]["property_key"] = "_create_txn";

    idx.push_back(index);
  }
  conn->query(idx.dump());

    unsigned t = 0;
  while (true) {
    json update_request(arrayValue);
    {
      json cmd(objectValue);
      cmd["UpdateDescriptorSet"] = objectValue;
      cmd["UpdateDescriptorSet"]["with_name"] = SET;
      cmd["UpdateDescriptorSet"]["build_index"] = true;
      update_request.push_back(cmd);
    }
    conn->query(update_request.dump());

    json del_request(arrayValue);
    {
      json add_desc_set(objectValue);
      add_desc_set["DeleteDescriptorSet"] = objectValue;
      add_desc_set["DeleteDescriptorSet"]["with_name"] = SET;
      del_request.push_back(add_desc_set);
    }
    conn->query(del_request.dump());

    json request(arrayValue);
    {
      json add_desc_set(objectValue);
      add_desc_set["AddDescriptorSet"] = objectValue;
      add_desc_set["AddDescriptorSet"]["name"] = SET;
      add_desc_set["AddDescriptorSet"]["dimensions"] = DIMS;

      add_desc_set["AddDescriptorSet"]["engine"] = arrayValue;
      add_desc_set["AddDescriptorSet"]["engine"].push_back ("HNSW");

      add_desc_set["AddDescriptorSet"]["metric"] = arrayValue;
      add_desc_set["AddDescriptorSet"]["metric"].push_back ("L2");
      add_desc_set["AddDescriptorSet"]["metric"].push_back ("IP");
      add_desc_set["AddDescriptorSet"]["metric"].push_back ("CS");

      request.push_back(add_desc_set);
    }
    auto response = json::parse(conn->query(request.dump()).json);
    std::cout << ++t << " " <<  response.dump(4) << std::endl;
  
    load(conn.get());
    conn->query(update_request.dump());
  }

  std::cout << "Batch load size " << BATCH << "/" << SIZE << std::endl;
  for (auto t : timings) {
    std::cout << t << "ms" << std::endl;
  }
  return 0;
}
