#ifndef MYSQL_CLIENT_WITH_EPOLL_H__
#define MYSQL_CLIENT_WITH_EPOLL_H__

#include <string>
#include <map>
#include <deque>
#include <atomic>
#include <memory>
#include "my_result.h"

struct MYSQL;
struct MYSQL_RES;
class query_result;
typedef void (*query_cb)(const my_multi_result& multi_result);

struct mysql_conn {
    enum conn_status {
        INIT = 0,
        READY = 1,
        QUERYING = 2,
        RESULTING = 3,
        NEXTING = 4,
        CALLBACKING = 5,
    };

    MYSQL *mysql;
    int id;
    conn_status status;//0 初始化 //1连接完成 2 querying 3 resulting, 4 error
    int query_cnt;
    std::string sql;
    my_multi_result *result;
    query_cb cb;
    MYSQL_RES *tmp_res;
    mysql_conn(MYSQL* arg_mysql, int arg_id)
        : mysql(arg_mysql)
        , id(arg_id)
        , status(conn_status::INIT)
        , query_cnt(0)
        , result(new my_multi_result)
        , cb(nullptr)
        , tmp_res(nullptr)
    {

    }

    ~mysql_conn(){
        delete result;
    }

    void notify_result(){
        cb(*result);
        result->clear();
    }
};

class query_result: std::enable_shared_from_this<query_result>{
public:
    query_result(MYSQL_RES* result)
        : result_(result)
    {

    }

    ~query_result(){
        if(result_){
            //free
        }
    }
private:
    MYSQL_RES *result_;
};


class mysql_client_with_epoll {
public:
    mysql_client_with_epoll(const char *host, const char *user, const char *passwd, const char *db, unsigned int port, int maxsize, int keepsize, int epoll_fd);
    //std::shared_ptr<query_result> 

    void add_conn();
    void del_conn(mysql_conn *conn);
    void auto_expand_conn();

    void query(const std::string& sql, query_cb cb);

    void do_real_connect(mysql_conn *conn);
    void do_all_real_connect_continue();
    void on_real_connect_error(mysql_conn *conn);
    void on_real_connect_ingress(mysql_conn *conn);
    void on_real_connect_finish(mysql_conn *conn);

    void do_real_query(mysql_conn *conn);
    void do_real_query_continue(mysql_conn *conn);
    void on_real_query_error(mysql_conn *conn);
    void on_real_query_ingress(mysql_conn *conn);
    void on_real_query_finish(mysql_conn *conn);

    void do_store_result(mysql_conn *conn);
    void on_store_result_error(mysql_conn *conn);
    void on_store_result_ingress(mysql_conn *conn);
    void on_store_result_finish(mysql_conn *conn);

    void do_next_result(mysql_conn *conn);    
    void do_next_result_continue(mysql_conn *conn);
    void on_next_result_error(mysql_conn *conn);
    void on_next_result_ingress(mysql_conn *conn);
    void on_next_result_finish(mysql_conn *conn);

    void on_all_result_finish(mysql_conn *conn);
    void on_some_query_error(mysql_conn *conn);

    void run_loop();


private:
    std::atomic<int> id_gen_;
    const std::string config_host_;
    const std::string config_user_;
    const std::string config_passwd_;
    const std::string config_db_;
    const unsigned config_port_;
    const int config_max_size_;
    const int config_keep_size_;

    const int epoll_fd_;
    std::deque<mysql_conn*> all_connected_conns_;
    std::deque<mysql_conn*> all_connecting_conns_;
    std::map<int, mysql_conn*> all_querying_conns_;

    std::deque<std::pair<std::string, query_cb>> waiting_tasks_;
};
#endif