import numpy as np
import tensorflow as tf
import os
import sys
import time
import re
import signal
import subprocess
import numpy as np
import math
from datetime import datetime

# input: txn_type + access_id + contention + op_type
# output: access decision + wait decision + piece end decision + wait info

ACCESSES = 26
OP_TYPE = 1 
CONTENTION_LEVEL = 1
RETRY_TIMES = 3 # [0, 1, >=2]
TXN_TYPE = 3
ACTION_DIMENSION = 3

INPUT_SPACE = CONTENTION_LEVEL * OP_TYPE * (ACCESSES)

ACCESSE_SPACE = INPUT_SPACE
PIECE_SPACE = INPUT_SPACE
WAIT_SPACE = INPUT_SPACE

txn_accesses = [11, 7, 8]
txn_accesses_sum = [0, 11, 18]

wait_info_act_count = [txn_accesses[0]+1, txn_accesses[1]+1, txn_accesses[2]+1]

REGEX_THPT = re.compile('agg_throughput\(([^)]+)\)')
REGEX_ABRT = re.compile('agg_abort_rate\(([^)]+)\)')

# tool functions
def parse(return_string):
    if return_string is None: return (0.0, 1.0)
    parse_thpt = re.search(REGEX_THPT, return_string)
    parse_abrt = re.search(REGEX_ABRT, return_string)
    if parse_thpt is None or parse_abrt is None:
        return (0.0, 1.0)
    thpt = parse_thpt.groups()[0]
    abrt = parse_abrt.groups()[0]
    return (float(thpt), float(abrt))

def parse_kid(pop_res, idx):
    start_pos = pop_res.find(str('./training/kids/kid_{}.txt'.format(idx)))
    if start_pos == -1:
        return -1
    end_pos = pop_res.find(str('./training/kids/kid_{}.txt'.format(idx + 1)))
    if end_pos == -1:
        end_pos = len(pop_res)
    return parse(pop_res[start_pos:end_pos])

def samples_eval(command, sample_count, load_per_sample):
    dict_res = {}
    pos = 0
    if load_per_sample:
        while pos < sample_count:
            command.append('--policy ./training/kids/kid_{}.txt'.format(pos))
            sys.stdout.flush()
            run_results = run(' '.join(command), die_after=180)
            dict_res[pos] = parse(run_results[1])
            # pop the command tail which is --policy parameter
            command.pop()
            pos = pos + 1
    else:
        while pos < sample_count:
            command.append('--kid-start {} --kid-end {}'.format(pos, sample_count))
            sys.stdout.flush()
            run_results = run(' '.join(command), die_after=900)
            # pop the command tail which is --kid-start --kid-end parameter
            command.pop()
            while pos < sample_count:
                kid_res = parse_kid(run_results[1], pos)
                if kid_res == -1:
                    dict_res[pos] = (float(0.0), float(1.0))
                    pos = pos + 1
                    break
                dict_res[pos] = kid_res
                pos = pos + 1
    return dict_res

def run(command, die_after = 0):
    extra = {} if die_after == 0 else {'preexec_fn': os.setsid}
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        shell=True, **extra)

    for _ in range(die_after if die_after > 0 else 600):
        if process.poll() is not None:
            break
        time.sleep(1)

    out_code = -1 if process.poll() is None else process.returncode

    if out_code < 0:
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        except Exception as e:
            print('{}, but continueing'.format(e))
        assert die_after != 0, 'Should only time out with die_after set'
        print('Failed with return code {}'.format(process.returncode))
        process.stdout.flush()
        return (out_code, process.stdout.read().decode('utf-8'))
        # return None # Timeout
    elif out_code > 0:
        print('Failed with return code {}'.format(process.returncode))
        process.stdout.flush()
        return (out_code, process.stdout.read().decode('utf-8'))
        # print(process.communicate())
        # return None

    process.stdout.flush()
    return (out_code, process.stdout.read().decode('utf-8'))

