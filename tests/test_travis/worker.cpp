#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>

#include <InputReader.h>
#include <PSSparseServerInterface.h>
#include <SparseLRModel.h>
#include <Configuration.h>
#include "SGD.h"
#include "Utils.h"
#include "Serializers.h"
#include <Tasks.h>

using namespace cirrus;

cirrus::Configuration config =
    cirrus::Configuration("configs/criteo_kaggle.cfg");

int main() {
  InputReader input;
  SparseDataset train_dataset = input.read_input_criteo_kaggle_sparse(
      "tests/test_data/train_lr.csv", ",", config);  // normalize=true
  train_dataset.check();
  train_dataset.print_info();

  SparseLRModel model(1 << config.get_model_bits());
  std::unique_ptr<PSSparseServerInterface> psi = std::make_unique<PSSparseServerInterface>("127.0.0.1", 1337);
  int version = 0;
  while (1) {
    SparseDataset minibatch = train_dataset.random_sample(20);
    psi->get_lr_sparse_model_inplace(minibatch, model, config);
    auto gradient = model.minibatch_grad_sparse(minibatch, config);
    gradient->setVersion(version++);
    LRSparseGradient* lrg = dynamic_cast<LRSparseGradient*>(gradient.get());
    psi->send_lr_gradient(*lrg);
  }
}
