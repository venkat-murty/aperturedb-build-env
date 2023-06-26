

#include "VDMSClient.h"
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
    return duration.count();
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
const unsigned DIMS = 128;
const unsigned TOTAL = 1024; // 100000;
const char *FIND = "FindDescriptorBatch";

std::array<unsigned, 3> THREADS{2, 4, 8};
std::array<unsigned, 1> COMMANDS{8};
std::array<unsigned, 5> DESCRIPTORS{16, 32, 64};

std::atomic<long long> count{0};
std::atomic<long long> mseconds{0};

void thread_fn(std::shared_ptr<VDMSClient> client, unsigned commands,
               unsigned descriptors, std::vector<std::string> &qvectors) {

  const std::string ref(descriptors * DIMS * sizeof(float), '\0');
  std::vector<std::string> mem(commands, ref);
  std::vector<std::string *> blobs(commands, nullptr);

  while (count < TOTAL) {

    json request(arrayValue);
    for (unsigned i = 0; i < commands; ++i) {
      blobs[i] = &mem[i];

      auto m = mem[i].data();
      for (unsigned j = 0; j < descriptors; ++j) {
        const auto &vec = qvectors.at(
            count.fetch_add(1, std::memory_order_relaxed) % qvectors.size());
        std::memcpy(m, vec.data(), sizeof(float) * DIMS);
        m += sizeof(float) * DIMS;
      }

      json find_desc(objectValue);
      find_desc[FIND]["set"] = SET;
      find_desc[FIND]["n_descriptors"] = descriptors;
      find_desc[FIND]["k_neighbors"] = 1;
      find_desc[FIND]["knn_first"] = false;

      request.push_back(std::move(find_desc));
    }
    auto [response, ms] = exec(client.get(), request.dump(), blobs);
    count += commands * descriptors;
    mseconds += ms;

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
      if (s.first != 0) {
        std::cerr << "Status = " << s.first << " count = " << s.second << "/"
                  << commands << " in " << ms << "ms" << std::endl;
      }
    }
  }
}

float r(double d) {
  if (std::isnan(d))
    return 0.0f;
  if (std::isinf(d))
    return 0.0f;
  if (d >= std::numeric_limits<float>::max())
    return std::numeric_limits<float>::max();
  if (d <= std::numeric_limits<float>::lowest())
    return std::numeric_limits<float>::lowest();
  return static_cast<float>(d);
}

int main(int argc, char *argv[]) {

  std::string ref(sizeof(float) * DIMS, '\0');
  std::vector<std::string> mem(1024 * 1024, ref);
  for (unsigned i = 0; i < mem.size(); ++i) {
    float *v = reinterpret_cast<float *>(mem[i].data());
    for (int d = 0; d < DIMS; ++d) {
      v[d] = r(drand48());
    }
  }

  std::vector<std::shared_ptr<VDMSClient>> connections;
  for (auto threads : THREADS) {
    while (connections.size() < threads) {
      connections.push_back(std::make_shared<VDMSClient>("admin", "admin"));
    }

    for (auto commands : COMMANDS) {
      for (auto descriptors : DESCRIPTORS) {
        std::vector<unsigned> results(connections.size(), 0);
        {
          count = 0;
          mseconds = 0;
          std::list<std::thread> thds;
          for (int i = 0; i < connections.size(); ++i) {
            auto conn = connections[i];
            thds.emplace_back(thread_fn, conn, commands, descriptors,
                                 std::ref(mem));
          }
          std::for_each(thds.begin(), thds.end(),
                        [](auto &th) { th.join(); });
        }

        std::cout << " Threads= " << threads
                  << " Commands_per_request= " << commands
                  << " Descriptors_per_command= " << descriptors
                  << " Count= " << count
                  << " Time_in_milliseconds= " << (mseconds / 1000)
                  << " Queries_per_seconds= "
                  << (mseconds > 0 ? count * 1000.0 * 1000.0 / (1.0 * mseconds) : 0.0) << " "
                  << FIND << std::endl;
      }
    }
  }
}
