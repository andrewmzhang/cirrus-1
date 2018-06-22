#include "S3IteratorText.h"
#include "Utils.h"
#include <unistd.h>
#include <vector>
#include <iostream>

#include <pthread.h>
#include <semaphore.h>

#define FETCH_SIZE (10 * 1024 * 1024) //  size we try to fetch at a time

//#define DEBUG

// example
// imagine input is in libsvm formta
// <label> <index1>:<value1> <index2>:<value2> ...
// at each iteration we read ~10MB of data

namespace cirrus {
  
S3IteratorText::S3IteratorText(
        const Configuration& c,
        uint64_t file_size,
        uint64_t minibatch_rows, // number of samples in a minibatch
        bool use_label,          // whether each sample has a label
        int worker_id,           // id of this worker
        bool random_access) :    // whether to access samples in a random fashion
  S3Iterator(c),
  left_id(left_id), right_id(right_id),
  conf(c), s3_rows(s3_rows),
  minibatch_rows(minibatch_rows),
  minibatches_list(100000),
  use_label(use_label),
  worker_id(worker_id),
  re(worker_id),
  random_access(random_access)
{
      
  std::cout << "S3IteratorText::Creating S3IteratorText"
    << " left_id: " << left_id
    << " right_id: " << right_id
    << " use_label: " << use_label
    << std::endl;

  // initialize s3
  s3_initialize_aws();
  s3_client.reset(s3_create_client_ptr());

  for (uint64_t i = 0; i < read_ahead; ++i) {
    pref_sem.signal();
  }

  sem_init(&semaphore, 0, 0);

  thread = new std::thread(std::bind(&S3IteratorText::thread_function, this, c));

  // we fix the random seed but make it different for every worker
  // to ensure each worker receives a different minibatch
  if (random_access) {
    srand(42 + worker_id);
  } else {
    current = left_id;
  }
}

const void* S3IteratorText::get_next_fast() {
  sem_wait(&semaphore);
  ring_lock.lock();

  // first discard empty queue
  while (minibatches_list.front()->size() == 0) {
    auto queue_ptr = minibatches_list.pop();
    delete queue_ptr; // free memory of empty queue
  }
  auto ret = minibatches_list.front()->front();
  minibatches_list.front()->pop();
  num_minibatches_ready--;
  ring_lock.unlock();

#ifdef DEBUG
  if (ret.second != -1) {
    std::cout << "get_next_fast::ret.second: " << ret.second << std::endl;
  }
#endif

  // FIXME this should be calculating the local amount of memory
  if (num_minibatches_ready < 200 && pref_sem.getvalue() < (int)read_ahead) {
#ifdef DEBUG
    std::cout << "get_next_fast::pref_sem.signal" << std::endl;
#endif
    pref_sem.signal();
  }

  return ret.first;
}

/**
  * Moves index forward while data[index] is a space
  * returns true if it ended on a digit, otherwise returns false
  */
bool ignore_spaces(uint64_t& index, const std::string& data) {
  while (data[index] && data[index] == ' ') {
    index++;
  }
  return isdigit(data[index]);
}

template <class T>
T read_num(uint64_t& index, const std::string& data) {
  assert(isdigit(data[index]));
  
  uint64_t index_fw = index;
  while (isdigit(data[index_fw])) {
    index_fw++;
  }
}

/**
  * Build minibatch from text in libsvm format
  */
bool S3IteratorText::build_dataset(
    const std::string& data, uint64_t index,
    std::shared_ptr<SparseDataset>& minibatch) {
  // format
  //<label> <index1>:<value1> <index2>:<value2>

  try {
    std::shared_ptr<SparseDataset> = std::make_shared<SparseDataset>();
    std::vector<std::vector<std::pair<int, FEATURE_TYPE>>> samples;
    std::vector<FEATURE_TYPE> labels;

    samples.resize(minibatch_rows);
    labels.resize(minibatch_rows);

    for (uint64_t sample = 0; sample < minibatch_rows; ++sample) {
      // ignore spaces
      if (!ignore_spaces(index, data)) {
        return false;
      }
      int label = read_num<int>(index, data);

      // read pairs
      while (1) {
        if (!ignore_spaces(index, data)) {
          if (data[index] == '\n') break; // move to next sample
          else return false; // end of text
        }
        uint64_t ind = read_num<uint64_t>(index, data);
        if (data[index] != ':') {
          return false;
        }
        index++;
        FEATURE_TYPE value = read_num<FEATURE_TYPE>(index, data);

        samples[sample].push_back(std::make_pair(ind, value));
      }
      labels[sample] = label;
    }
  } catch (...) {
    // read_num throws exception if it can't find a digit right away
    return false;
  }
}

std::vector<std::shared_ptr<SparseDataset>>>
parse_s3_obj_libsvm(const std::string& s3_data) {
  std::vector<std::shared_ptr<SparseDataset>> result;
  // find first sample
  uint64_t index = 0;
  while (1) {
    if (index >= s3_data.size()) {
      return result;
    }
    if (s3_data[index] == "\n") {
      index++;
      break;
    }
    index++;
  }
  if (index == s3_data.size()) {
    // bad luck last char was the first newline
    throw std::runtime_error("Error");
  }

  while (1) {
    std::shared_ptr<SparseDataset> minibatch;
    if (!build_dataset(s3_data, index, &minibatch)) {
      // could not build full minibatch
      return result;
    }
    result.push_back(minibatch);
  }
}

void S3IteratorText::push_samples(std::ostringstream* oss) {
  uint64_t n_minibatches = s3_rows / minibatch_rows;

  // we parse this piece of text
  // this returns a collection of minibatches
  auto s3_data = oss->str();
  std::vector<std::shared_ptr<SparseDataset>> dataset =
    parse_s3_obj_libsvm(s3_data);

  ring_lock.lock();
  minibatches_list.add(dataset);
  ring_lock.unlock();
  for (uint64_t i = 0; i < n_minibatches; ++i) {
    num_minibatches_ready++;
    sem_post(&semaphore);
  }
  str_version++;
}

static int sstream_size(std::ostringstream& ss) {
  return ss.tellp();
}

/**
  * Returns a range of bytes (right side is exclusive)
  */
std::make_pair<uint64_t, uint64_t>
S3IteratorText::get_file_range(uint64_t file_size) {
  // given the size of the file we return a random file index
  if (file_size < FETCH_SIZE) {
    // file is small so we get the whole file
    return std::make_pair(0, file_size);
  }

  // we sample the left side of the range
  std::uniform_int_distribution<int> sampler(0, file_size - 1);
  uint64_t left_index = sampler(re);
  if (file_size - left_index < FETCH_SIZE) {
    // make sure we get a range with size FETCH_SIZE
    left_index = file_size - FETCH_SIZE;
  }
  return std::make_pair(left_index, left_index + FETCH_SIZE);
}

void S3IteratorText::report_bandwidth() {
  uint64_t elapsed_us = (get_time_us() - start);
  double mb_s = sstream_size(*s3_obj) / elapsed_us
    * 1000.0 * 1000 / 1024 / 1024;
  std::cout << "received s3 obj"
    << " elapsed: " << elapsed_us
    << " size: " << sstream_size(*s3_obj)
    << " BW (MB/s): " << mb_s
    << "\n";
}

void S3IteratorText::thread_function(const Configuration& config) {
  std::cout << "Building S3 deser. with size: "
    << std::endl;

  uint64_t count = 0;
  while (1) {
    // if we can go it means there is a slot
    // in the ring
    std::cout << "Waiting for pref_sem" << std::endl;
    pref_sem.wait();

    // FIXME we should allow random and non-random
    // random range of bytes to be fetched from dataset
    std::pair<uint64_t, uint64_t> range = get_file_range(file_size);

    std::ostringstream* s3_obj = nullptr;
try_start:
    try {
      std::cout << "S3IteratorText: getting object " << obj_id_str << std::endl;
      uint64_t start = get_time_us();

      s3_obj = s3_get_object_range_ptr(
          config.get_s3_dataset_key(), *s3_client,
          config.get_s3_bucket(), range);

      report_bandwidth(get_time_us() - start, sstream_size(*s3_obj));
    } catch(...) {
      std::cout
        << "S3IteratorText: error in s3_get_object"
        << " obj_id_str: " << obj_id_str
        << std::endl;
      goto try_start;
      exit(-1);
    }
    push_samples(s3_obj);
  }
}

} // namespace cirrus

