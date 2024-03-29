#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <utility>
#include <string>
#include <algorithm>

#include <stdlib.h>
#include <sched.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include "bench.h"

#include "../counter.h"
#include "../scopedperf.hh"
#include "../allocator.h"

#ifdef USE_JEMALLOC
//cannot include this header b/c conflicts with malloc.h
//#include <jemalloc/jemalloc.h>
extern "C" void malloc_stats_print(void (*write_cb)(void *, const char *), void *cbopaque, const char *opts);
extern "C" int mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
#endif
#ifdef USE_TCMALLOC
#include <google/heap-profiler.h>
#endif

using namespace std;
using namespace util;

size_t nthreads = 1;
volatile bool running = true;
int verbose = 0;
uint64_t txn_flags = 0;
double scale_factor = 1.0;
uint64_t runtime = 30;
uint64_t ops_per_worker = 0;
int run_mode = RUNMODE_TIME;
int enable_parallel_loading = false;
int pin_cpus = 0;
int slow_exit = 0;
int retry_aborted_transaction = 0;
int no_reset_counters = 0;
int backoff_aborted_transaction = 0;
int consistency_check = 0;
int dynamic_workload = 0;
int kid_start = 0;
int kid_end = 0;
std::vector<std::string> policies_to_eval;
double backoff_alpha = 0.5;

template <typename T>
static void
delete_pointers(const vector<T *> &pts)
{
  for (size_t i = 0; i < pts.size(); i++)
    delete pts[i];
}

template <typename T>
static vector<T>
elemwise_sum(const vector<T> &a, const vector<T> &b)
{
  INVARIANT(a.size() == b.size());
  vector<T> ret(a.size());
  for (size_t i = 0; i < a.size(); i++)
    ret[i] = a[i] + b[i];
  return ret;
}

template <typename K, typename V>
static void
map_agg(map<K, V> &agg, const map<K, V> &m)
{
  for (typename map<K, V>::const_iterator it = m.begin();
       it != m.end(); ++it)
    agg[it->first] += it->second;
}

// returns <free_bytes, total_bytes>
static pair<uint64_t, uint64_t>
get_system_memory_info()
{
  struct sysinfo inf;
  sysinfo(&inf);
  return make_pair(inf.mem_unit * inf.freeram, inf.mem_unit * inf.totalram);
}

static bool
clear_file(const char *name)
{
  ofstream ofs(name);
  ofs.close();
  return true;
}

static void
write_cb(void *p, const char *s) UNUSED;
static void
write_cb(void *p, const char *s)
{
  const char *f = "jemalloc.stats";
  static bool s_clear_file UNUSED = clear_file(f);
  ofstream ofs(f, ofstream::app);
  ofs << s;
  ofs.flush();
  ofs.close();
}

static event_avg_counter evt_avg_abort_spins("avg_abort_spins");

void
bench_worker::run()
{
  // XXX(stephentu): so many nasty hacks here. should actually
  // fix some of this stuff one day
  if (set_core_id)
    coreid::set_core_id(worker_id); // cringe
  {
    scoped_rcu_region r; // register this thread in rcu region
  }
  on_run_setup();
  scoped_db_thread_ctx ctx(db, false);
  const workload_desc_vec workload = get_workload();
  txn_counts.resize(workload.size());
  abort_counts.resize(workload.size());
  barrier_a->count_down();
  barrier_b->wait_for();
  timer t;
  bool is_retry = false;
  int which_retry = 0;
  while (running && (run_mode != RUNMODE_OPS || ntxn_commits < ops_per_worker)) {
    double d = r.next_uniform();
    for (size_t i = 0; i < workload.size(); i++) {
      if ((i + 1) == workload.size() || d < workload[i].frequency) {
      retry:
        // timer t;
        if (is_retry) {
          is_retry = false;
        } else {
          t.lap();
        }
        const unsigned long old_seed = r.get_seed();
        const auto ret = workload[i].fn(this);
        // ret.second == 0 means this txn is a read-only transaction
        // since read-only transactions use snapshot, they must have been committed
        if (likely(ret.first)) {
          ++ntxn_commits;
          uint64_t time = t.lap();
          // latency_numer_us += time;
          switch(ret.second)
          {
            case 1:
            {
              ++ntxn_commits_new_order;
              tail_latency1.emplace_back(time);
              break;
            }
            case 2:
            {
              ++ntxn_commits_payment;
              tail_latency2.emplace_back(time);
              break;
            }
            case 3:
            {
              ++ntxn_commits_delivery;
              tail_latency3.emplace_back(time);
              break;
            }
            case 4:
            {
              ++ntxn_commits_order_status;
              tail_latency4.emplace_back(time);
              break;
            }
            case 5:
            {
              ++ntxn_commits_stock_level;
              tail_latency5.emplace_back(time);
              break;
            }
          }
          backoff_action action = pg->inference_backoff_action(true /*success*/, which_retry, ret.second);
          modify_backoff(action.first, action.second);
          which_retry = 0;
        } else {
          ++ntxn_aborts;
          switch(ret.second)
          {
            case 1:
            {
              ++ntxn_aborts_new_order;
              break;
            }
            case 2:
            {
              ++ntxn_aborts_payment;
              break;
            }
            case 3:
            {
              ++ntxn_aborts_delivery;
              break;
            }
            case 4:
            {
              ++ntxn_aborts_order_status;
              break;
            }
            case 5:
            {
              ++ntxn_aborts_stock_level;
              break;
            }
          }
          if (retry_aborted_transaction && running && !is_user_initiate_abort) {
            if (backoff_aborted_transaction) {
              backoff_action action = pg->inference_backoff_action(false /*fail*/, which_retry, ret.second);
              modify_backoff(action.first, action.second);
              uint64_t spins = backoff;
              evt_avg_abort_spins.offer(spins);
              while (spins) {
                nop_pause();
                spins--;
              }
            }
            ++which_retry;
            r.set_seed(old_seed);
            is_retry = true;
            goto retry;
          }
        }
        is_user_initiate_abort = false;
        size_delta += ret.second; // should be zero on abort
        // txn_counts[i]++; // txn_counts aren't used to compute throughput (is
                         // just an informative number to print to the console
                         // in verbose mode)
        break;
      }
      d -= workload[i].frequency;
    }
  }
}

