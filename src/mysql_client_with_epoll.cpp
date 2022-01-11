#include "mysql_client_with_epoll.h"
#include <sys/epoll.h>
#include <mysql.h>
#include <violite.h>
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <errno.h>
#include <string.h>

#include<chrono>
#include<sstream>
#include<iomanip>
std::string time_in_HH_MM_SS_MMM()
{
    using namespace std::chrono;

    // get current time
    auto now = system_clock::now();

    // get number of milliseconds for the current second
    // (remainder after division into seconds)
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    // convert to std::time_t in order to convert to std::tm (broken time)
    auto timer = system_clock::to_time_t(now);

    // convert to broken time
    std::tm bt = *std::localtime(&timer);

    std::ostringstream oss;

    oss << std::put_time(&bt, "%H:%M:%S"); // HH:MM:SS
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}


//#define DEBUG_PRINTF(fmt, ...) printf("[%s] [%s:%d] " fmt, time_in_HH_MM_SS_MMM().c_str(), __FILE__, __LINE__, ##__VA_ARGS__)

#define DEBUG_PRINTF(fmt, ...)

mysql_client_with_epoll::mysql_client_with_epoll(const char *host, const char *user, const char *passwd,
    const char *db, unsigned int port, int maxsize, int epoll_fd)
    : id_gen_(0)
    , config_host_(host)
    , config_user_(user)
    , config_passwd_(passwd)
    , config_db_(db)
    , config_port_(port)
    , epoll_fd_(epoll_fd == -1 ? epoll_create(1) : epoll_fd)
{
    //epoll_create failed
    if(epoll_fd == -1 && epoll_fd_ == -1){
        int sys_err = errno;
        assert(false);
    }

    for(int i = 0; i < maxsize; ++i){
        add_conn();
    }
}

void mysql_client_with_epoll::add_conn()
{
    int id = id_gen_++;
    MYSQL *mysql = mysql_init(nullptr);
    assert(mysql != nullptr);
    mysql_conn *conn = new mysql_conn(mysql, id);

    do_real_connect(conn);
}

void mysql_client_with_epoll::del_conn(mysql_conn *conn){
    if(conn->status == mysql_conn::conn_status::INIT){//in all_connecting_conns_
        mysql_close(conn->mysql);
        // auto iter = std::find_if(all_connecting_conns_.begin(), all_connecting_conns_.end(), [conn](mysql_conn*e){
        //     return e->id == conn->id;
        // });
        // assert(iter != all_connecting_conns_.end());
        // all_connecting_conns_.erase(iter);
        delete conn;
        //all_connecting_conns_ aleady erased
    }
    else if(conn->status == mysql_conn::conn_status::READY){
        int err = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, conn->mysql->net.vio->mysql_socket.fd, nullptr);
        assert(err == 0);
        mysql_close(conn->mysql);
        assert(false);
    }
}

void mysql_client_with_epoll::auto_expand_conn()
{
    int expand_cnt = (int)waiting_tasks_.size() - (int)all_connected_conns_.size();
    if(expand_cnt > 0) {
        for(int i = 0; i < expand_cnt; ++i) {
            add_conn();
        }
    }
}

//std::shared_ptr<query_result> 
void mysql_client_with_epoll::query(const std::string& sql, query_cb cb)
{
    if(all_connected_conns_.empty()){
        waiting_tasks_.emplace_back(std::pair(sql, cb));
        return;
    }

    mysql_conn* conn = all_connected_conns_.front();
    all_connected_conns_.pop_front();

    assert(conn->status == mysql_conn::conn_status::READY);

    ++conn->query_cnt;
    conn->sql = std::move(sql);
    conn->cb = cb;

    do_real_query(conn);
}

void mysql_client_with_epoll::do_real_connect(mysql_conn *conn)
{
    net_async_status status = mysql_real_connect_nonblocking(conn->mysql, config_host_.c_str(), config_user_.c_str(), config_passwd_.c_str(), config_db_.c_str(), config_port_, nullptr, CLIENT_MULTI_STATEMENTS);

    if(status == net_async_status::NET_ASYNC_NOT_READY){
        all_connecting_conns_.emplace_back(conn);
        DEBUG_PRINTF("id = %d, INIT\n", conn->id);
    } else if(status == net_async_status::NET_ASYNC_ERROR){
        on_real_connect_error(conn);
    } else {
        assert(false);
    }
}

