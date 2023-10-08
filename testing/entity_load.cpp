#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "VDMSClient.h"
#include "comm/Exception.h"

using namespace std::chrono;
using json = nlohmann::json;
using namespace VDMS;

const json nullValue(nlohmann::json::value_t::null);
const json arrayValue(nlohmann::json::value_t::array);
const json objectValue(nlohmann::json::value_t::object);

void read_data_file(std::function<void(std::vector<std::string>)> process) {

  using Row = std::vector<std::string>;
  std::string line; /* string to hold each line read from file  */

  Row csv;
  std::ifstream f("/mnt/nvme1/data/images.adb.csv");

  const std::regex re(",");

  // The first line.
  std::getline(f, line);
  // std::cout << line << std::endl;
  // s3_url,id,hash,user_nsid,user_nickname,date_taken,date_uploaded,title,description,longitude,latitude,accuracy,license,constraint_id,format

  while (std::getline(f, line)) { /* read each line into line */
    csv.clear();
    std::copy(std::sregex_token_iterator(line.begin(), line.end(), re, -1),
              std::sregex_token_iterator(), std::back_inserter(csv));
    if (csv.size() == 15)
      process(std::move(csv));
  }
}

#include <atomic>
#include <semaphore>

struct Queue {
  constexpr static size_t kSize = 1024 * 1024;

  std::vector<std::string> requests{kSize * 2};

  std::atomic_uint32_t head = 0;
  uint32_t tail = 0;

  std::counting_semaphore<> acquire{kSize};
  std::counting_semaphore<> release{0};

  void push(std::string request) {
    acquire.acquire();
    requests[tail % requests.size()] = std::move(request);
    ++tail;
    release.release();
  }

  std::string pop() {
    release.acquire();
    auto p = head.fetch_add(1, std::memory_order_relaxed);
    auto result = std::move(requests[p % requests.size()]);
    acquire.release();
    return result;
  }

  size_t size() const { return tail - head.load(std::memory_order_relaxed); }
};

bool value(const std::vector<std::string> &fields, int idx) {
  return !fields.at(idx).empty();
}

double to_number(std::string const &s) {
  try {
    double v = std::stod(s);
    if (v > std::numeric_limits<uint32_t>::lowest() &&
        v < std::numeric_limits<uint32_t>::max())
      return v;
  } catch (...) {
  }
  return 0.0;
}

