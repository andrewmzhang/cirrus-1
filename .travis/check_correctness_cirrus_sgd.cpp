#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>

#include <InputReader.h>
#include <SparseLRModel.h>
#include <Configuration.h>
#include "SGD.h"

using namespace cirrus;

void print_info(const auto& samples) {
  std::cout << "Number of samples: " << samples.size() << std::endl;
  std::cout << "Number of cols: " << samples[0].size() << std::endl;
}

double check_error(auto model, auto dataset) {
  auto ret = model->calc_loss(dataset, 0);
  auto loss = ret.first;
  auto avg_loss = loss / dataset.num_samples();
  std::cout << "total loss: " << loss
    << " avg loss: " << avg_loss
    << std::endl;
  return avg_loss;
}

cirrus::Configuration config = cirrus::Configuration("../configs/criteo_kaggle.cfg"); 
std::mutex model_lock;
std::unique_ptr<SparseLRModel> model;
double epsilon = 0.00001;
double learning_rate = 0.1;
std::unique_ptr<OptimizationMethod> opt_method = std::make_unique<SGD>(learning_rate);

void learning_function(const SparseDataset& dataset) {
  for (uint64_t i = 0; 20; ++i) {
    SparseDataset ds = dataset.random_sample(20);

    auto gradient = model->minibatch_grad(ds, epsilon);

    model_lock.lock();
    opt_method->sgd_update(model, gradient.release());
    model_lock.unlock();
  }
}

int main() {
  InputReader input;
  SparseDataset dataset = input.read_input_criteo_kaggle_sparse(
      config.get_input_path(),
      ",", config); // normalize=true
  dataset.check();
  dataset.print_info();

  model.reset(new SparseLRModel((1 << config.get_model_bits()) + 1));

  uint64_t num_threads = 20;
  std::vector<std::shared_ptr<std::thread>> threads;
  for (uint64_t i = 0; i < num_threads; ++i) {
    threads.push_back(std::make_shared<std::thread>(
          learning_function, dataset));
  }

  while (1) {
    usleep(100000); // 100ms
    model_lock.lock();
    auto avg_loss = check_error(model.get(), dataset);
    model_lock.unlock();
    if (avg_loss <= 0.15) {
      break;
    }
  }
  for (uint64_t i = 0; i < num_threads; ++i) {
    (*threads[i]).detach();
  }

  return 0;
}