void
bench_runner::run()
{
  // load data
  const vector<bench_loader *> loaders = make_loaders();
  {
    spin_barrier b(loaders.size());
    const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();
    {
      scoped_timer t("dataloading", verbose);
      for (vector<bench_loader *>::const_iterator it = loaders.begin();
          it != loaders.end(); ++it) {
        (*it)->set_barrier(b);
        (*it)->start();
      }
      for (vector<bench_loader *>::const_iterator it = loaders.begin();
          it != loaders.end(); ++it)
        (*it)->join();
    }
    const pair<uint64_t, uint64_t> mem_info_after = get_system_memory_info();
    const int64_t delta = int64_t(mem_info_before.first) - int64_t(mem_info_after.first); // free mem
    const double delta_mb = double(delta)/1048576.0;
    if (verbose)
      cerr << "DB size: " << delta_mb << " MB" << endl;
  }

  db->do_txn_epoch_sync(); // also waits for worker threads to be persisted
  {
    const auto persisted_info = db->get_ntxn_persisted();
    if (get<0>(persisted_info) != get<1>(persisted_info))
      cerr << "ERROR: " << persisted_info << endl;
    //ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
    if (verbose)
      cerr << persisted_info << " txns persisted in loading phase" << endl;
  }
  db->reset_ntxn_persisted();

  if (!no_reset_counters) {
    event_counter::reset_all_counters(); // XXX: for now - we really should have a before/after loading
    PERF_EXPR(scopedperf::perfsum_base::resetall());
  }
  {
    const auto persisted_info = db->get_ntxn_persisted();
    if (get<0>(persisted_info) != 0 ||
        get<1>(persisted_info) != 0 ||
        get<2>(persisted_info) != 0.0) {
      cerr << persisted_info << endl;
      ALWAYS_ASSERT(false);
    }
  }

  map<string, size_t> table_sizes_before;
  if (verbose) {
    for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
         it != open_tables.end(); ++it) {
      scoped_rcu_region guard;
      const size_t s = it->second->size();
      cerr << "table " << it->first << " size " << s << endl;
      table_sizes_before[it->first] = s;
    }
    cerr << "starting benchmark..." << endl;
  }

  const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();

  const vector<bench_worker *> workers = make_workers();
  ALWAYS_ASSERT(!workers.empty());
  for (vector<bench_worker *>::const_iterator it = workers.begin();
       it != workers.end(); ++it)
    (*it)->start();

  barrier_a.wait_for(); // wait for all threads to start up
  timer t, t_nosync;
  barrier_b.count_down(); // bombs away!
  if (run_mode == RUNMODE_TIME) {
    sleep(runtime);
    running = false;
  }
  __sync_synchronize();
  for (size_t i = 0; i < nthreads; i++)
    workers[i]->join();
  const unsigned long elapsed_nosync = t_nosync.lap();
  db->do_txn_finish(); // waits for all worker txns to persist
  size_t n_commits = 0;
  size_t n_commits_new_order = 0;
  size_t n_commits_payment = 0;
  size_t n_commits_delivery = 0;
  size_t n_commits_order_status = 0;
  size_t n_commits_stock_level = 0;
  size_t n_aborts = 0;
  size_t n_aborts_new_order = 0;
  size_t n_aborts_payment = 0;
  size_t n_aborts_delivery = 0;
  size_t n_aborts_order_status = 0;
  size_t n_aborts_stock_level = 0;
  uint64_t latency_numer_us = 0;

  std::vector<uint64_t> total_latency;
  std::vector<uint64_t> latency1;
  std::vector<uint64_t> latency2;
  std::vector<uint64_t> latency3;
  std::vector<uint64_t> latency4;
  std::vector<uint64_t> latency5;
  total_latency.reserve(1 << 16);
  latency1.reserve(1 << 16);
  latency2.reserve(1 << 16);
  latency3.reserve(1 << 16);
  latency4.reserve(1 << 16);
  latency5.reserve(1 << 16);

  uint64_t all_txns_sum = 0;
  uint64_t new_order_sum = 0;
  uint64_t payment_sum = 0;
  uint64_t delivery_sum = 0;
  uint64_t order_status_sum = 0;
  uint64_t stock_level_sum = 0;

  for (size_t i = 0; i < nthreads; i++) {
    n_commits += workers[i]->get_ntxn_commits();
    n_commits_new_order += workers[i]->get_ntxn_commits_new_order();
    n_commits_payment += workers[i]->get_ntxn_commits_payment();
    n_commits_delivery += workers[i]->get_ntxn_commits_delivery();
    n_commits_order_status += workers[i]->get_ntxn_commits_order_status();
    n_commits_stock_level += workers[i]->get_ntxn_commits_stock_level();

    n_aborts += workers[i]->get_ntxn_aborts();
    n_aborts_new_order += workers[i]->get_ntxn_aborts_new_order();
    n_aborts_payment += workers[i]->get_ntxn_aborts_payment();
    n_aborts_delivery += workers[i]->get_ntxn_aborts_delivery();
    n_aborts_order_status += workers[i]->get_ntxn_aborts_order_status();
    n_aborts_stock_level += workers[i]->get_ntxn_aborts_stock_level();

    latency_numer_us += workers[i]->get_latency_numer_us();
    std::vector<uint64_t> vec1 = workers[i]->get_tail_latency1();
    std::vector<uint64_t> vec2 = workers[i]->get_tail_latency2();
    std::vector<uint64_t> vec3 = workers[i]->get_tail_latency3();
    std::vector<uint64_t> vec4 = workers[i]->get_tail_latency4();
    std::vector<uint64_t> vec5 = workers[i]->get_tail_latency5();
    for (auto item : vec1) {
      total_latency.emplace_back(item);
      all_txns_sum += item;
      latency1.emplace_back(item);
      new_order_sum += item;
    }
    for (auto item : vec2) {
      total_latency.emplace_back(item);
      all_txns_sum += item;
      latency2.emplace_back(item);
      payment_sum += item;
    }
    for (auto item : vec3) {
      total_latency.emplace_back(item);
      all_txns_sum += item;
      latency3.emplace_back(item);
      delivery_sum += item;
    }
    for (auto item : vec4) {
      total_latency.emplace_back(item);
      all_txns_sum += item;
      latency4.emplace_back(item);
      order_status_sum += item;
    }
    for (auto item : vec5) {
      total_latency.emplace_back(item);
      all_txns_sum += item;
      latency5.emplace_back(item);
      stock_level_sum += item;
    }
  }

  std::sort(total_latency.begin(), total_latency.end());
  std::sort(latency1.begin(), latency1.end());
  std::sort(latency2.begin(), latency2.end());
  std::sort(latency3.begin(), latency3.end());
  std::sort(latency4.begin(), latency4.end());
  std::sort(latency5.begin(), latency5.end());

  double all_txns_avg = (double) all_txns_sum / (double) total_latency.size();
  uint64_t all_txns_p50 = total_latency[(uint64_t) (total_latency.size() * 0.5)];
  uint64_t all_txns_p90 = total_latency[(uint64_t) (total_latency.size() * 0.9)];
  uint64_t all_txns_p95 = total_latency[(uint64_t) (total_latency.size() * 0.95)];
  uint64_t all_txns_p99 = total_latency[(uint64_t) (total_latency.size() * 0.99)];
  uint64_t all_txns_p999 = total_latency[(uint64_t) (total_latency.size() * 0.999)];
  uint64_t all_txns_p9999 = total_latency[(uint64_t) (total_latency.size() * 0.9999)];

  std::cout << "Latency - microseconds(µs)" << std::endl;
  // std::cout << "all_txns_avg latency - " << all_txns_avg << std::endl;
  // std::cout << "all_txns_p50 latency - " << all_txns_p50 << std::endl;
  // std::cout << "all_txns_p90 latency - " << all_txns_p90 << std::endl;
  // std::cout << "all_txns_p95 latency - " << all_txns_p95 << std::endl;
  // std::cout << "all_txns_p99 latency - " << all_txns_p99 << std::endl;
  // std::cout << "all_txns_p999 latency - " << all_txns_p999 << std::endl;
  // std::cout << "all_txns_p9999 latency - " << all_txns_p9999 << std::endl;

  double new_order_avg = (double) new_order_sum / (double) latency1.size();
  uint64_t new_order_p50 = latency1[(uint64_t) (latency1.size() * 0.5)];
  uint64_t new_order_p90 = latency1[(uint64_t) (latency1.size() * 0.9)];
  uint64_t new_order_p95 = latency1[(uint64_t) (latency1.size() * 0.95)];
  uint64_t new_order_p99 = latency1[(uint64_t) (latency1.size() * 0.99)];
  uint64_t new_order_p999 = latency1[(uint64_t) (latency1.size() * 0.999)];
  uint64_t new_order_p9999 = latency1[(uint64_t) (latency1.size() * 0.9999)];

  // std::cout << "new_order_avg latency - " << new_order_avg << std::endl;
  std::cout << "new_order_p50 latency - " << new_order_p50 << std::endl;
  std::cout << "new_order_p90 latency - " << new_order_p90 << std::endl;
  // std::cout << "new_order_p95 latency - " << new_order_p95 << std::endl;
  std::cout << "new_order_p99 latency - " << new_order_p99 << std::endl;
  // std::cout << "new_order_p999 latency - " << new_order_p999 << std::endl;
  // std::cout << "new_order_p9999 latency - " << new_order_p9999 << std::endl;

  double payment_avg = (double) payment_sum / (double) latency2.size();
  uint64_t payment_p50 = latency2[(uint64_t) (latency2.size() * 0.5)];
  uint64_t payment_p90 = latency2[(uint64_t) (latency2.size() * 0.9)];
  uint64_t payment_p95 = latency2[(uint64_t) (latency2.size() * 0.95)];
  uint64_t payment_p99 = latency2[(uint64_t) (latency2.size() * 0.99)];
  uint64_t payment_p999 = latency2[(uint64_t) (latency2.size() * 0.999)];
  uint64_t payment_p9999 = latency2[(uint64_t) (latency2.size() * 0.9999)];

  // std::cout << "payment_avg latency - " << payment_avg << std::endl;
  std::cout << "payment_p50 latency - " << payment_p50 << std::endl;
  std::cout << "payment_p90 latency - " << payment_p90 << std::endl;
  // std::cout << "payment_p95 latency - " << payment_p95 << std::endl;
  std::cout << "payment_p99 latency - " << payment_p99 << std::endl;
  // std::cout << "payment_p999 latency - " << payment_p999 << std::endl;
  // std::cout << "payment_p9999 latency - " << payment_p9999 << std::endl;

  double delivery_avg = (double) delivery_sum / (double) latency3.size();
  uint64_t delivery_p50 = latency3[(uint64_t) (latency3.size() * 0.5)];
  uint64_t delivery_p90 = latency3[(uint64_t) (latency3.size() * 0.9)];
  uint64_t delivery_p95 = latency3[(uint64_t) (latency3.size() * 0.95)];
  uint64_t delivery_p99 = latency3[(uint64_t) (latency3.size() * 0.99)];
  uint64_t delivery_p999 = latency3[(uint64_t) (latency3.size() * 0.999)];
  uint64_t delivery_p9999 = latency3[(uint64_t) (latency3.size() * 0.9999)];
  
  // std::cout << "delivery_avg latency - " << delivery_avg << std::endl;
  std::cout << "delivery_p50 latency - " << delivery_p50 << std::endl;
  std::cout << "delivery_p90 latency - " << delivery_p90 << std::endl;
  // std::cout << "delivery_p95 latency - " << delivery_p95 << std::endl;
  std::cout << "delivery_p99 latency - " << delivery_p99 << std::endl;
  // std::cout << "delivery_p999 latency - " << delivery_p999 << std::endl;
  // std::cout << "delivery_p9999 latency - " << delivery_p9999 << std::endl;

  double order_status_avg = (double) order_status_sum / (double) latency4.size();
  uint64_t order_status_p50 = latency4[(uint64_t) (latency4.size() * 0.5)];
  uint64_t order_status_p90 = latency4[(uint64_t) (latency4.size() * 0.9)];
  uint64_t order_status_p95 = latency4[(uint64_t) (latency4.size() * 0.95)];
  uint64_t order_status_p99 = latency4[(uint64_t) (latency4.size() * 0.99)];
  uint64_t order_status_p999 = latency4[(uint64_t) (latency4.size() * 0.999)];
  uint64_t order_status_p9999 = latency4[(uint64_t) (latency4.size() * 0.9999)];
  
  // std::cout << "order_status_avg latency - " << order_status_avg << std::endl;
  // std::cout << "order_status_p50 latency - " << order_status_p50 << std::endl;
  // std::cout << "order_status_p90 latency - " << order_status_p90 << std::endl;
  // std::cout << "order_status_p95 latency - " << order_status_p95 << std::endl;
  // std::cout << "order_status_p99 latency - " << order_status_p99 << std::endl;
  // std::cout << "order_status_p999 latency - " << order_status_p999 << std::endl;
  // std::cout << "order_status_p9999 latency - " << order_status_p9999 << std::endl;

  double stock_level_avg = (double) stock_level_sum / (double) latency5.size();
  uint64_t stock_level_p50 = latency5[(uint64_t) (latency5.size() * 0.5)];
  uint64_t stock_level_p90 = latency5[(uint64_t) (latency5.size() * 0.9)];
  uint64_t stock_level_p95 = latency5[(uint64_t) (latency5.size() * 0.95)];
  uint64_t stock_level_p99 = latency5[(uint64_t) (latency5.size() * 0.99)];
  uint64_t stock_level_p999 = latency5[(uint64_t) (latency5.size() * 0.999)];
  uint64_t stock_level_p9999 = latency5[(uint64_t) (latency5.size() * 0.9999)];
  
  // std::cout << "stock_level_avg latency - " << stock_level_avg << std::endl;
  // std::cout << "stock_level_p50 latency - " << stock_level_p50 << std::endl;
  // std::cout << "stock_level_p90 latency - " << stock_level_p90 << std::endl;
  // std::cout << "stock_level_p95 latency - " << stock_level_p95 << std::endl;
  // std::cout << "stock_level_p99 latency - " << stock_level_p99 << std::endl;
  // std::cout << "stock_level_p999 latency - " << stock_level_p999 << std::endl;
  // std::cout << "stock_level_p9999 latency - " << stock_level_p9999 << std::endl;

  const auto persisted_info = db->get_ntxn_persisted();

  const unsigned long elapsed = t.lap(); // lap() must come after do_txn_finish(),
                                         // because do_txn_finish() potentially
                                         // waits a bit

  //consistency check
  {
    const vector<bench_checker *> checkers = make_checkers();
    if(checkers.size() > 0) {
      spin_barrier b(checkers.size());

      for (vector<bench_checker *>::const_iterator it = checkers.begin();
          it != checkers.end(); ++it) {
        (*it)->set_barrier(b);
        (*it)->start();
      }

      for (vector<bench_checker *>::const_iterator it = checkers.begin();
          it != checkers.end(); ++it)
        (*it)->join();
    }
  }

  // various sanity checks
  ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
  // not == b/c persisted_info does not count read-only txns
  ALWAYS_ASSERT(n_commits >= get<1>(persisted_info));

  const double elapsed_nosync_sec = double(elapsed_nosync) / 1000000.0;
  const double agg_nosync_throughput = double(n_commits) / elapsed_nosync_sec;
  const double avg_nosync_per_core_throughput = agg_nosync_throughput / double(workers.size());

  const double elapsed_sec = double(elapsed) / 1000000.0;
  const double agg_throughput = double(n_commits) / elapsed_sec;
  const double agg_throughput_new_order = double(n_commits_new_order) / elapsed_sec;
  const double agg_throughput_payment = double(n_commits_payment) / elapsed_sec;
  const double agg_throughput_delivery = double(n_commits_delivery) / elapsed_sec;
  const double agg_throughput_order_status = double(n_commits_order_status) / elapsed_sec;
  const double agg_throughput_stock_level = double(n_commits_stock_level) / elapsed_sec;
  const double avg_per_core_throughput = agg_throughput / double(workers.size());

  const double agg_abort_throughput = double(n_aborts) / elapsed_sec;
  const double agg_abort_throughput_new_order = double(n_aborts_new_order) / elapsed_sec;
  const double agg_abort_throughput_payment = double(n_aborts_payment) / elapsed_sec;
  const double agg_abort_throughput_delivery = double(n_aborts_delivery) / elapsed_sec;
  const double agg_abort_throughput_order_status = double(n_aborts_order_status) / elapsed_sec;
  const double agg_abort_throughput_stock_level = double(n_aborts_stock_level) / elapsed_sec;

  const double agg_abort_rate = double(n_aborts) / (n_commits + n_aborts);//elapsed_sec;
  const double agg_abort_rate_new_order = double(n_aborts_new_order) / (n_commits_new_order + n_aborts_new_order);//elapsed_sec;
  const double agg_abort_rate_payment = double(n_aborts_payment) / (n_commits_payment + n_aborts_payment);//elapsed_sec;
  const double agg_abort_rate_delivery = double(n_aborts_delivery) / (n_commits_delivery + n_aborts_delivery);//elapsed_sec;
  const double agg_abort_rate_order_status = double(n_aborts_order_status) / (n_commits_order_status + n_aborts_order_status);//elapsed_sec;
  const double agg_abort_rate_stock_level = double(n_aborts_stock_level) / (n_commits_stock_level + n_aborts_stock_level);//elapsed_sec;

  const double avg_per_core_abort_rate = agg_abort_rate / double(workers.size());

  // we can use n_commits here, because we explicitly wait for all txns
  // run to be durable
  const double agg_persist_throughput = double(n_commits) / elapsed_sec;
  const double avg_per_core_persist_throughput =
    agg_persist_throughput / double(workers.size());

  // XXX(stephentu): latency currently doesn't account for read-only txns
  const double avg_latency_us =
    double(latency_numer_us) / double(n_commits);
  const double avg_latency_ms = avg_latency_us / 1000.0;
  const double avg_persist_latency_ms =
    get<2>(persisted_info) / 1000.0;

  map<string, size_t> agg_txn_counts = workers[0]->get_txn_counts();
  map<string, size_t> agg_abort_counts = workers[0]->get_abort_counts();
  ssize_t size_delta = workers[0]->get_size_delta();
  for (size_t i = 1; i < workers.size(); i++) {
    map_agg(agg_txn_counts, workers[i]->get_txn_counts());
    map_agg(agg_abort_counts, workers[i]->get_abort_counts());
    size_delta += workers[i]->get_size_delta();
  }
  map<string, double> abort_rate_breakdown;
  for (const auto &iter : agg_txn_counts) {
    size_t commit_count = iter.second;
    size_t abort_count = agg_abort_counts[iter.first];
    abort_rate_breakdown[iter.first] = abort_count / static_cast<double>(abort_count + commit_count);
  }

  if (verbose) {
    const pair<uint64_t, uint64_t> mem_info_after = get_system_memory_info();
    const int64_t delta = int64_t(mem_info_before.first) - int64_t(mem_info_after.first); // free mem
    const double delta_mb = double(delta)/1048576.0;
    const double size_delta_mb = double(size_delta)/1048576.0;
    map<string, counter_data> ctrs = event_counter::get_all_counters();

    cerr << "--- table statistics ---" << endl;
    for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
         it != open_tables.end(); ++it) {
      scoped_rcu_region guard;
      const size_t s = it->second->size();
      const ssize_t delta = ssize_t(s) - ssize_t(table_sizes_before[it->first]);
      cerr << "table " << it->first << " size " << it->second->size();
      if (delta < 0)
        cerr << " (" << delta << " records)" << endl;
      else
        cerr << " (+" << delta << " records)" << endl;
    }