class Cache(object):
    def __init__(self, cache_lifetime):
        self._lifetime = cache_lifetime
        self._newitem = lambda: {'count': 0, 'average': 0, 'round': 0}
        self._cache = {}

    def __str__(self):
        return str(self._cache) + '\n'

    # more times a sample has been evaluated, less chance for it to be evaluate again.
    def query(self, sample):
        if not self._cache.__contains__(sample):
            return (False, float(.0))

        item = self._cache[sample]
        if np.random.rand() < 1./item['count']:
            return (False, float(.0))

        return (True, float(item['average']))

    def store(self, sample, sample_value, round_id):
        if not self._cache.__contains__(sample):
            self._cache[sample] = self._newitem()
        item = self._cache[sample]
        item['count'] += 1
        item['average'] = item['average'] + (sample_value - item['average']) / item['count']
        item['round'] = round_id

    def evict(self, round_id):
        for key in list(self._cache):
            if (round_id - self._cache[key]['round']) > self._lifetime:
                self._cache.pop(key)

class Sample:
    def __init__(self, access = [], wait = [], piece = [], 
                    wait_info1 = [], wait_info2 = [], wait_info3 = [],
                    txn_buf_size = 32, backoff = []):
        self.set_sample(access, wait, piece, wait_info1, wait_info2, wait_info3, txn_buf_size, backoff)

    def __str__(self):
        return str('access:') + '\n' + str(self.access) + '\n' + \
               str('wait:') + '\n' + str(self.wait) + '\n' + \
               str('piece:') + '\n' + str(self.piece) + '\n' + \
               str('wait_info1:') + '\n' + str(self.wait_info1) + '\n' + \
               str('wait_info2:') + '\n' + str(self.wait_info2) + '\n' + \
               str('wait_info3:') + '\n' + str(self.wait_info3) + '\n' + \
               str('txn_buf_size:') + '\n' + str(self._txn_buf_size) + '\n' + \
               str('backoff:') + '\n' + str(self.backoff) + '\n'

    def set_sample(self, access = [], wait = [], piece = [], 
                   wait_info1 = [], wait_info2 = [], wait_info3 = [],
                   txn_buf_size = 32, backoff = []):
        self.access = np.array(access)
        self.wait = np.array(wait)
        self.piece = np.array(piece)
        self.wait_info1 = np.array(wait_info1)
        self.wait_info2 = np.array(wait_info2)
        self.wait_info3 = np.array(wait_info3)
        self._txn_buf_size = txn_buf_size
        self.backoff = np.array(backoff)

    @classmethod
    def default_different_action(cls):
        all_result = []
        default = True
        # access
        result = [default] * (ACCESSE_SPACE)
        all_result.append(result)
        # wait
        result = [default] * (WAIT_SPACE)
        all_result.append(result)
        # piece
        result = [default] * (PIECE_SPACE)
        all_result.append(result)
        # wait_info1
        result = [default] * (WAIT_SPACE)
        all_result.append(result)
        # wait_info2
        result = [default] * (WAIT_SPACE)
        all_result.append(result)
        # wait_info3
        result = [default] * (WAIT_SPACE)
        all_result.append(result)

        return all_result

    # def get_actions(self):
    def get_actions(self, access_in, wait_in , piece_in, waitinfo1_in, waitinfo2_in, waitinfo3_in):
        access = []
        wait = []
        piece = []
        waitinfo1 = []
        waitinfo2 = []
        waitinfo3 = []
        # self.access = access_in
        # self.wait = wait_in
        # self.piece = piece_in
        # self.waitinfo1 = waitinfo1_in
        # self.waitinfo2 = waitinfo2_in
        # self.waitinfo3 = waitinfo3_in
        # print("test")
        # print(access_in)
        # print(access)

        for idx in range(ACCESSE_SPACE):
            access_one = [False] * 2
            access_one[access_in[idx]] = True
            access.append(access_one)
        for idx in range(PIECE_SPACE):
            piece_one = [False] * 2
            piece_one[piece_in[idx]] = True
            piece.append(piece_one)
        for idx in range(WAIT_SPACE):
            wait_one = [False] * 2
            waitinfo1_one = [False] * wait_info_act_count[0]
            waitinfo2_one = [False] * wait_info_act_count[1]
            waitinfo3_one = [False] * wait_info_act_count[2]
            wait_one[wait_in[idx]] = True
            waitinfo1_one[waitinfo1_in[idx]] = True
            waitinfo2_one[waitinfo2_in[idx]] = True
            waitinfo3_one[waitinfo3_in[idx]] = True
            wait.append(wait_one)
            waitinfo1.append(waitinfo1_one)
            waitinfo2.append(waitinfo2_one)
            waitinfo3.append(waitinfo3_one)

        # 现在每个action 的所有candidate中，和这个policy一致的就是1(True), 不一致的就是0（False）
        # 这里的scale变量是你要调的，以满足不同的概率e.g. 70% 80%
        # 举个例子， scale = 2的话， softmax结果，两个概率就是e^0/(e^0+e^2)和e^2/(e^0+e^2)
        # 注意waitinfo，长度不同，可能需要不同的scale值以满足都是70%的概率（70%是个例子）
        scale = 2

        access = np.array(access) * 1.3863
        wait = np.array(wait) * 1.3863
        piece = np.array(piece) * 1.3863
        waitinfo1 = np.array(waitinfo1) * 3.8712
        waitinfo2 = np.array(waitinfo2) * 3.4657
        waitinfo3 = np.array(waitinfo3) * 3.5835

        return access, wait, piece, waitinfo1, waitinfo2, waitinfo3

    def different_action(self, access, wait, piece, waitinfo1, waitinfo2, waitinfo3):
        all_result = []
        # access
        result = []
        for i in range(ACCESSE_SPACE):
            result.append(self.access[i] != access[i])
        all_result.append(result)
        # wait
        result = []
        for i in range(WAIT_SPACE):
            result.append(self.wait[i] != wait[i])
        all_result.append(result)
        # piece
        result = []
        for i in range(PIECE_SPACE):
            result.append(self.piece[i] != piece[i])
        all_result.append(result)
        # wait_info1
        result = []
        for i in range(WAIT_SPACE):
            result.append(self.wait_info1[i] != waitinfo1[i])
        all_result.append(result)
        # wait_info2
        result = []
        for i in range(WAIT_SPACE):
            result.append(self.wait_info2[i] != waitinfo2[i])
        all_result.append(result)
        # wait_info3
        result = []
        for i in range(WAIT_SPACE):
            result.append(self.wait_info3[i] != waitinfo3[i])
        all_result.append(result)

        return all_result

    def write_to_file(self, file):
        with open(file, 'w+') as file:
            # txn buffer size
            file.write("txn buffer size\n")
            file.write(str(self._txn_buf_size) + "\n")
            # retry backoff
            file.write("txn commit backoff part, 3 lines for retry times [0, 1, >=2] (decrease backoff)\n")
            for idx in range(RETRY_TIMES):
                stri = ""
                for i in range (TXN_TYPE):
                    stri += str(self.backoff[idx * TXN_TYPE + i]) + " "
                file.write(stri + "\n")
            file.write("txn abort backoff part, 3 lines for retry times [0, 1, >=2] (increase backoff)\n")
            for idx in range(RETRY_TIMES):
                stri = ""
                for i in range (TXN_TYPE):
                    stri += str(self.backoff[RETRY_TIMES * TXN_TYPE + idx * TXN_TYPE + i]) + " "
                file.write(stri + "\n")

            file.write("normal access\n")
            # traverse all possible states:
            for i in range(INPUT_SPACE):
                action = []
                action.append(self.access[i])
                action.append(self.wait[i])
                action.append(self.piece[i])
                action.append(self.wait_info1[i])
                action.append(self.wait_info2[i])
                action.append(self.wait_info3[i])

                # save the (s, a) pair to the file and use it to run the db system
                stri = ""
                for i in range (ACTION_DIMENSION + TXN_TYPE):
                    stri += str(action[i]) + " "
                file.write(stri + "\n")

    def pick_up(self, policy_path):
        self.access, self.wait, self.piece = [], [], []
        self.wait_info1, self.wait_info2, self.wait_info3 = [], [], []
        with open(policy_path, 'r') as f:
            # txn buffer size
            f.readline()
            txn_buf_size_str = f.readline().strip()
            self._txn_buf_size = int(txn_buf_size_str)
            # backoff
            self.backoff = []
            f.readline()
            for _ in range(RETRY_TIMES):
                backoff_str = f.readline().strip()
                self.backoff.extend(list([str(i) for i in backoff_str.split(' ')]))
            f.readline()
            for _ in range(RETRY_TIMES):
                backoff_str = f.readline().strip()
                self.backoff.extend(list([str(i) for i in backoff_str.split(' ')]))

            # normal action
            f.readline()
            for i in range(INPUT_SPACE):
                action = list([int(i) for i in f.readline().strip().split(' ')])
                assert len(action) == 3 + TXN_TYPE
                self.access.append(action[0])
                self.wait.append(action[1])
                self.piece.append(action[2])
                self.wait_info1.append(action[3])
                self.wait_info2.append(action[4])
                self.wait_info3.append(action[5])

    @property
    def np_access(self):
        return np.array(self.access)

    @property
    def np_wait(self):
        return np.array(self.wait)
    
    @property
    def np_piece(self):
        return np.array(self.piece)

    @property
    def np_wait_info1(self):
        return np.array(self.wait_info1)

    @property
    def np_wait_info2(self):
        return np.array(self.wait_info2)

    @property
    def np_wait_info3(self):
        return np.array(self.wait_info3)

    @property
    def txn_buf_size(self):
        return self._txn_buf_size

    @property
    def np_backoff(self):
        return np.array(self.backoff)

