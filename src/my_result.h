#ifndef MY_RESULT_H__
#define MY_RESULT_H__

#include <cassert>
#include <string>
#include <vector>
#include <map>

class my_result
{
public:
	int affected_rows;
    int error_code;
	std::string error_str;
    std::map<std::string, std::vector<std::string>> data;

public:
	my_result();

	int get_error_code()const { return error_code; }
	const std::string& get_error_str() const { return error_str; }
	int get_affected_rows() const { return affected_rows; }

	int get_result_rows() const {
		return (int)data.begin()->second.size();
	}

    bool has_result_set() const {
        return !data.empty();
    }

	const std::string& get_value_string_ref(int row_index, const std::string& col_name) const
	{
		return data.at(col_name).at(row_index);
	}

	int64_t get_value_int64(int row_index, const std::string& col_name) const
	{
		return (int64_t)std::atoll( data.at(col_name).at(row_index).c_str());
	}

	int get_value_int(int row_index, const std::string& col_name) const
	{
		return (int)std::atoi(data.at(col_name).at(row_index).c_str());
	}

	bool get_value_bool(int row_index, const std::string& col_name) const
	{
		return (int)std::atoi(data.at(col_name).at(row_index).c_str()) != 0;
	}
};

class my_multi_result {
public:
    const my_result& get_result(int result_id) const {
		assert(result_id >= 0 && result_id < (int)multi_data.size());
		return multi_data.at(result_id);
    }
    const int get_result_count() const {
        return (int)multi_data.size();
    }

    void clear() {
        multi_data.clear();
    }
    std::vector<my_result> multi_data;
};

#endif // !