void mysql_client_with_epoll::do_all_real_connect_continue()
{
    for(auto iter = all_connecting_conns_.begin(); iter != all_connecting_conns_.end(); ){
        mysql_conn *conn = *iter;

        //DEBUG_PRINTF("retry async connect for mysqlid %d\n", conn->id);
        net_async_status status = mysql_real_connect_nonblocking(conn->mysql, nullptr, nullptr, nullptr, nullptr, 0, nullptr, 0);
        if(status == NET_ASYNC_COMPLETE){            
            on_real_connect_finish(conn);
            iter = all_connecting_conns_.erase(iter);
        } else if(status == NET_ASYNC_NOT_READY) {
            on_real_connect_ingress(conn);
            ++iter;
        } 
        else if(status == NET_ASYNC_ERROR){
            on_real_connect_error(conn);
            iter = all_connecting_conns_.erase(iter);
        }
        else{
            assert(false);
        }            
    }
}

void mysql_client_with_epoll::on_real_connect_error(mysql_conn *conn)
{
    DEBUG_PRINTF("conn error mysql_errno:%d,mysql_error:%s\n",  mysql_errno(conn->mysql), mysql_error(conn->mysql));
    del_conn(conn);
}

void mysql_client_with_epoll::on_real_connect_ingress(mysql_conn *conn)
{

}

void mysql_client_with_epoll::on_real_connect_finish(mysql_conn *conn)
{
    assert(conn->status == mysql_conn::conn_status::INIT);
    conn->status = mysql_conn::conn_status::READY;
    all_connected_conns_.emplace_back(conn);
    DEBUG_PRINTF("id = %d, INIT -> READY\n", conn->id);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
    ev.data.fd = conn->id;

    //assert(conn->mysql->net.vio->mysql_socket.fd == conn->mysql->connector_fd);
    int err = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn->mysql->net.vio->mysql_socket.fd, &ev);
    assert(err == 0);
}

void mysql_client_with_epoll::do_real_query(mysql_conn *conn)
{
    conn->status = mysql_conn::conn_status::QUERYING;
    all_querying_conns_.emplace(conn->id, conn);

    DEBUG_PRINTF("id = %d, READY -> QUERYING, query_cnt = %d, sql = %s\n", conn->id, conn->query_cnt, conn->sql.c_str());
    net_async_status status = mysql_real_query_nonblocking(conn->mysql, conn->sql.c_str(), conn->sql.length());

    if(status == net_async_status::NET_ASYNC_COMPLETE){
        on_real_query_finish(conn);
    } else if(status == net_async_status::NET_ASYNC_NOT_READY){
        //on_real_query_ingress(conn);
    } else if(status == NET_ASYNC_ERROR){
        on_real_query_error(conn);
    } else {
        assert(false);
    }
}

void mysql_client_with_epoll::do_real_query_continue(mysql_conn *conn)
{
    net_async_status status = mysql_real_query_nonblocking(conn->mysql, nullptr, 0);
    if(status == net_async_status::NET_ASYNC_COMPLETE){
        on_real_query_finish(conn);
    }
    else if(status == net_async_status::NET_ASYNC_NOT_READY){
        on_real_query_ingress(conn);
    } 
    else if(status == NET_ASYNC_ERROR){
        on_real_query_error(conn);
    }
    else{
         assert(false);
    }
}

void mysql_client_with_epoll::on_real_query_error(mysql_conn *conn)
{
    assert(conn->status == mysql_conn::conn_status::QUERYING);
    conn->status = mysql_conn::conn_status::READY;

    DEBUG_PRINTF("id = %d, QUERYING -> READY, query_cnt = %d, sql = %s, query error. mysql_errno:%d,mysql_error:%s\n", 
        conn->id, 
        conn->query_cnt, 
        conn->sql.c_str(), 
        mysql_errno(conn->mysql), 
        mysql_error(conn->mysql));

    conn->result->multi_data.emplace_back(my_result());
    auto &result = conn->result->multi_data.back();
    result.error_code = mysql_errno(conn->mysql);
    result.error_str =  mysql_error(conn->mysql);

    conn->notify_result();

    all_querying_conns_.erase(conn->id);
    all_connected_conns_.push_back(conn);
}

void mysql_client_with_epoll::on_real_query_ingress(mysql_conn *conn)
{
    //
    assert(false);
}

void mysql_client_with_epoll::on_real_query_finish(mysql_conn *conn)
{
    assert(conn->status == mysql_conn::conn_status::QUERYING);
    conn->status = mysql_conn::conn_status::RESULTING;
    DEBUG_PRINTF("id = %d, QUERYING -> RESULTING\n", conn->id);
    do_store_result(conn);
}

void mysql_client_with_epoll::do_store_result(mysql_conn *conn)
{
    auto &result = conn->tmp_res;
    net_async_status status = mysql_store_result_nonblocking(conn->mysql, &result);

    if(status == net_async_status::NET_ASYNC_COMPLETE){
        on_store_result_finish(conn);
    }
    else if(status == NET_ASYNC_NOT_READY){
        on_store_result_ingress(conn);
    }
    else if(status == NET_ASYNC_ERROR){
        on_store_result_error(conn);
    }
    else{
        assert(false);
    }
}