#ifdef ENABLE_BENCH_TXN_COUNTERS
    cerr << "--- txn counter statistics ---" << endl;
    {
      // take from thread 0 for now
      abstract_db::txn_counter_map agg = workers[0]->get_local_txn_counters();
      for (auto &p : agg) {
        cerr << p.first << ":" << endl;
        for (auto &q : p.second)
          cerr << "  " << q.first << " : " << q.second << endl;
      }
    }
#endif
    cerr << "--- benchmark statistics ---" << endl;
    cerr << "runtime: " << elapsed_sec << " sec" << endl;
    cerr << "memory delta: " << delta_mb  << " MB" << endl;
    cerr << "memory delta rate: " << (delta_mb / elapsed_sec)  << " MB/sec" << endl;
    cerr << "logical memory delta: " << size_delta_mb << " MB" << endl;
    cerr << "logical memory delta rate: " << (size_delta_mb / elapsed_sec) << " MB/sec" << endl;
    cerr << "agg_nosync_throughput: " << agg_nosync_throughput << " ops/sec" << endl;
    cerr << "avg_nosync_per_core_throughput: " << avg_nosync_per_core_throughput << " ops/sec/core" << endl;
    cerr << "agg_throughput: " << agg_throughput << " ops/sec" << endl;
    cerr << "avg_per_core_throughput: " << avg_per_core_throughput << " ops/sec/core" << endl;
    cerr << "agg_persist_throughput: " << agg_persist_throughput << " ops/sec" << endl;
    cerr << "avg_per_core_persist_throughput: " << avg_per_core_persist_throughput << " ops/sec/core" << endl;
    cerr << "avg_latency: " << avg_latency_ms << " ms" << endl;
    cerr << "avg_persist_latency: " << avg_persist_latency_ms << " ms" << endl;
    cerr << "agg_abort_rate: " << agg_abort_rate << endl;
    cerr << "avg_per_core_abort_rate: " << avg_per_core_abort_rate << " aborts/sec/core" << endl;
    cerr << "txn_breakdown: " << format_list(agg_txn_counts.begin(), agg_txn_counts.end()) << endl;
    cerr << "abort_breakdown: " << format_list(agg_abort_counts.begin(), agg_abort_counts.end()) << endl;
    cerr << "abort_rate_breakdown: " << format_list(abort_rate_breakdown.begin(), abort_rate_breakdown.end()) << endl;
    cerr << "--- system counters (for benchmark) ---" << endl;
    for (map<string, counter_data>::iterator it = ctrs.begin();
         it != ctrs.end(); ++it)
      cerr << it->first << ": " << it->second << endl;
    cerr << "--- perf counters (if enabled, for benchmark) ---" << endl;
    PERF_EXPR(scopedperf::perfsum_base::printall());
    cerr << "--- allocator stats ---" << endl;
    ::allocator::DumpStats();
    cerr << "---------------------------------------" << endl;

