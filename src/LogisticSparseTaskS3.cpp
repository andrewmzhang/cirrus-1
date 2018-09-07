#include <Tasks.h>

#include <unistd.h>
#include <pthread.h>
#include <memory>
#include "MultiplePSSparseServerInterface.h"
#include "PSSparseServerInterface.h"
#include "S3SparseIterator.h"
#include "Serializers.h"
#include "Utils.h"

#undef DEBUG

namespace cirrus {

void LogisticSparseTaskS3::push_gradient(LRSparseGradient* lrg) {
  auto before_push_us = get_time_us();

  //sparse_model_get->psi->send_lr_gradient(*lrg);
#ifdef DEBUG
  std::cout << "Published gradients!" << std::endl;
  auto elapsed_push_us = get_time_us() - before_push_us;
  static uint64_t before = 0;
  if (before == 0)
    before = get_time_us();
  auto now = get_time_us();
  std::cout << "[WORKER] "
      << "Worker task published gradient"
      << " with version: " << lrg->getVersion()
      << " at time (us): " << get_time_us()
      << " took(us): " << elapsed_push_us
      << " bw(MB/s): " << std::fixed <<
         (1.0 * lrg->getSerializedSize() / elapsed_push_us / 1024 / 1024 * 1000 * 1000)
      << " since last(us): " << (now - before)
      << "\n";
  before = now;
#endif
}

// get samples and labels data
bool LogisticSparseTaskS3::get_dataset_minibatch(
    std::shared_ptr<SparseDataset>& dataset,
    S3SparseIterator& s3_iter) {
#ifdef DEBUG
  auto start = get_time_us();
#endif

  dataset = s3_iter.getNext();
#ifdef DEBUG
  auto finish1 = get_time_us();
#endif

#ifdef DEBUG
  auto finish2 = get_time_us();
  double bw = 1.0 * dataset->getSizeBytes() /
    (finish2-start) * 1000.0 * 1000 / 1024 / 1024;
  std::cout << "[WORKER] Get Sample Elapsed (S3) "
    << " minibatch size: " << config.get_minibatch_size()
    << " part1(us): " << (finish1 - start)
    << " part2(us): " << (finish2 - finish1)
    << " BW (MB/s): " << bw
    << " at time: " << get_time_us()
    << "\n";
#endif
  return true;
}

void LogisticSparseTaskS3::run(const Configuration& config, int worker) {
  std::cout << "Starting LogisticSparseTaskS3 " << ps_ips.size() << std::endl;
  uint64_t num_s3_batches = config.get_limit_samples() / config.get_s3_size();
  this->config = config;

  std::cout << "[WORKER] " << "num s3 batches: " << num_s3_batches
    << std::endl;
  wait_for_start(worker, nworkers);

  // Create iterator that goes from 0 to num_s3_batches
  auto train_range = config.get_train_range();
  S3SparseIterator s3_iter(
      train_range.first, train_range.second,
      config, config.get_s3_size(), config.get_minibatch_size(),
      true, worker);

  std::cout << "[WORKER] starting loop" << std::endl;

  uint64_t version = 1;
  SparseLRModel model(1 << config.get_model_bits());

  bool printed_rate = false;
  int cnt = 0;
  auto start_time = get_time_ms();
  while (1) {
    auto t0 = get_time_ms();
    std::shared_ptr<SparseDataset> dataset;
    if (!get_dataset_minibatch(dataset, s3_iter)) {
      continue;
    }
    auto t1 = get_time_ms();
    auto time_took = t1 - t0;
    if (time_took < 15) {
        usleep((15 - time_took) * 1000);
    }

    auto now = get_time_ms();
    cnt++;
    if (now - start_time > 1000) {
        std::cout << "[WORKER] Events: " << cnt << std::endl;
        start_time = now;
        cnt = 0;
    }



  }
}

} // namespace cirrus