void mysql_client_with_epoll::on_store_result_error(mysql_conn *conn)
{
    assert(false);
}

void mysql_client_with_epoll::on_store_result_ingress(mysql_conn *conn)
{
    assert(false);
}

void mysql_client_with_epoll::on_store_result_finish(mysql_conn *conn)
{
    conn->result->multi_data.emplace_back(my_result());
    auto &result = conn->result->multi_data.back();

    if(conn->tmp_res == nullptr){
        //没有结果集
        int field_count = mysql_field_count(conn->mysql);
        if(field_count == 0){//INSERT UPDATE DELETE for no result set
            result.affected_rows = mysql_affected_rows(conn->mysql);
            assert(result.affected_rows != -1);
        }
        else {// ERROR
            result.error_code = mysql_errno(conn->mysql);
            result.error_str = mysql_error(conn->mysql);
            assert(false);
        }
    } else {
        result.affected_rows = mysql_affected_rows(conn->mysql);
        assert(result.affected_rows != -1);

        auto &result_data = result.data;
        my_ulonglong rows = mysql_num_rows(conn->tmp_res);

        unsigned int fileds = mysql_num_fields(conn->tmp_res);

        MYSQL_FIELD * col_name = mysql_fetch_field(conn->tmp_res);
        for (unsigned int i = 0; i < fileds; ++i)
        {
            auto exist_iter = result_data.find(col_name[i].name);
            assert(exist_iter == result_data.end());								//do not support same name;

            result_data.emplace(col_name[i].name, std::vector<std::string>());
            result_data.at(col_name[i].name).reserve(rows);
        }

        for (my_ulonglong i = 0; i < rows; ++i)
        {
            MYSQL_ROW row = mysql_fetch_row(conn->tmp_res);										//
            assert(row != nullptr);

            for (unsigned int j = 0; j < fileds; ++j)
            {
                auto &v = result_data.at(col_name[j].name);
                v.emplace_back(std::string(row[j]));
            }
        }
        mysql_free_result(conn->tmp_res);
    }

    int next_status = mysql_next_result(conn->mysql);

    if(next_status == -1){
        assert(conn->status == mysql_conn::conn_status::RESULTING);
        conn->status = mysql_conn::conn_status::CALLBACKING;
        DEBUG_PRINTF("id = %d, RESULTING -> CALLBACKING \n", conn->id);
        conn->notify_result();
        conn->status = mysql_conn::conn_status::READY;
        DEBUG_PRINTF("id = %d, CALLBACKING -> READY \n", conn->id);

        auto iter = all_querying_conns_.find(conn->id);
        assert(iter != all_querying_conns_.end());
        all_querying_conns_.erase(iter);
        all_connected_conns_.emplace_back(conn);

    } else if(next_status == 0){
        do_store_result(conn);
    }
    else{
        DEBUG_PRINTF("id = %d, mysql_next_result for error. mysql_errno:%d,mysql_error:%s\n", 
            conn->id,
            mysql_errno(conn->mysql),
            mysql_error(conn->mysql));
        assert(false);
    }
}

void mysql_client_with_epoll::run_loop()
{
    const int ev_size = 1024;
    struct epoll_event evs[ev_size];

    while(true) {
        do_all_real_connect_continue();

        auto_expand_conn();

        int epoll_wait_ms = all_connecting_conns_.empty() ? -1 : 1;

        while(!waiting_tasks_.empty() && !all_connected_conns_.empty()){
            auto &task = waiting_tasks_.front();
            query(task.first, task.second);
            waiting_tasks_.pop_front();
        }

        //DEBUG_PRINTF("begin epoll_wait with for time %dms\n", epoll_wait_ms);
        int num_evs = epoll_wait(epoll_fd_, evs, ev_size, epoll_wait_ms);
        if(num_evs == -1){
            int sys_error = errno;
            DEBUG_PRINTF("error_name:%s, error_desc:%s\n", strerrorname_np(sys_error), strerrordesc_np(sys_error));
        }else{
            if(num_evs == 0){
                //do timer trigger
            }

            //DEBUG_PRINTF("after epoll_wait num_evs:%d\n", num_evs);
            for(int i = 0; i < num_evs; ++i){
                int events = evs[i].events;
                int id = evs[i].data.fd;

                assert(events == EPOLLIN);
                //DEBUG_PRINTF("id = %d, event = %x \n", id, events);

                auto iter = all_querying_conns_.find(id);
                assert(iter != all_querying_conns_.end());
                
                mysql_conn *conn = iter->second;
                if(conn->status == mysql_conn::conn_status::QUERYING){
                    do_real_query_continue(conn);
                }
   
                else{
                    assert(false);
                }
            }
        }
    }
}