#ifdef USE_JEMALLOC
    cerr << "dumping heap profile..." << endl;
    mallctl("prof.dump", NULL, NULL, NULL, 0);
    cerr << "printing jemalloc stats..." << endl;
    malloc_stats_print(write_cb, NULL, "");
#endif
#ifdef USE_TCMALLOC
    HeapProfilerDump("before-exit");
#endif
  }

  // cout << "COMMIT_BREAKDOWN ";
  // for (const auto cnt : agg_txn_counts) {
  //     cout << cnt.second << " ";
  // }
  // cout << endl;

  // cout << "ABORT_BREAKDOWN ";
  // for (const auto cnt : agg_abort_counts) {
  //     cout << cnt.second << " ";
  // }
  // cout << endl;
  // output for plotting script
  // cout << "RESULT "
  //      << "agg_throughput(" << agg_throughput << ")," << endl
  //      << "agg_new_order_throughput(" << agg_throughput_new_order << ")," << endl
  //      << "agg_payment_throughput(" << agg_throughput_payment << ")," << endl
  //      << "agg_delivery_throughput(" << agg_throughput_delivery << ")," << endl
  //      << "agg_order_status_throughput(" << agg_throughput_order_status << ")," << endl
  //      << "agg_stock_level_throughput(" << agg_throughput_stock_level << ")," << endl
  //      << "agg_persist_throughput(" << agg_persist_throughput << ")," << endl
  //      << "avg_latency_ms(" << avg_latency_ms << ")," << endl
  //      << "avg_persist_latency_ms(" << avg_persist_latency_ms << ")," << endl
  //      << "agg_abort_throughput(" << agg_abort_throughput << ")," << endl
  //      << "agg_abort_throughput_new_order(" << agg_abort_throughput_new_order << ")," << endl
  //      << "agg_abort_throughput_payment(" << agg_abort_throughput_payment << ")," << endl
  //      << "agg_abort_throughput_delivery(" << agg_abort_throughput_delivery << ")," << endl
  //      << "agg_abort_throughput_order_status(" << agg_abort_throughput_order_status << ")," << endl
  //      << "agg_abort_throughput_stock_level(" << agg_abort_throughput_stock_level << ")," << endl
  //      << "agg_abort_rate(" << agg_abort_rate << ")," << endl
  //      << "agg_abort_rate_new_order(" << agg_abort_rate_new_order << ")," << endl
  //      << "agg_abort_rate_payment(" << agg_abort_rate_payment << ")," << endl
  //      << "agg_abort_rate_delivery(" << agg_abort_rate_delivery << ")," << endl
  //      << "agg_abort_rate_order_status(" << agg_abort_rate_order_status << ")," << endl
  //      << "agg_abort_rate_stock_level(" << agg_abort_rate_stock_level << ")," << endl;
       
  cout.flush();

  if (!slow_exit)
    return;

  map<string, uint64_t> agg_stats;
  for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
       it != open_tables.end(); ++it) {
    map_agg(agg_stats, it->second->clear());
    delete it->second;
  }
  if (verbose) {
    for (auto &p : agg_stats)
      cerr << p.first << " : " << p.second << endl;

  }
  open_tables.clear();

  delete_pointers(loaders);
  delete_pointers(workers);
}