class Policy:
    def __init__(self, access = [], wait = [], piece = [], \
                 wait_info1 = [], wait_info2 = [], wait_info3 = []):
        self.access_prob = access
        self.wait_prob = wait
        self.piece_prob = piece
        self.wait_info1_prob = wait_info1
        self.wait_info2_prob = wait_info2
        self.wait_info3_prob = wait_info3

    def __str__(self):
        return str('access_prob:') + '\n' + str(self.access_prob) + '\n' + \
               str('wait_prob:') + '\n' + str(self.wait_prob) + '\n' + \
               str('piece_prob:') + '\n' + str(self.piece_prob) + '\n' + \
               str('wait_info1_prob:') + '\n' + str(self.wait_info1_prob) + '\n' + \
               str('wait_info2_prob:') + '\n' + str(self.wait_info2_prob) + '\n' + \
               str('wait_info3_prob:') + '\n' + str(self.wait_info3_prob)

    def set_prob(self, access = [], wait = [], piece = [], \
                 wait_info1 = [], wait_info2 = [], wait_info3 = []):
        self.access_prob = access
        self.wait_prob = wait
        self.piece_prob = piece
        self.wait_info1_prob = wait_info1
        self.wait_info2_prob = wait_info2
        self.wait_info3_prob = wait_info3

    def table_sample(self, file = None):
        access, wait, piece = [], [], []
        wait_info1, wait_info2, wait_info3 = [], [], []

        # sampling
        for idx in range(ACCESSE_SPACE):
            access.append(np.random.choice(2, p=self.access_prob[idx]))
        for idx in range(PIECE_SPACE):
            piece.append(np.random.choice(2, p=self.piece_prob[idx]))
        for idx in range(WAIT_SPACE):
            wait.append(np.random.choice(2, p=self.wait_prob[idx]))
            wait_info1.append(np.random.choice(wait_info_act_count[0], p=self.wait_info1_prob[idx]))
            wait_info2.append(np.random.choice(wait_info_act_count[1], p=self.wait_info2_prob[idx]))
            wait_info3.append(np.random.choice(wait_info_act_count[2], p=self.wait_info3_prob[idx]))
        
        sample = Sample(access, wait, piece, wait_info1, wait_info2, wait_info3, 6, [0,4,8,1,0,0,8,4,2,1,8,1,4,2,1,4,2,4])

        if file is not None:
            sample.write_to_file(file)

        return access, wait, piece, wait_info1, wait_info2, wait_info3, sample

    def table_sample_batch(self, kid_dir, samples_per_distribution):
        access_, wait_, piece_ = [], [], []
        wait_info1_, wait_info2_, wait_info3_ = [], [], []

        if not os.path.exists(kid_dir):
            os.mkdir(kid_dir)
        base_path = os.path.join(os.getcwd(), kid_dir)
        for idx in range(samples_per_distribution):
            recent_path = os.path.join(base_path, 'kid_{}.txt'.format(idx))
            if os.path.exists(recent_path):
                os.remove(recent_path)

            access, wait, piece, \
            wait_info1, wait_info2, wait_info3, \
            sample \
            = self.table_sample(recent_path)

            access_.extend(access)
            wait_.extend(wait)
            piece_.extend(piece)
            wait_info1_.extend(wait_info1)
            wait_info2_.extend(wait_info2)
            wait_info3_.extend(wait_info3)
        return access_, wait_, piece_, wait_info1_, wait_info2_, wait_info3_

    def samples(self, count):
        samples = []
        for idx in range(count):
            _, _, _, _, _, _, sample = self.table_sample()
            samples.append(sample)
        return samples
