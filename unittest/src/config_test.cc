#define protected public
#define private public
#include "logkafka/config.h"
#undef protected
#undef private
#include "gtest/gtest.h"
#include <sys/file.h>

using namespace logkafka;

class ConfigTest: public ::testing::Test {
protected:
    ConfigTest() {
    }

    virtual ~ConfigTest() {
    }
    
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

public:
    static bool createFile(char* filename, string content);

};

bool ConfigTest::createFile(char* filename, string content) {
    if(-1 == mkstemp(filename)) {
        fprintf(stderr, "generate unique filename fail!\n");
        return false;
    }

    FILE* res_stream = fopen(filename, "a");
    if (res_stream == NULL) {
        fprintf(stderr, "%s open fail!\n", filename);
        return false;
    }

    flock(fileno(res_stream), LOCK_EX);
    fprintf(res_stream, "%s", content.c_str());
    flock(fileno(res_stream), LOCK_UN);

    if (res_stream != NULL) {
        fclose(res_stream);
        res_stream = NULL;
    }
}

TEST_F (ConfigTest, TestReadConfString) {

    char filename[255] = "/tmp/logkafka_test.confXXXXXX";
    ConfigTest::createFile(filename, 
            "zk_urls = 127.0.0.1:2181\n" \
            "pos_path = /tmp/pos.logkafka_test\n" \
            "line_max_bytes = 1048576\n" \
            "stat_silent_max_ms = 10000\n" \
            "zookeeper_upload_interval = 10000\n" \
            "refresh_interval = 30000\n" \
            );

    Config config;
    config.init(filename);
    unlink(filename);

    EXPECT_STREQ("127.0.0.1:2181", config.zk_urls.c_str());
}
