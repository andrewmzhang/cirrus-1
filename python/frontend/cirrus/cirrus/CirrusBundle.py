from utils import *

class CirrusBundle:

    # Graph interfacer

    def __init__(self):
        self.cirrus_objs = []
        self.infos = []
        self.param_lst = []
        self.num_jobs = 1

        pass

    def set_task_parameters(self, task, param_dict_lst):
        self.param_lst = param_dict_lst

        for param in param_dict_lst:
            c = task(**param)
            self.cirrus_objs.append(c)
            self.infos.append({'color': get_random_color()})

    def get_number_experiments(self):
        return len(self.cirrus_objs)

    def set_jobs(self, n):
        self.num_jobs = n

    def set_existing_machines(self):
        pass

    def run(self):
        for cirrus_ob in self.cirrus_objs:
            cirrus_ob.run()

    def kill_all(self):
        for cirrus_ob in self.cirrus_objs:
            cirrus_ob.kill()

    # Get data regarding experiment i.
    def get_info(self, i, param=None):
        out = self.infos[i]
        if param:
            return out[param]
        else:
            return out

    # Gets the top n. If n == 0, gets all. Else gets last n
    def get_top(self, n):
        index = 0
        lst = []
        for cirrus_obj in self.cirrus_objs:
            loss = cirrus_obj.get_last_loss()
            lst.append((index, loss))
            index += 1
        lst.sort()
        top = lst[:n]
        return [cirrus_obj.get_time_loss() for cirrus_obj in top]

    def kill(self, i):
        self.cirrus_objs[i].kill()