void
bench_runner::dynamic_run()
{
  // load data
  const vector<bench_loader *> loaders = make_loaders();
  {
    spin_barrier b(loaders.size());
    const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();
    {
      scoped_timer t("dataloading", verbose);
      for (vector<bench_loader *>::const_iterator it = loaders.begin();
          it != loaders.end(); ++it) {
        (*it)->set_barrier(b);
        (*it)->start();
      }
      for (vector<bench_loader *>::const_iterator it = loaders.begin();
          it != loaders.end(); ++it)
        (*it)->join();
    }
    const pair<uint64_t, uint64_t> mem_info_after = get_system_memory_info();
    const int64_t delta = int64_t(mem_info_before.first) - int64_t(mem_info_after.first); // free mem
    const double delta_mb = double(delta)/1048576.0;
    if (verbose)
      cerr << "DB size: " << delta_mb << " MB" << endl;
  }

  
  // workers initlaization
  const vector<bench_worker *> workers = make_workers();

  // todo - FIX ME
  // dynamic workload info
  bool dynamic_info = true;
  std::vector<double> factors = {1, 2, 4, 8, 16, 32};
  std::vector<double> workloads;
  for (int i = 0; i < factors.size(); i++)
  {
    for (double j = 0; j < 5 /*keep one contenion for how many iters*/; j++)
    {
      workloads.push_back(factors[i]);
    }
  }
  // workload detection related
  PolicySelector *selector = db->selector;
  double last_commit_throughput = 0;
  double last_abort_throughput = 0;

  // iterate benchmark running
  for (int run_count = 0; run_count < workloads.size(); ++run_count) {
    // reset some workload info 
    barrier_a.reset(nthreads);
    barrier_b.reset(1);
    nthreads = nthreads;
    running = true;
    scale_factor = workloads[run_count];
    if (dynamic_info) std::cerr << "Period " << run_count << " current workload is " << scale_factor << "wh" << std::endl;

    db->do_txn_epoch_sync(); // also waits for worker threads to be persisted
    {
      const auto persisted_info = db->get_ntxn_persisted();
      if (get<0>(persisted_info) != get<1>(persisted_info))
        cerr << "ERROR: " << persisted_info << endl;
      //ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
      if (verbose)
        cerr << persisted_info << " txns persisted in loading phase" << endl;
    }
    db->reset_ntxn_persisted();

    if (!no_reset_counters) {
      event_counter::reset_all_counters(); // XXX: for now - we really should have a before/after loading
      PERF_EXPR(scopedperf::perfsum_base::resetall());
    }
    {
      const auto persisted_info = db->get_ntxn_persisted();
      if (get<0>(persisted_info) != 0 ||
          get<1>(persisted_info) != 0 ||
          get<2>(persisted_info) != 0.0) {
        cerr << persisted_info << endl;
        ALWAYS_ASSERT(false);
      }
    }

    map<string, size_t> table_sizes_before;
    if (verbose) {
      for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
          it != open_tables.end(); ++it) {
        scoped_rcu_region guard;
        const size_t s = it->second->size();
        cerr << "table " << it->first << " size " << s << endl;
        table_sizes_before[it->first] = s;
      }
      cerr << "starting benchmark..." << endl;
    }

    const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();

    // set worker's policy according to its workload - heuristic
    // Policy *current_policy = selector->policy_interrupt(last_commit_throughput, last_abort_throughput);
    for (size_t i = 0; i < nthreads; i++) {
      // if (dynamic_info) std::cerr << "worker " << i << " using policy " << current_policy->get_identifier() << std::endl;
      // workers[i]->set_policy(current_policy);
      workers[i]->set_pg(db->pg);
      // tpc-c hack - let each worker known its valid range to access
      workers[i]->reset_workload(nthreads, i);
      workers[i]->clear();
    }

    ALWAYS_ASSERT(!workers.empty());
    for (vector<bench_worker *>::const_iterator it = workers.begin();
        it != workers.end(); ++it)
      (*it)->start();

    barrier_a.wait_for(); // wait for all threads to start up
    timer t, t_nosync;
    barrier_b.count_down(); // bombs away!
    if (run_mode == RUNMODE_TIME) {
      sleep(runtime);
      running = false;
    }
    __sync_synchronize();
    for (size_t i = 0; i < nthreads; i++)
      workers[i]->join();
    const unsigned long elapsed_nosync = t_nosync.lap();
    db->do_txn_finish(); // waits for all worker txns to persist
    size_t n_commits = 0;
    size_t n_aborts = 0;
    uint64_t latency_numer_us = 0;
    for (size_t i = 0; i < nthreads; i++) {
      n_commits += workers[i]->get_ntxn_commits();
      n_aborts += workers[i]->get_ntxn_aborts();
      latency_numer_us += workers[i]->get_latency_numer_us();
    }
    const auto persisted_info = db->get_ntxn_persisted();

    const unsigned long elapsed = t.lap(); // lap() must come after do_txn_finish(),
                                          // because do_txn_finish() potentially
                                          // waits a bit

    // various sanity checks
    ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
    // not == b/c persisted_info does not count read-only txns
    ALWAYS_ASSERT(n_commits >= get<1>(persisted_info));

    const double elapsed_nosync_sec = double(elapsed_nosync) / 1000000.0;
    const double agg_nosync_throughput = double(n_commits) / elapsed_nosync_sec;
    const double avg_nosync_per_core_throughput = agg_nosync_throughput / double(workers.size());

    const double elapsed_sec = double(elapsed) / 1000000.0;
    const double agg_throughput = double(n_commits) / elapsed_sec;
    const double avg_per_core_throughput = agg_throughput / double(workers.size());

    const double agg_abort_throughput = double(n_aborts) / elapsed_sec;
    const double agg_abort_rate = double(n_aborts) / (n_commits + n_aborts);//elapsed_sec;
    const double avg_per_core_abort_rate = agg_abort_rate / double(workers.size());

    // we can use n_commits here, because we explicitly wait for all txns
    // run to be durable
    const double agg_persist_throughput = double(n_commits) / elapsed_sec;
    const double avg_per_core_persist_throughput =
      agg_persist_throughput / double(workers.size());

    // XXX(stephentu): latency currently doesn't account for read-only txns
    const double avg_latency_us =
      double(latency_numer_us) / double(n_commits);
    const double avg_latency_ms = avg_latency_us / 1000.0;
    const double avg_persist_latency_ms =
      get<2>(persisted_info) / 1000.0;

    if (verbose) {
      const pair<uint64_t, uint64_t> mem_info_after = get_system_memory_info();
      const int64_t delta = int64_t(mem_info_before.first) - int64_t(mem_info_after.first); // free mem
      const double delta_mb = double(delta)/1048576.0;
      map<string, size_t> agg_txn_counts = workers[0]->get_txn_counts();
      ssize_t size_delta = workers[0]->get_size_delta();
      for (size_t i = 1; i < workers.size(); i++) {
        map_agg(agg_txn_counts, workers[i]->get_txn_counts());
        size_delta += workers[i]->get_size_delta();
      }
      const double size_delta_mb = double(size_delta)/1048576.0;
      map<string, counter_data> ctrs = event_counter::get_all_counters();

      cerr << "--- table statistics ---" << endl;
      for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
          it != open_tables.end(); ++it) {
        scoped_rcu_region guard;
        const size_t s = it->second->size();
        const ssize_t delta = ssize_t(s) - ssize_t(table_sizes_before[it->first]);
        cerr << "table " << it->first << " size " << it->second->size();
        if (delta < 0)
          cerr << " (" << delta << " records)" << endl;
        else
          cerr << " (+" << delta << " records)" << endl;
      }
#ifdef ENABLE_BENCH_TXN_COUNTERS
      cerr << "--- txn counter statistics ---" << endl;
      {
        // take from thread 0 for now
        abstract_db::txn_counter_map agg = workers[0]->get_local_txn_counters();
        for (auto &p : agg) {
          cerr << p.first << ":" << endl;
          for (auto &q : p.second)
            cerr << "  " << q.first << " : " << q.second << endl;
        }
      }
#endif
      cerr << "--- benchmark statistics ---" << endl;
      cerr << "runtime: " << elapsed_sec << " sec" << endl;
      cerr << "memory delta: " << delta_mb  << " MB" << endl;
      cerr << "memory delta rate: " << (delta_mb / elapsed_sec)  << " MB/sec" << endl;
      cerr << "logical memory delta: " << size_delta_mb << " MB" << endl;
      cerr << "logical memory delta rate: " << (size_delta_mb / elapsed_sec) << " MB/sec" << endl;
      cerr << "agg_nosync_throughput: " << agg_nosync_throughput << " ops/sec" << endl;
      cerr << "avg_nosync_per_core_throughput: " << avg_nosync_per_core_throughput << " ops/sec/core" << endl;
      cerr << "agg_throughput: " << agg_throughput << " ops/sec" << endl;
      cerr << "avg_per_core_throughput: " << avg_per_core_throughput << " ops/sec/core" << endl;
      cerr << "agg_persist_throughput: " << agg_persist_throughput << " ops/sec" << endl;
      cerr << "avg_per_core_persist_throughput: " << avg_per_core_persist_throughput << " ops/sec/core" << endl;
      cerr << "avg_latency: " << avg_latency_ms << " ms" << endl;
      cerr << "avg_persist_latency: " << avg_persist_latency_ms << " ms" << endl;
      cerr << "agg_abort_rate: " << agg_abort_rate << " (aborts/commits + aborts)" << endl;
      cerr << "avg_per_core_abort_rate: " << avg_per_core_abort_rate << " aborts/sec/core" << endl;
      cerr << "txn breakdown: " << format_list(agg_txn_counts.begin(), agg_txn_counts.end()) << endl;
      cerr << "--- system counters (for benchmark) ---" << endl;
      for (map<string, counter_data>::iterator it = ctrs.begin();
          it != ctrs.end(); ++it)
        cerr << it->first << ": " << it->second << endl;
      cerr << "--- perf counters (if enabled, for benchmark) ---" << endl;
      PERF_EXPR(scopedperf::perfsum_base::printall());
      cerr << "--- allocator stats ---" << endl;
      ::allocator::DumpStats();
      cerr << "---------------------------------------" << endl;

#ifdef USE_JEMALLOC
      cerr << "dumping heap profile..." << endl;
      mallctl("prof.dump", NULL, NULL, NULL, 0);
      cerr << "printing jemalloc stats..." << endl;
      malloc_stats_print(write_cb, NULL, "");
#endif
#ifdef USE_TCMALLOC
      HeapProfilerDump("before-exit");
#endif
    }

    // maintain this time benchmark info
    last_commit_throughput = agg_throughput;
    last_abort_throughput = agg_abort_throughput;

    // output for plotting script
    cout << "RESULT "
        << "agg_throughput(" << agg_throughput << "),"
        << "agg_persist_throughput(" << agg_persist_throughput << "),"
        << "avg_latency_ms(" << avg_latency_ms << "),"
        << "avg_persist_latency_ms(" << avg_persist_latency_ms << "),"
        << "agg_abort_throughput(" << agg_abort_throughput << "),"
        << "agg_abort_rate(" << agg_abort_rate << "),"
        << "0" <<endl;
        
    cout.flush();
  }
  if (!slow_exit)
    return;

  map<string, uint64_t> agg_stats;
  for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
       it != open_tables.end(); ++it) {
    map_agg(agg_stats, it->second->clear());
    delete it->second;
  }
  if (verbose) {
    for (auto &p : agg_stats)
      cerr << p.first << " : " << p.second << endl;

  }
  open_tables.clear();

  delete_pointers(workers);
  delete_pointers(loaders);
}

