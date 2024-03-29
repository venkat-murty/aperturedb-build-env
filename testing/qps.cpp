#include "VDMSClient.h"
#include "comm/Exception.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
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
    return (duration.count() / 1000);
  }
};

std::vector<std::pair<unsigned, std::string>> filter;

unsigned rand(unsigned max) {
  std::random_device rd; // Only used once to initialise (seed) engine
  std::mt19937 rng(
      rd()); // Random-number engine used (Mersenne-Twister in this case)
  std::uniform_int_distribution<int> uni(0, max); // Guaranteed unbiased

  return uni(rng);
}

void batch_load(unsigned elements, VDMSClient *client, unsigned start,
                std::string name, unsigned BATCH) {
  json request(arrayValue);
  for (unsigned i = 0; i < BATCH; ++i) {
    json add_entity(objectValue);

    add_entity["AddEntity"] = objectValue;
    add_entity["AddEntity"]["class"] = "Person";
    add_entity["AddEntity"]["properties"] = objectValue;
    add_entity["AddEntity"]["properties"]["name"] = name;
    add_entity["AddEntity"]["properties"]["id"] = i + start;
    add_entity["AddEntity"]["properties"]["gender"] = "F";
    add_entity["AddEntity"]["properties"]["email"] = "jane.doe@xyz.com";

    request.push_back(std::move(add_entity));

    if (filter.size() < 10000) {
      filter.emplace_back(i + start, name);
    } else {
      int idx = rand(elements + i - 1);
      if (idx < filter.size()) {
        filter[idx] = std::make_pair(i + start, name);
      }
    }
  }
 // client->query(request.dump());
}

unsigned load(VDMSClient *client, int elements, bool unique_ids) {
  static unsigned total = 0;
  static char ch = 'a';

  unsigned BATCH = 100000;
  while (elements > 1) {
    batch_load(total, client, unique_ids ? total : 0, std::string{ch}, BATCH);
    total += BATCH;
    elements -= BATCH;
  }
  batch_load(total, client, unique_ids ? total : 0, std::string{ch}, 1);
  ++ch;

  return total;
}

void thread_fn(VDMSClient *conn, const std::vector<std::string> &requests,
               unsigned queries) {
  unsigned idx = rand(requests.size());
  for (unsigned i = 0; i < queries; ++i) {
    conn->query(requests[(i + idx) % requests.size()]);
  }
}

#define DB_HOST "yfcc100m.datasets.develop-cloud.aperturedata.io"
// (utils/src/comm/ConnClient.cc:89: comm::Exception={num=8,name='ConnectionError'})

#define DB_PORT 55555
// DB_USER="admin"
// DB_PASS="admin"

int run() {
  VDMSClientConfig cfg;
  cfg.addr = DB_HOST;
  cfg.port = DB_PORT;
  std::shared_ptr<VDMSClient> conn =
      std::make_shared<VDMSClient>("admin", "admin", cfg);

  /// yfcc100m.datasets.develop-cloud.aperturedata.io

  json idx(arrayValue);
  {
    json index(objectValue);
    index["CreateIndex"] = objectValue;
    index["CreateIndex"]["index_type"] = "entity";
    index["CreateIndex"]["class"] = "Person";
    index["CreateIndex"]["property_key"] = "id";

    idx.push_back(index);
  }
  conn->query(idx.dump());

  const unsigned min_threads = 1;
  const unsigned max_threads = 32; // 80

  const unsigned rounds = 1;

  // Elements added in each round.
  const int elements = 1000000;
  const bool unique_ids = true;
  const unsigned queries = 10000000;
  const unsigned times = 2; // 3 Average of test runs.

  std::vector<std::unique_ptr<VDMSClient>> connections;
  connections.reserve(max_threads + 1);
  for (unsigned i = 0; i <= max_threads; ++i) {
    connections.emplace_back(
        std::make_unique<VDMSClient>("admin", "admin", cfg));
  }

  for (unsigned r = 0; r < rounds; ++r) {
    unsigned entities = load(conn.get(), elements, unique_ids);

    // Construct requests.
    std::vector<std::string> requests;
    for (auto const &e : filter) {
      json request(arrayValue);
      json find(objectValue);
      find["FindEntity"] = objectValue;
      find["FindEntity"]["with_class"] = "Person";
      find["FindEntity"]["constraints"] = objectValue;
      find["FindEntity"]["constraints"]["id"] = arrayValue;
      find["FindEntity"]["constraints"]["id"].push_back("==");
      find["FindEntity"]["constraints"]["id"].push_back(e.first);
      if (!unique_ids) {
        find["FindEntity"]["constraints"]["name"] = arrayValue;
        find["FindEntity"]["constraints"]["name"].push_back("==");
        find["FindEntity"]["constraints"]["name"].push_back(e.second);
      }
      find["FindEntity"]["results"] = objectValue;
      find["FindEntity"]["results"]["all_properties"] = true;
      request.push_back(find);
      requests.emplace_back(request.dump());
    }
    std::cerr << "Loaded " << entities << std::endl;

    for (unsigned th = max_threads; th <= max_threads; th *= 2) {
      unsigned q = queries / th;

      std::vector<unsigned> durations;
      for (unsigned t = 0; t < times; ++t) {
        std::vector<std::thread> threads;
        threads.reserve(th);

        T time("run");
        for (unsigned x = 0; x < th; ++x) {
          threads.emplace_back(thread_fn, connections[x].get(), requests, q);
        }
        std::for_each(threads.begin(), threads.end(),
                      [](auto &a) { a.join(); });

        durations.push_back(time.duration());
      }
      unsigned min = std::accumulate(
          durations.begin(), durations.end(),
          std::numeric_limits<unsigned>::max(),
          [](unsigned total, unsigned elem) { return std::min(total, elem); });

      std::cerr << "Entities = " << entities << " threads = " << th
                << " duration = " << min << "ms for " << (q * th) << " queries."
                << std::endl;
    }
  }
  return 0;
}

int main() {
  try {
    return run();
  } catch (comm::Exception e) {
    std::cerr << e << std::endl;
  }
  return -1;
}
