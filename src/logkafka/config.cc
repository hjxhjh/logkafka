///////////////////////////////////////////////////////////////////////////
//
// logkafka - Collect logs and send lines to Apache Kafka v0.8+
//
///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2015 Qihoo 360 Technology Co., Ltd. All rights reserved.
//
// Licensed under the MIT License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://opensource.org/licenses/MIT
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////
#include "logkafka/config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "base/tools.h"
#include "logkafka/tail_watcher.h"

#include "easylogging/easylogging++.h"

using namespace std;

namespace logkafka {

Config::Config()
{/*{{{*/
    cfg_opt_t opts[] =
    {
        CFG_STR("zk_urls", DEFAULT_ZK_URLS, CFGF_NONE),
        CFG_STR("pos_path", DEFAULT_POS_PATH, CFGF_NONE),
        CFG_INT("line_max_bytes", DEFAULT_LINE_MAX_BYTES, CFGF_NONE),
        CFG_INT("stat_silent_max_ms", DEFAULT_STAT_SILENT_MAX_MS, CFGF_NONE),
        CFG_INT("zookeeper_upload_interval", DEFAULT_ZOOKEEPER_UPLOAD_INTERVAL,
                CFGF_NONE),
        CFG_INT("refresh_interval", DEFAULT_REFRESH_INTERVAL, CFGF_NONE),
        CFG_INT("message_send_max_retries", DEFAULT_MESSAGE_SEND_MAX_RETRIES,
                CFGF_NONE),
        CFG_END()
    };

    m_cfg = cfg_init(opts, CFGF_NONE);
}/*}}}*/

Config::~Config()
{/*{{{*/
    cfg_free(m_cfg);
}/*}}}*/

bool Config::init(const char* filepath)
{/*{{{*/
    int ret = cfg_parse(m_cfg, filepath);
    switch (ret) {
        case CFG_PARSE_ERROR:
            fprintf(stderr, "config file parsing error!\n");
            return false;
        case CFG_FILE_ERROR:
            fprintf(stderr, "config file error!\n");
            return false;
        default:
            break;
    };

    char realdir[PATH_MAX];
    if (NULL == realDir(filepath, realdir)) {
        fprintf(stderr, "get realdir of filepath(%s) error!\n", filepath);
        return false;
    }
    string realdir_s(realdir);
        
    zk_urls = cfg_getstr(m_cfg, "zk_urls");
    pos_path = cfg_getstr(m_cfg, "pos_path");
    line_max_bytes = cfg_getint(m_cfg, "line_max_bytes");
    stat_silent_max_ms = cfg_getint(m_cfg, "stat_silent_max_ms");
    zookeeper_upload_interval = cfg_getint(m_cfg, "zookeeper_upload_interval");
    refresh_interval = cfg_getint(m_cfg, "refresh_interval");
    message_send_max_retries = cfg_getint(m_cfg, "message_send_max_retries");

    if (!isAbsPath(pos_path.c_str())) {
        pos_path = realdir_s + '/' + pos_path;
    }

    if (line_max_bytes > HARD_LIMIT_LINE_MAX_BYTES) {
        fprintf(stderr, "line_max_bytes %lu exceeds hard limit %lu!\n",
                line_max_bytes, HARD_LIMIT_LINE_MAX_BYTES);
        return false;
    }

    if (!TailWatcher::isStateSilentMaxMsValid(stat_silent_max_ms)) {
        fprintf(stderr, "stat_silent_max_ms %lu is not valid!\n",
                stat_silent_max_ms);
        return false;
    }

    if (zookeeper_upload_interval > HARD_LIMIT_ZOOKEEPER_UPLOAD_INTERVAL) {
        fprintf(stderr, "zookeeper_upload_interval %lu exceeds hard limit %lu!\n",
                zookeeper_upload_interval, HARD_LIMIT_ZOOKEEPER_UPLOAD_INTERVAL);
        return false;
    }

    return true;
}/*}}}*/

} // namespace logkafka