void
bench_runner::training_run(std::vector<std::string>& policies)
{
  // load data
  const vector<bench_loader *> loaders = make_loaders();
  {
    spin_barrier b(loaders.size());
    const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();
    {
      scoped_timer t("dataloading", verbose);
      for (vector<bench_loader *>::const_iterator it = loaders.begin();
          it != loaders.end(); ++it) {
        (*it)->set_barrier(b);
        (*it)->start();
      }
      for (vector<bench_loader *>::const_iterator it = loaders.begin();
          it != loaders.end(); ++it)
        (*it)->join();
    }
    const pair<uint64_t, uint64_t> mem_info_after = get_system_memory_info();
    const int64_t delta = int64_t(mem_info_before.first) - int64_t(mem_info_after.first); // free mem
    const double delta_mb = double(delta)/1048576.0;
    if (verbose)
      cerr << "DB size: " << delta_mb << " MB" << endl;
  }

  
  // workers initlaization
  const vector<bench_worker *> workers = make_workers();

  PolicySelector *selector = db->selector;

  // iterate benchmark running
  for (int run_count = 0; run_count < policies.size(); ++run_count) {
    // reset some workload info 
    barrier_a.reset(nthreads);
    barrier_b.reset(1);
    nthreads = nthreads;
    running = true;

    db->do_txn_epoch_sync(); // also waits for worker threads to be persisted
    {
      const auto persisted_info = db->get_ntxn_persisted();
      if (get<0>(persisted_info) != get<1>(persisted_info))
        cerr << "ERROR: " << persisted_info << endl;
      //ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
      if (verbose)
        cerr << persisted_info << " txns persisted in loading phase" << endl;
    }
    db->reset_ntxn_persisted();

    if (!no_reset_counters) {
      event_counter::reset_all_counters(); // XXX: for now - we really should have a before/after loading
      PERF_EXPR(scopedperf::perfsum_base::resetall());
    }
    {
      const auto persisted_info = db->get_ntxn_persisted();
      if (get<0>(persisted_info) != 0 ||
          get<1>(persisted_info) != 0 ||
          get<2>(persisted_info) != 0.0) {
        cerr << persisted_info << endl;
        ALWAYS_ASSERT(false);
      }
    }

    map<string, size_t> table_sizes_before;
    if (verbose) {
      for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
          it != open_tables.end(); ++it) {
        scoped_rcu_region guard;
        const size_t s = it->second->size();
        cerr << "table " << it->first << " size " << s << endl;
        table_sizes_before[it->first] = s;
      }
      cerr << "starting benchmark..." << endl;
    }

    const pair<uint64_t, uint64_t> mem_info_before = get_system_memory_info();

    // set worker's policy
    PolicyGradient *pg = new PolicyGradient(policies[run_count]);
    // Policy *current_policy = selector->pick_policy_by_name(policies[run_count]);
    for (size_t i = 0; i < nthreads; i++) {
      // workers[i]->set_policy(current_policy);
      workers[i]->set_pg(pg);
      workers[i]->clear();
    }
    // todo -reclaim the pg instance

    ALWAYS_ASSERT(!workers.empty());
    for (vector<bench_worker *>::const_iterator it = workers.begin();
        it != workers.end(); ++it)
      (*it)->start();

    barrier_a.wait_for(); // wait for all threads to start up
    timer t, t_nosync;
    barrier_b.count_down(); // bombs away!
    if (run_mode == RUNMODE_TIME) {
      sleep(runtime);
      running = false;
    }
    __sync_synchronize();
    for (size_t i = 0; i < nthreads; i++)
      workers[i]->join();
    const unsigned long elapsed_nosync = t_nosync.lap();
    db->do_txn_finish(); // waits for all worker txns to persist
    size_t n_commits = 0;
    size_t n_aborts = 0;
    uint64_t latency_numer_us = 0;
    for (size_t i = 0; i < nthreads; i++) {
      n_commits += workers[i]->get_ntxn_commits();
      n_aborts += workers[i]->get_ntxn_aborts();
      latency_numer_us += workers[i]->get_latency_numer_us();
    }
    const auto persisted_info = db->get_ntxn_persisted();

    const unsigned long elapsed = t.lap(); // lap() must come after do_txn_finish(),
                                          // because do_txn_finish() potentially
                                          // waits a bit

    // various sanity checks
    ALWAYS_ASSERT(get<0>(persisted_info) == get<1>(persisted_info));
    // not == b/c persisted_info does not count read-only txns
    ALWAYS_ASSERT(n_commits >= get<1>(persisted_info));

    const double elapsed_nosync_sec = double(elapsed_nosync) / 1000000.0;
    const double agg_nosync_throughput = double(n_commits) / elapsed_nosync_sec;
    const double avg_nosync_per_core_throughput = agg_nosync_throughput / double(workers.size());

    const double elapsed_sec = double(elapsed) / 1000000.0;
    const double agg_throughput = double(n_commits) / elapsed_sec;
    const double avg_per_core_throughput = agg_throughput / double(workers.size());

    const double agg_abort_throughput = double(n_aborts) / elapsed_sec;
    const double agg_abort_rate = double(n_aborts) / (n_commits + n_aborts);//elapsed_sec;
    const double avg_per_core_abort_rate = agg_abort_rate / double(workers.size());

    // we can use n_commits here, because we explicitly wait for all txns
    // run to be durable
    const double agg_persist_throughput = double(n_commits) / elapsed_sec;
    const double avg_per_core_persist_throughput =
      agg_persist_throughput / double(workers.size());

    // XXX(stephentu): latency currently doesn't account for read-only txns
    const double avg_latency_us =
      double(latency_numer_us) / double(n_commits);
    const double avg_latency_ms = avg_latency_us / 1000.0;
    const double avg_persist_latency_ms =
      get<2>(persisted_info) / 1000.0;

    if (verbose) {
      const pair<uint64_t, uint64_t> mem_info_after = get_system_memory_info();
      const int64_t delta = int64_t(mem_info_before.first) - int64_t(mem_info_after.first); // free mem
      const double delta_mb = double(delta)/1048576.0;
      map<string, size_t> agg_txn_counts = workers[0]->get_txn_counts();
      ssize_t size_delta = workers[0]->get_size_delta();
      for (size_t i = 1; i < workers.size(); i++) {
        map_agg(agg_txn_counts, workers[i]->get_txn_counts());
        size_delta += workers[i]->get_size_delta();
      }
      const double size_delta_mb = double(size_delta)/1048576.0;
      map<string, counter_data> ctrs = event_counter::get_all_counters();

      cerr << "--- table statistics ---" << endl;
      for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
          it != open_tables.end(); ++it) {
        scoped_rcu_region guard;
        const size_t s = it->second->size();
        const ssize_t delta = ssize_t(s) - ssize_t(table_sizes_before[it->first]);
        cerr << "table " << it->first << " size " << it->second->size();
        if (delta < 0)
          cerr << " (" << delta << " records)" << endl;
        else
          cerr << " (+" << delta << " records)" << endl;
      }
#ifdef ENABLE_BENCH_TXN_COUNTERS
      cerr << "--- txn counter statistics ---" << endl;
      {
        // take from thread 0 for now
        abstract_db::txn_counter_map agg = workers[0]->get_local_txn_counters();
        for (auto &p : agg) {
          cerr << p.first << ":" << endl;
          for (auto &q : p.second)
            cerr << "  " << q.first << " : " << q.second << endl;
        }
      }
#endif
      cerr << "--- benchmark statistics ---" << endl;
      cerr << "runtime: " << elapsed_sec << " sec" << endl;
      cerr << "memory delta: " << delta_mb  << " MB" << endl;
      cerr << "memory delta rate: " << (delta_mb / elapsed_sec)  << " MB/sec" << endl;
      cerr << "logical memory delta: " << size_delta_mb << " MB" << endl;
      cerr << "logical memory delta rate: " << (size_delta_mb / elapsed_sec) << " MB/sec" << endl;
      cerr << "agg_nosync_throughput: " << agg_nosync_throughput << " ops/sec" << endl;
      cerr << "avg_nosync_per_core_throughput: " << avg_nosync_per_core_throughput << " ops/sec/core" << endl;
      cerr << "agg_throughput: " << agg_throughput << " ops/sec" << endl;
      cerr << "avg_per_core_throughput: " << avg_per_core_throughput << " ops/sec/core" << endl;
      cerr << "agg_persist_throughput: " << agg_persist_throughput << " ops/sec" << endl;
      cerr << "avg_per_core_persist_throughput: " << avg_per_core_persist_throughput << " ops/sec/core" << endl;
      cerr << "avg_latency: " << avg_latency_ms << " ms" << endl;
      cerr << "avg_persist_latency: " << avg_persist_latency_ms << " ms" << endl;
      cerr << "agg_abort_rate: " << agg_abort_rate << " (aborts/commits + aborts)" << endl;
      cerr << "avg_per_core_abort_rate: " << avg_per_core_abort_rate << " aborts/sec/core" << endl;
      cerr << "txn breakdown: " << format_list(agg_txn_counts.begin(), agg_txn_counts.end()) << endl;
      cerr << "--- system counters (for benchmark) ---" << endl;
      for (map<string, counter_data>::iterator it = ctrs.begin();
          it != ctrs.end(); ++it)
        cerr << it->first << ": " << it->second << endl;
      cerr << "--- perf counters (if enabled, for benchmark) ---" << endl;
      PERF_EXPR(scopedperf::perfsum_base::printall());
      cerr << "--- allocator stats ---" << endl;
      ::allocator::DumpStats();
      cerr << "---------------------------------------" << endl;

#ifdef USE_JEMALLOC
      cerr << "dumping heap profile..." << endl;
      mallctl("prof.dump", NULL, NULL, NULL, 0);
      cerr << "printing jemalloc stats..." << endl;
      malloc_stats_print(write_cb, NULL, "");
#endif
#ifdef USE_TCMALLOC
      HeapProfilerDump("before-exit");
#endif
    }

    // output for plotting script
    cout << policies[run_count] << ":" << endl;
    cout << "RESULT "
        << "agg_throughput(" << agg_throughput << "),"
        << "agg_persist_throughput(" << agg_persist_throughput << "),"
        << "avg_latency_ms(" << avg_latency_ms << "),"
        << "avg_persist_latency_ms(" << avg_persist_latency_ms << "),"
        << "agg_abort_throughput(" << agg_abort_throughput << "),"
        << "agg_abort_rate(" << agg_abort_rate << "),"
        << "0" <<endl;
        
    cout.flush();
  }
  if (!slow_exit)
    return;

  map<string, uint64_t> agg_stats;
  for (map<string, abstract_ordered_index *>::iterator it = open_tables.begin();
       it != open_tables.end(); ++it) {
    map_agg(agg_stats, it->second->clear());
    delete it->second;
  }
  if (verbose) {
    for (auto &p : agg_stats)
      cerr << p.first << " : " << p.second << endl;

  }
  open_tables.clear();

  delete_pointers(workers);
  delete_pointers(loaders);
}

