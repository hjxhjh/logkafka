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
#include "logkafka/zookeeper.h"

#include <cstdlib>
#include <sstream>

#include "base/tools.h"
#include "logkafka/producer.h"

#include "easylogging/easylogging++.h"

using namespace base;

namespace logkafka {

const string Zookeeper::BROKER_IDS_PATH = "/brokers/ids";
const unsigned long Zookeeper::REFRESH_INTERVAL_MS = 30000UL;
const string Zookeeper::LOGKAFKA_CONFIG_PATH = "/logkafka/config/";
const string Zookeeper::LOGKAFKA_CLIENT_PATH = "/logkafka/client/";

Zookeeper::Zookeeper()
{/*{{{*/
    m_zhandle = NULL;
    m_loop = NULL;
    m_zk_log_fp = NULL;
    m_refresh_timer_trigger = NULL;
    m_thread = NULL;

    m_log_config = "{}";
    m_broker_urls = "";
}/*}}}*/

Zookeeper::~Zookeeper()
{/*{{{*/
    delete m_loop; m_loop = NULL;
    delete m_zk_log_fp; m_zk_log_fp = NULL;
    delete m_refresh_timer_trigger; m_refresh_timer_trigger = NULL;
    delete m_thread; m_thread = NULL;
}/*}}}*/

bool Zookeeper::init(const string &zk_urls, long refresh_interval)
{/*{{{*/
    m_zk_urls = zk_urls;
    m_zk_log_fp = fopen("/dev/null", "w");
    zoo_set_log_stream(m_zk_log_fp);

    char buf[256] = {};
    if (0 == gethostname(buf, sizeof(buf) -1)) {
        m_hostname = buf;
    } else {
        LERROR << "Fail to get hostname";
        return false;
    }

    m_client_path = LOGKAFKA_CLIENT_PATH + m_hostname;
    m_config_path = LOGKAFKA_CONFIG_PATH + m_hostname;

    refresh((void *)this);

    /* Use one zookeeper loop for all watchers */
    m_loop = new uv_loop_t();
    int res = uv_loop_init(m_loop);
    if (res < 0) {
        LERROR << "Fail to init uv loop, " << uv_strerror(res);
        return false;
    }

    /* The existence of the async handle will keep the loop alive. */  
    m_exit_handle.data = this;
    uv_async_init(m_loop, &m_exit_handle, exitAsyncCb);

    m_refresh_timer_trigger = new TimerWatcher();
    if (!m_refresh_timer_trigger->init(m_loop, 0,
            refresh_interval,
            this, &refresh)) {
        LERROR << "Fail to init refresh timer";
        delete m_refresh_timer_trigger; m_refresh_timer_trigger = NULL;
        return false;
    }
 
    /* Run zookeeper loop in another thread */
    m_thread = new uv_thread_t();
    uv_thread_create(m_thread, &threadFunc, m_loop);

    return true;
}/*}}}*/

void Zookeeper::threadFunc(void *arg)
{/*{{{*/
    uv_loop_t *loop = reinterpret_cast<uv_loop_t *>(arg);
    int res = uv_run(loop, UV_RUN_DEFAULT);
    if (res < 0) {
        LERROR << "Fail to run loop, " << uv_strerror(res);
    }
}/*}}}*/

bool Zookeeper::connect()
{/*{{{*/
    if (NULL != m_zhandle) {
        LINFO << "Close invaild zookeeper connection...";
        zookeeper_close(m_zhandle);
        m_zhandle = NULL;
    }

    LDEBUG << "Try to init zhandle";
    m_zhandle = zookeeper_init(m_zk_urls.c_str(), 
            globalWatcher, 30000, NULL, (void*)this, 0);
    if (NULL == m_zhandle) {
        LERROR << "Fail to init zhandle, zookeeper urls " << m_zk_urls;
        return false;
    }

    return true;
}/*}}}*/

void Zookeeper::close()
{/*{{{*/
    m_refresh_timer_trigger->close();

    uv_stop(m_loop);
    uv_async_send(&m_exit_handle);

    ScopedLock lk(m_zhandle_mutex);
    if (NULL != m_zhandle) {
        zookeeper_close(m_zhandle);
        m_zhandle = NULL;
    }

    if (NULL != m_zk_log_fp) {
        fclose(m_zk_log_fp); 
        m_zk_log_fp = NULL;
    }
}/*}}}*/

void Zookeeper::exitAsyncCb(uv_async_t* handle) 
{/*{{{*/
    /* After closing the async handle, it will no longer keep the loop alive. */
    Zookeeper *zookeeper = reinterpret_cast<Zookeeper *>(handle->data);
    uv_close((uv_handle_t*) &zookeeper->m_exit_handle, NULL);
} /*}}}*/

void Zookeeper::refresh(void *arg)
{/*{{{*/
    if (NULL == arg) {
        LERROR << "Zookeeper refresh function arg is NULL";
        return;
    }

    Zookeeper *zookeeper = reinterpret_cast<Zookeeper *>(arg);

    if (!zookeeper->refreshConnection()) {
        LERROR << "Fail to refresh zookeeper connection";
        return;
    }

    if (!zookeeper->refreshBrokerUrls()) {
        LERROR << "Fail to refresh broker urls";
    }

    if (!zookeeper->refreshWatchers()) {
        LERROR << "Fail to refresh zookeeper watchers";
    }

    if (!zookeeper->refreshLogConfig()) {
        LERROR << "Fail to refresh log config";
    }
}/*}}}*/

bool Zookeeper::refreshConnection()
{/*{{{*/
    ScopedLock l(m_zhandle_mutex);

    int res = zoo_state(m_zhandle);
    if (NULL == m_zhandle || ZOK != res) {
        if (!connect()) {
            LERROR << "Fail to reset zookeeper connection";
            return false;
        }
    }

    return true;
}/*}}}*/

bool Zookeeper::refreshWatchers()
{/*{{{*/
    ScopedLock l(m_zhandle_mutex);

    /* set config change watcher */
    if (!setWatcher(m_config_path, configChangeWatcher, (void*)this)) {
        LERROR << "Fail to set config change watcher";
        return false;
    }

    /* set broker change watcher */
    if (!setChildrenWatcher(BROKER_IDS_PATH, brokerChangeWatcher, (void*)this)) {
        LERROR << "Fail to set broker change watcher";
        return false;
    }

    /* create EPHEMERAL node for checking whether logkafka is alive */
    ensurePathExist(m_client_path);    
    /* if lost connection to zk and reconnect to it, this ephemeral node may still exist */
    zoo_delete(m_zhandle, m_client_path.c_str(), -1);
    if (zoo_create(m_zhandle, m_client_path.c_str(), NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 
                ZOO_EPHEMERAL, NULL, 0) != ZOK)
    {
        LERROR << "Fail to create zookeeper path, " << m_client_path;
        return false;
    }

    return true;
}/*}}}*/

bool Zookeeper::refreshLogConfig()
{/*{{{*/
    ScopedLock l(m_log_config_mutex);

    string log_config;
    if (!getZnodeData(m_config_path, log_config)) {
        LERROR << "Fail to get log config";
        return false;
    }

    m_log_config = log_config;

    return true;
}/*}}}*/

bool Zookeeper::refreshBrokerUrls()
{/*{{{*/
    ScopedLock l(m_broker_urls_mutex);

    vector<string> ids;
    if (!getBrokerIds(ids)) {
        return false;
    }

    vector<string>::const_iterator iter;
    for (iter = ids.begin(); iter != ids.end(); ++iter) {
        string host, port;
        string bid = *iter; 
        if (!getBrokerIpAndPort(bid, host, port)) {
            return false;
        }

        if (iter != ids.begin()) {
            m_broker_urls.append(",");
        }

        m_broker_urls.append(host);
        m_broker_urls.append(":");
        m_broker_urls.append(port);
    }

    return true;
}/*}}}*/

string Zookeeper::getLogConfig()
{/*{{{*/
    ScopedLock l(m_log_config_mutex);
    return m_log_config;
}/*}}}*/

bool Zookeeper::ensurePathExist(const string& path)
{/*{{{*/
    if (NULL == m_zhandle) {
        return false;
    }

    if (ZOK == zoo_exists(m_zhandle, path.c_str(), 0, NULL)) {
        return true;
    }

    size_t start_pos = 1;
    size_t pos = 0;
    while ((pos = path.find('/', start_pos)) != string::npos) {
        string parent_path = path.substr(0, pos);
        start_pos = pos+1;
        zoo_create(m_zhandle, parent_path.c_str(), NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    }

    int ret = zoo_create(m_zhandle, path.c_str(), NULL, 0, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
    if (ret != ZOK && ret != ZNODEEXISTS) {
        LERROR << "create znode failed: " << path.c_str()
               << ", error: " << errno2String(ret);
        return false;
    }

    return true;
}/*}}}*/

bool Zookeeper::getZnodeData(const string& path, string &data)
{/*{{{*/
    if (NULL == m_zhandle) {
        return false;
    }

    struct Stat stat;
    int status = ZOK;
    int len = 0;
    bool ret = false; 
    char *buf = NULL;

    status = zoo_exists(m_zhandle, path.c_str(), 0, &stat);
    len = (status == ZOK) ? stat.dataLength : ZNODE_BUF_MAX_LEN;

    buf = (char *)malloc(len + 1);
    bzero(buf, len + 1);
    status = zoo_get(m_zhandle, path.c_str(), 0, buf, &len, NULL);
    if (status == ZOK) {
        data = string(buf);
        ret = true;
    } else {
        LERROR << "get znode error"
               << ", path: " << path 
               << ", error:%s" << errno2String(status);
    }
    free(buf);

    return ret;
}/*}}}*/

bool Zookeeper::setWatcher(const string& path, 
        watcher_fn watcher, void *wctx)
{/*{{{*/
    LINFO << "try to set watcher " << path.c_str();

    if (NULL == m_zhandle) {
        LERROR << "zhandle is NULL";
        return false; 
    }

    if (!ensurePathExist(path))
    {
        LERROR << "create znode " << path;
        return false;
    }
    int len = 0;
    int ret;
    if (ZOK != (ret = zoo_wget(m_zhandle, path.c_str(), watcher, wctx, NULL, &len, NULL)))
    {
        LWARNING << "set watcher failed: " << path;
        return false;
    }
    LINFO << "set watcher success: " << path.c_str();
    return true;
}/*}}}*/

bool Zookeeper::setChildrenWatcher(const string& path, 
        watcher_fn watcher, void *wctx)
{/*{{{*/
    LINFO << "try to set children watcher " << path.c_str();

    if (NULL == m_zhandle) {
        return false; 
    }

    if (!ensurePathExist(path)) {
        LERROR << "Create znode " << path.c_str();
        return false;
    }

    int ret = ZOK;
    if (ZOK != (ret = zoo_wget_children(m_zhandle, path.c_str(), watcher, wctx, NULL))) {
        LWARNING << "set children watcher failed: " << path.c_str();
        return false;
    }
    LINFO << "set children watcher success: " << path.c_str();
    return true;
}/*}}}*/

const char* Zookeeper::state2String(int state)
{/*{{{*/
    if (state == 0)
        return "CLOSED_STATE";
    if (state == ZOO_CONNECTING_STATE)
        return "CONNECTING_STATE";
    if (state == ZOO_ASSOCIATING_STATE)
        return "ASSOCIATING_STATE";
    if (state == ZOO_CONNECTED_STATE)
        return "CONNECTED_STATE";
    if (state == ZOO_EXPIRED_SESSION_STATE)
        return "EXPIRED_SESSION_STATE";
    if (state == ZOO_AUTH_FAILED_STATE)
        return "AUTH_FAILED_STATE";

    return "INVALID_STATE";
}/*}}}*/

const char* Zookeeper::event2String(int ev)
{/*{{{*/
    switch (ev) {
        case 0:
            return "ZOO_ERROR_EVENT";
        case CREATED_EVENT_DEF:
            return "ZOO_CREATED_EVENT";
        case DELETED_EVENT_DEF:
            return "ZOO_DELETED_EVENT";
        case CHANGED_EVENT_DEF:
            return "ZOO_CHANGED_EVENT";
        case CHILD_EVENT_DEF:
            return "ZOO_CHILD_EVENT";
        case SESSION_EVENT_DEF:
            return "ZOO_SESSION_EVENT";
        case NOTWATCHING_EVENT_DEF:
            return "ZOO_NOTWATCHING_EVENT";
    }
    return "INVALID_EVENT";
}/*}}}*/

const char* Zookeeper::errno2String(int errnum)
{/*{{{*/
    const char* str_err = NULL;
    switch(errnum) {
        case ZNONODE:
            str_err = "the parent node does not exist";
            break;
        case ZNOAUTH:
            str_err = "the client does not have permission";
            break;
        case ZBADARGUMENTS:      
            str_err = "invalid input parameters";
            break;
        case ZBADVERSION:        
            str_err = "expected version does not match actual version";
            break;
        case ZINVALIDSTATE:      
            str_err = "zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE";
            break;
        case ZMARSHALLINGERROR:  
            str_err = "failed to marshall a request; possibly, out of memory";
            break;
        case ZNOCHILDRENFOREPHEMERALS:
            str_err = "cannot create children of ephemeral nodes";
            break;
        case ZSYSTEMERROR:       
            str_err = "System and server-side errors";
            break;
        case ZOPERATIONTIMEOUT:  
            str_err = "Operation timeout";
            break;
        case ZUNIMPLEMENTED:     
            str_err = "Operation is unimplemented";
            break;
        case ZOK:     
            str_err = "Operation is EXIT_SUCCESS";
            break;
        default:
            str_err = "unknown error";
            break;
    }
    return str_err;
}/*}}}*/

void Zookeeper::globalWatcher(zhandle_t* zhandle, int type, 
        int state, const char* path, void* context)
{/*{{{*/
    LINFO << "Watcher event: " << event2String(type)
          << ", state: " << state2String(state)
          << ", path: " << path;

    if (NULL == context) {
        LWARNING << "broker change watcher context is NULL";
        return;
    }

    Zookeeper *zookeeper = reinterpret_cast<Zookeeper *>(context);

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            LINFO << "Connect to zookeeper successfully.";
        } 

        if (state == ZOO_AUTH_FAILED_STATE) {
            LERROR << "Authentication failure, shutting down...";
        } 

        if (state == ZOO_EXPIRED_SESSION_STATE) {
            LINFO << "Session expired, try to reconnect...";
            refresh((void*)zookeeper);
        }
    }
}/*}}}*/

void Zookeeper::brokerChangeWatcher(zhandle_t* zhandle, int type, 
        int state, const char* path, void* context)
{/*{{{*/
    LINFO << "Watcher event: " << event2String(type)
          << ", state: " << state2String(state)
          << ", path: " << path;

    if (NULL == context) {
        LWARNING << "broker change watcher context is NULL";
        return;
    }

    Zookeeper *zookeeper = reinterpret_cast<Zookeeper *>(context);

    zookeeper->refreshWatchers();

    if (type != ZOO_SESSION_EVENT) { 
        zookeeper->refreshBrokerUrls();
    }
}/*}}}*/

void Zookeeper::configChangeWatcher(zhandle_t* zhandle, int type, 
        int state, const char* path, void* context)
{/*{{{*/
    LINFO << "Watcher event: " << event2String(type)
          << ", state: " << state2String(state)
          << ", path: " << path;

    if (NULL == context) {
        LWARNING << "config change watcher context is NULL";
        return;
    }

    Zookeeper *zookeeper = reinterpret_cast<Zookeeper *>(context);

    zookeeper->refreshWatchers();

    if (type != ZOO_SESSION_EVENT) { 
        zookeeper->refreshLogConfig();
    }
}/*}}}*/

bool Zookeeper::getBrokerIds(vector<string>& ids)
{/*{{{*/
    if (NULL == m_zhandle) {
        return false; 
    }

    struct String_vector brokerids;

    int ret = zoo_get_children(m_zhandle, BROKER_IDS_PATH.c_str(), 0, &brokerids);
    if (ret != ZOK) {
        LERROR << "Get children error"
               << ", path: " << BROKER_IDS_PATH
               << ", error: " << errno2String(ret);

        return false;
    } else {
        ids.clear();
        for (int i = 0; i < brokerids.count; ++i)
        {
            ids.push_back(brokerids.data[i]);
        }
        deallocate_String_vector(&brokerids);
        return true;
    }
}/*}}}*/

bool Zookeeper::getBrokerIpAndPort(const string& brokerid, 
        string& host, string& port)
{/*{{{*/
    string brokerid_info_path = BROKER_IDS_PATH + "/" + brokerid;
    string brokerinfo;
    if (!getZnodeData(brokerid_info_path, brokerinfo)) {
        LERROR << "Fail to get broker info";
        return false;
    }

    /* 1. Parse a JSON text string to a document. */
    Document document;
    if (document.Parse<0>(brokerinfo.c_str()).HasParseError()) {
        LERROR << "Json parsing failed, json: " << brokerinfo;
        return false;
    }

    /* 2. Access values in document. */
    if (!document.IsObject()) {
        LERROR << "Document is not object, type: " << Json::TypeNames[document.GetType()];
        return false;
    }

    try {
        Json::getValue(document, "host", host);

        int port_int;
        Json::getValue(document, "port", port_int);
        port = int2Str(port_int);
    } catch (const JsonErr &err) {
        LERROR << "Json error: " << err;
        return false;
    }

    return true;
}/*}}}*/

string Zookeeper::getBrokerUrls()
{/*{{{*/
    ScopedLock l(m_broker_urls_mutex);
    return m_broker_urls;
}/*}}}*/

bool Zookeeper::setLogState( const char *buf, int buflen,
        stat_completion_t completion)
{/*{{{*/
    ScopedLock l(m_zhandle_mutex);

    if (NULL == m_zhandle) {
        LWARNING << "zhandle is NULL";
        return false;
    }

    int ret = ZOK;
    const char *client_path = m_client_path.c_str();
    char *path = strndup(client_path, strlen(client_path));
    if (NULL == path) {
        LERROR << "Fail to strndup path " << client_path;
        return false;
    }

    /* NOTE: use zookeeper async set for not blocking the main loop */
    if ((ret = zoo_aset(m_zhandle, client_path, buf, buflen,
                    -1, completion, path)) != ZOK)
    {
        LERROR << "Fail to set znode, " << zerror(ret)
               << ", path: " << path
               << ", buf: " << buf
               << ", buflen: " << buflen;
        free(path);
        return false;
    }

    return true;
}/*}}}*/

} // namespace logkafka
