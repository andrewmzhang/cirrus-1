

import time
import random
from context import cirrus
from cirrus import LogisticRegression
from cirrus import app
from cirrus import CirrusBundle

url = "ec2-34-212-6-172.us-west-2.compute.amazonaws.com"
ip = "172.31.5.74"

data_bucket = 'cirrus-criteo-kaggle-19b-random'
model = 'model_v1'

basic_params = {
    'n_workers': 5,
    'n_ps': 1,
    'worker_size': 128,
    'dataset': data_bucket,
    'learning_rate': 0.01,
    'epsilon': 0.0001,
    'progress_callback': None,
    'timeout': 0,
    'threshold_loss': 0,
    'resume_model': model,
    'key_name': 'mykey',
    'key_path': '/home/camus/Downloads/mykey.pem',
    'ps_ip_public': url,
    'ps_ip_private': ip,
    'ps_username': 'ubuntu',
    'opt_method': 'adagrad',
    'checkpoint_model': 60,
    'minibatch_size': 20,
    'model_bits': 19,
    'use_grad_threshold': False,
    'grad_threshold': 0.001,
    'train_set': (0,824),
    'test_set': (835,840)
}


if __name__ == "__main__":
    batch = []
    index = 0
    base_port = 1337
    start =    0.100000
    end =      0.000001
    interval = 0.001
    for _ in range(10):
        config = basic_params.copy()
        config['ps_ip_port'] = base_port + (index * 2)
        config['learning_rate'] = start
        print(start)
        batch.append(config)
        start /= 1.25

    cb = CirrusBundle(task=LogisticRegression, param_dict_lst = batch)
    cb.set_jobs(2)
    cb.run_bundle()
    app.bundle = cb
    
    app.app.run_server()