#cheat note:
# $@ target file
# $^ all depends file
# $< first depends file
################################# CONFIG AREA #############################

target := test_mysql_pool

INCDIR := ./3rd/hiredis \
./3rd/mysql-server/include \
./3rd/mysql-server-build/include \
./3rd/gperftools/src


#CXXFLAGS := -Wall -Wno-unused-variable -std=c++20 -g -O0 -DSHOW_DEBUG_PRINTF -pthread $(addprefix -I, $(INCDIR))
CXXFLAGS := -Wall -Wno-unused-variable -std=c++20 -O2 -pthread $(addprefix -I, $(INCDIR))

LDFLAGS := -lssl -lcrypto -lunwind -ldl -pthread -Wl,-rpath=.

libs := 3rd/mysql-server-build/archive_output_directory/libmysqlclient.a \
3rd/gperftools/.libs/libprofiler.a

so := 

JOBS := $(shell nproc)

MAKEFLAGS := --jobs=$(JOBS)

srcs := $(wildcard src/*.cpp)


###########################################################################
.PHONY: clean

depends := $(patsubst %.cpp, %.d, $(srcs))
objects := $(patsubst %.cpp, %.o, $(srcs))

$(target): $(libs) $(objects)
	$(CXX) $(objects) $(libs) $(LDFLAGS) $(so) -o $@

$(depends): %.d: %.cpp $(libs)
	@rm -fr $@
	@echo build dep $<
	@$(CXX) $(CXXFLAGS) -MM $< -MT $(<:.cpp=.o) -MF $@

clean:
	rm -fr $(depends) $(objects) $(target)

3rd/mysql-server-build/archive_output_directory/libmysqlclient.a:submodule_init
	test ! -d 3rd/mysql-server-build && \
mkdir -p 3rd/mysql-server-build || true
	test ! -e 3rd/mysql-server-build/boost_1_73_0.tar.bz2 && \
proxychains wget https://boostorg.jfrog.io/artifactory/main/release/1.73.0/source/boost_1_73_0.tar.bz2 -O 3rd/mysql-server-build/boost_1_73_0.tar.bz2 \
|| true
	until [[ `md5sum  3rd/mysql-server-build/boost_1_73_0.tar.bz2  | cut -d ' ' -f1` = "9273c8c4576423562bbe84574b07b2bd"  ]];do :;done;
	test ! -e 3rd/mysql-server-build/boost_1_73_0 && { cd 3rd/mysql-server-build && tar xvf boost_1_73_0.tar.bz2; } || true
	test ! -e 3rd/mysql-server-build/Makefile && { cd 3rd/mysql-server-build && cmake -DWITH_DEBUG=1 -DDOWNLOAD_BOOST=1 -DWITH_BOOST=./boost_1_73_0 ../mysql-server; } || true
	test ! -e 3rd/mysql-server-build/archive_output_directory/libmysqlclient.a && { cd 3rd/mysql-server-build && cmake --build . -t mysqlclient --config Debug -j; } || true

3rd/gperftools/.libs/libprofiler.a:submodule_init
	@test ! -e 3rd/gperftools/Makefile && { cd 3rd/gperftools && ./autogen.sh && ./configure;} || true
	@test ! -e 3rd/gperftools/.libs/libprofiler.a && { cd 3rd/gperftools && make -j; } || true

pdf:
	3rd/gperftools/src/pprof --pdf  ./test_mysql_pool test_capture.prof >capture.pdf

text:
	3rd/gperftools/src/pprof --text  ./test_mysql_pool test_capture.prof >capture.txt

submodule_init:
	@if git submodule status | egrep -q '^[-]|^[+]' ; then\
		echo "INFO: Need to reinitialize git submodules";\
		git submodule update --init;\
	fi
	

ifneq ($(MAKECMDGOALS), clean)
 include $(depends)
endif

