#include <boost/lockfree/queue.hpp>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace std::chrono;
using json = nlohmann::json;

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
  std::cout << line << std::endl;
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
  constexpr static size_t kSize = 1024;

  std::array<std::string, kSize> requests;

  std::atomic_uint32_t head = 0;
  uint32_t tail = 0;

  std::counting_semaphore<> acquire{kSize - 10};
  std::counting_semaphore<> release{0};

  void push(std::string request) {
    acquire.acquire();
    requests[tail % kSize] = std::move(request);
    ++tail;
    release.release();
  }

  std::string pop() {
    release.acquire();
    auto p = head.fetch_add(1, std::memory_order_relaxed);
    auto result = std::move(requests[p % kSize]);
    acquire.release();
    return result;
  }

  size_t size() const { return tail - head.load(std::memory_order_relaxed); }
};

bool value(const std::vector<std::string> &fields, int idx) {
  return !fields.at(idx).empty();
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
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["s3_url"] = fields[f - 1];
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["id"] = std::stol(fields[f - 1]);
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
            std::stol(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["title"] = fields[f - 1];
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["longitude"] =
            std::stod(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["latitude"] =
            std::stod(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["accuracy"] =
            std::stod(fields[f - 1]);
      if (value(fields, f++))
        add_entity["AddEntity"]["properties"]["license"] = fields[f - 1];

      request.push_back(std::move(add_entity));

    } catch (...) {
    }

    if (request.size() >= batch) {
      q.push(request.dump());
      request = arrayValue;

      std::cout << q.size() << std::endl;
    }
  };

  read_data_file(callback);
}

int main() {

  Queue queue;
  read_file(queue, 200);
}