void read_file(Queue &q, int batch) {
  json request(arrayValue);

  auto callback = [&request, &q, batch](std::vector<std::string> fields) {
    if (!std::accumulate(fields.begin(), fields.end(), true,
                         [](bool result, const std::string &f) {
                           return result && f.size() < 256;
                         }))
      return;

    try {

      json add_entity(objectValue);

      add_entity["AddEntity"] = objectValue;
      add_entity["AddEntity"]["class"] = "Row";
      add_entity["AddEntity"]["properties"] = objectValue;

      unsigned f = 0;
      add_entity["AddEntity"]["properties"]["cmd"] = request.size() + 1;

      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["s3_url"] = fields[f - 1];
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["id"] = to_number(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["hash"] = fields[f - 1];
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["user_nsid"] = fields[f - 1];
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["user_nickname"] = fields[f - 1];
      if (value(fields, f++)) {
        add_entity["AddEntity"]["properties"]["date_taken"] = objectValue;
        add_entity["AddEntity"]["properties"]["date_taken"]["_date"] =
            "Sat Oct 7 17:59:24 PDT 1946";
      }
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["date_uploaded"] =
            to_number(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["title"] = fields[f - 1];
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["longitude"] =
            to_number(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["latitude"] =
            to_number(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["accuracy"] =
            to_number(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["license"] = fields[f - 1];

      request.push_back(std::move(add_entity));

    } catch (...) {
    }

    if (request.size() >= batch) {
      q.push(request.dump());
      request = arrayValue;
    }
  };

  read_data_file(callback);
}

#define DB_HOST "yfcc100m.datasets.develop-cloud.aperturedata.io"
// (utils/src/comm/ConnClient.cc:89:
// comm::Exception={num=8,name='ConnectionError'})

#define DB_PORT 55555
// DB_USER="admin"
// DB_PASS="admin"

std::shared_ptr<VDMSClient> connection() {
  std::cerr << "New connection " << std::endl;
  VDMSClientConfig cfg;
  cfg.addr = DB_HOST;
  cfg.port = DB_PORT;
  return std::make_shared<VDMSClient>("admin", "admin", cfg);
}

std::atomic_uint64_t inserted = 0;
std::atomic_uint64_t requests = 0;
std::atomic_uint64_t successful = 0;

bool exec(Queue *queue, VDMSClient *client) {
  std::string req = queue->pop();
  auto response = client->query(req);
  requests.fetch_add(1, std::memory_order_relaxed);
  auto j = json::parse(response.json);

  if (j.is_object() && j.find("status") != j.end()) {
    if (j.find("info") != j.end() &&
        j.at("info").get<std::string>() == "Not Authenticated!") {
      std::cerr << "Not Authenticated" << std::endl;
      return false;
    }

    std::cerr << json::parse(req).dump(4) << std::endl;
    std::cerr << j.dump() << std::endl;

    abort();
    return false;
  } else {
    successful.fetch_add(1, std::memory_order_relaxed);
    uint64_t zeros = 0;
    for (const auto &c : j) {
      if (c.is_object() && c.find("status") != c.end()) {
        if (c.at("status").get<int>() == 0)
          ++zeros;
      } else {
        for (const auto &x : c) {
          if (x.is_object() && x.find("status") != x.end()) {
            if (x.at("status").get<int>() == 0)
              ++zeros;
          } else {
            for (const auto &y : x) {
              if (y.is_object() && y.find("status") != y.end()) {
                if (y.at("status").get<int>() == 0)
                  ++zeros;
              }
            }
          }
        }
      }
    }
    inserted.fetch_add(zeros, std::memory_order_relaxed);
  }
  return true;
}

void exec_th(Queue *queue) {
  while (true) {
    try {
      auto conn = connection();
      while (exec(queue, conn.get())) {
      }
    } catch (comm::Exception e) {
      std::cerr << e << std::endl;
    } catch (...) {
      std::cerr << "EXCEPTION " << std::endl;
    }
    std::this_thread::sleep_for(1000ms);
  }
}

std::string now() {
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);

  std::ostringstream timestamp;
  timestamp << std::put_time(&tm, "%m/%d %H:%M:%S");
  return std::move(timestamp.str());
}

void print_th() {
  using namespace std::chrono_literals;
  {
    auto conn = connection();

    json idx(arrayValue);
    {
      json index(objectValue);
      index["CreateIndex"] = objectValue;
      index["CreateIndex"]["index_type"] = "entity";
      index["CreateIndex"]["class"] = "Row";
      index["CreateIndex"]["property_key"] = "id";

      idx.push_back(index);
    }
    conn->query(idx.dump());
  }

  auto start = high_resolution_clock::now();
  while (true) {

    std::this_thread::sleep_for(10000ms);

    auto tick = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(tick - start);
    auto seconds = (duration.count() / 1000 / 1000);

    std::cout << "Metrics: (" << now() << ") Tick = " << seconds
              << " Requests = " << requests.load(std::memory_order_relaxed)
              << " Successful = " << successful.load(std::memory_order_relaxed)
              << " Inserted = " << inserted.load(std::memory_order_relaxed)
              << std::endl;
  }
}

void read_fn(Queue *queue, unsigned batch_size) {
  read_file(*queue, batch_size);
}

unsigned const BATCH = 200;
unsigned const THREADS = 80;

int main() {
  Queue queue;

  std::thread q(read_fn, &queue, BATCH);
  std::thread p(print_th);

  std::this_thread::sleep_for(10000ms);

  const unsigned threads = THREADS;
  std::vector<std::thread> th;
  th.reserve(threads);

  for (unsigned x = 0; x < threads; ++x) {
    th.emplace_back(exec_th, &queue);
  }
  std::for_each(th.begin(), th.end(), [](auto &a) { a.join(); });
}