template <typename K, typename V>
struct map_maxer {
  typedef map<K, V> map_type;
  void
  operator()(map_type &agg, const map_type &m) const
  {
    for (typename map_type::const_iterator it = m.begin();
        it != m.end(); ++it)
      agg[it->first] = std::max(agg[it->first], it->second);
  }
};

//template <typename KOuter, typename KInner, typename VInner>
//struct map_maxer<KOuter, map<KInner, VInner>> {
//  typedef map<KInner, VInner> inner_map_type;
//  typedef map<KOuter, inner_map_type> map_type;
//};

#ifdef ENABLE_BENCH_TXN_COUNTERS
void
bench_worker::measure_txn_counters(void *txn, const char *txn_name)
{
  auto ret = db->get_txn_counters(txn);
  map_maxer<string, uint64_t>()(local_txn_counters[txn_name], ret);
}
#endif

map<string, size_t>
bench_worker::get_txn_counts() const
{
  map<string, size_t> m;
  const workload_desc_vec workload = get_workload();
  for (size_t i = 0; i < txn_counts.size(); i++)
    m[workload[i].name] = txn_counts[i];
  return m;
}

map<string, size_t>
bench_worker::get_abort_counts() const
{
  map<string, size_t> m;
  const workload_desc_vec workload = get_workload();
  for (size_t i = 0; i < abort_counts.size(); i++)
    m[workload[i].name] = abort_counts[i];
  return m;
}