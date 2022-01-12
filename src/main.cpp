#include "mysql_client_with_epoll.h"

#include <functional>
#include <chrono>
#include <signal.h>
#include <gperftools/profiler.h>



std::string sql = "update ddns_log_temp set id = 0 where id = 1;select * from ddns_log where id > 100000";
//std::string sql = "select * from ddns_log where id > 100000";
mysql_client_with_epoll *gpoll = nullptr;


int total_cnt = 0;
std::chrono::time_point<std::chrono::steady_clock> last_ts;
std::chrono::time_point<std::chrono::steady_clock> init_ts;



void on_query_cb(const my_multi_result& multi_result)
{
	//return;
	//printf("query finished\n");
	assert(multi_result.get_result_count() == 2);
	auto &result0 = multi_result.get_result(0);
	assert(!result0.has_result_set());

	auto &result1 = multi_result.get_result(1);
	assert(result1.has_result_set());

	gpoll->query(sql, on_query_cb);

	++total_cnt;
	if(total_cnt % 1000 == 0) {
		std::chrono::time_point<std::chrono::steady_clock> ts = std::chrono::steady_clock::now();

		//int current_speed = 
		int ms = std::chrono::duration_cast<std::chrono::milliseconds>(ts - last_ts).count();
		int speed = (double)1000 * 1000 / ms; 

		int avg_speed = (double)total_cnt * 1000 / std::chrono::duration_cast<std::chrono::milliseconds>(ts - init_ts).count();

		printf("%d, cost ms = %d cur_speed = %d avg_speed = %d\n", total_cnt, ms, speed, avg_speed);

		last_ts = ts;
	}
}

void handle(int sig) {
	assert(sig == SIGINT);
	ProfilerStop();
	std::exit(0);
}


int main(int argc, char* argv[]){
	ProfilerStart("test_capture.prof");




	int default_cnt = 1;

	if(argc == 2){
		if(std::atoi(argv[1]) > 0) {
			default_cnt = std::atoi(argv[1]);
			
		}
	}

	mysql_client_with_epoll pool("127.0.0.1", "root", "123456", "MyLog", 3306, default_cnt, default_cnt, -1);
	gpoll = &pool;
	//std::string sql = "select * from ddns_log where id > 100000;select * from ddns_log where id > 10000;";

	init_ts = std::chrono::steady_clock::now();
	last_ts = init_ts;

	printf("default_cnt is set to %d\n", default_cnt);
	for(int i = 0; i < default_cnt; ++i){
		pool.query(sql, on_query_cb);
	}

	auto register_ret = signal(SIGINT, handle);
	assert(register_ret != SIG_ERR);
	pool.run_loop();
	return 0;
}