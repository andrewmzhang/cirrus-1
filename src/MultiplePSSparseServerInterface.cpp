#include "MultiplePSSparseServerInterface.h"
#include <cassert>
#include "Constants.h"
#include "PSSparseServerInterface.h"
#include "config.h"
#include "lz4.h"

#undef DEBUG

#define MAX_MSG_SIZE (1024 * 1024)

namespace cirrus {

MultiplePSSparseServerInterface::MultiplePSSparseServerInterface(
    std::vector<std::string> param_ips,
    std::vector<uint64_t> ps_ports) {
  std::cout << "Starting Multiple PS " << param_ips.size() << std::endl;
  for (int i = 0; i < param_ips.size(); i++) {  // replace 2 with num_servers
    std::cout << "Attempting connection to " << param_ips[i] << ":"
              << ps_ports[i] << std::endl;
    auto ptr = new PSSparseServerInterface(param_ips[i], ps_ports[i]);
    while (true) {
      try {
        ptr->connect();
        break;
      } catch (std::exception& exc) {
        std::cout << exc.what();
      }
    }
    psints.push_back(ptr);
    std::cout << "Connected!!!" << std::endl;
  }
}

void MultiplePSSparseServerInterface::send_gradient(
    const LRSparseGradient& gradient) {
  int num_ps = psints.size();
  uint32_t operation = SEND_LR_GRADIENT;
#ifdef DEBUG
  std::cout << "Sending gradient" << std::endl;
#endif
  int ret;
  for (auto psint : psints) {
    ret = psint->send_wrapper(operation, sizeof(uint32_t));
    if (ret == -1)
      throw std::runtime_error("Error sending operation");
  }

  uint32_t size = gradient.getShardSerializedSize(num_ps);
  char data[size];
  auto starts_and_size = gradient.shard_serialize(data, num_ps);

  for (int i = 0; i < num_ps; i++) {
    auto psint = psints[i];
    auto sas = starts_and_size[i];

  char* msg_data = data + std::get<0>(sas);
  uint32_t msg_size = std::get<1>(sas);
  // this compression doesn't help
//#ifdef ENABLE_LR_COMPRESSION
//  size_t max_compressed_size = LZ4_compressBound(msg_size) + 2048;
//  std::shared_ptr<char[]> comp_data(new char[max_compressed_size]);
//  char* comp_ptr = comp_data.get();
//  store_value<uint32_t>(comp_ptr, msg_size);
//  msg_size =
//          LZ4_compress_default(msg_data, // dont compress the uncompr size
//                               comp_ptr, msg_size, max_compressed_size) +
//          sizeof(uint32_t);
//  msg_data = comp_data.get();
//#endif
//#ifdef DEBUG
//  std::cout << std::get<1>(sas) << " -> " << msg_size << std::endl;
//#endif

    ret = psint->send_wrapper(msg_size, sizeof(uint32_t));
    if (ret == -1) {
      throw std::runtime_error("Error sending grad size");
    }
    ret = psint->send_all_wrapper(msg_data, msg_size);
    if (ret == 0) {
      throw std::runtime_error("Error sending grad");
    }
  }
}

SparseLRModel MultiplePSSparseServerInterface::get_lr_sparse_model(
    const SparseDataset& ds,
    const Configuration& config) {
  SparseLRModel model(0);
  get_lr_sparse_model(ds, model, config);
  return std::move(model);
}

void MultiplePSSparseServerInterface::get_lr_sparse_model(
    const SparseDataset& ds,
    SparseLRModel& model,
    const Configuration& config) {
  // Initialize variables

  int num_servers = psints.size();
  char** msg_lst = new char*[num_servers];
  char** msg_begin_lst = new char*[num_servers];
  uint32_t* num_weights_lst = new uint32_t[num_servers];
  for (int i = 0; i < num_servers; i++) {
    msg_lst[i] = new char[MAX_MSG_SIZE];
    msg_begin_lst[i] = msg_lst[i];
    num_weights_lst[i] = 0;
    store_value<uint32_t>(
        msg_lst[i],
        num_weights_lst[i]);  // just make space for the number of weights
  }

  // Split the dataset based on which server data belongs to.
  // XXX consider optimizing this


  for (const auto& sample : ds.data_) {
    for (const auto& w : sample) {
      uint32_t server_index = w.first % num_servers;
      uint32_t data_index = (w.first - server_index) / num_servers;
      store_value<uint32_t>(msg_lst[server_index], data_index);
      num_weights_lst[server_index]++;
    }
  }

  for (int i = 0; i < num_servers; i++) {
    char* msg = msg_begin_lst[i];
    store_value(msg, num_weights_lst[i]);

    uint32_t operation = GET_LR_SPARSE_MODEL;
    if (send_all(psints[i]->sock, &operation, sizeof(uint32_t)) == -1) {
      throw std::runtime_error("Error getting sparse lr model");
    }

    uint32_t msg_size =
        sizeof(uint32_t) + sizeof(uint32_t) * num_weights_lst[i];
    char* msg_data = msg_begin_lst[i];
#ifdef ENABLE_LR_COMPRESSION
    size_t max_compressed_size = LZ4_compressBound(msg_size) + 2048;
    std::shared_ptr<char[]> comp_data(new char[max_compressed_size]);
    char* comp_ptr = comp_data.get();
    store_value<uint32_t>(comp_ptr, msg_size);
    msg_size =
            LZ4_compress_default(msg_data, // dont compress the uncompr size
                                 comp_ptr, msg_size, max_compressed_size) +
            sizeof(uint32_t);
    msg_data = comp_data.get();
#endif
#ifdef DEBUG
  std::cout << "model: " <<  sizeof(uint32_t) + sizeof(uint32_t) * num_weights_lst[i]
            << " -> " << msg_size << std::endl;
#endif

    // 2. Send msg size
    send_all(psints[i]->sock, &msg_size, sizeof(uint32_t));

    // 3. Send num_weights + weights
    if (send_all(psints[i]->sock, msg_data, msg_size) == -1) {
      throw std::runtime_error("Error getting sparse lr model");
    }
  }

  // we get the model subset with just the right amount of weights
  for (int i = 0; i < num_servers; i++) {
    psints[i]->get_lr_sparse_model_inplace_sharded(
        model, config, msg_begin_lst[i], num_weights_lst[i], i, num_servers);
    delete[] msg_begin_lst[i];
  }

  delete[] msg_begin_lst;
  delete[] msg_lst;
  delete[] num_weights_lst;
}

std::unique_ptr<CirrusModel> MultiplePSSparseServerInterface::get_full_model() {
  std::unique_ptr<SparseLRModel> model = std::make_unique<SparseLRModel>(0);
  // placeholder for now NOT CORRECT
  for (int i = 0; i < psints.size(); i++) {
    psints[i]->get_full_model_inplace(model, i, psints.size());
  }
  return std::move(model);
}

}  // namespace cirrus
