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
#ifndef LOGKAFKA_TAIL_WATCHER_H_
#define LOGKAFKA_TAIL_WATCHER_H_

#include <cstdlib>
#include <string>

#include "base/common.h"
#include "base/json.h"
#include "base/mutex.h"
#include "base/scoped_lock.h"
#include "base/stat_watcher.h"
#include "base/timer_watcher.h"
#include "logkafka/io_handler.h"
#include "logkafka/manager.h"
#include "logkafka/memory_position_entry.h"
#include "logkafka/output.h"
#include "logkafka/output_kafka.h"
#include "logkafka/position_entry.h"
#include "logkafka/rotate_handler.h"
#include "logkafka/task_conf.h"

using namespace std;
using namespace base;

namespace logkafka {

class Manager;

typedef void (*UpdateFunc)(Manager *, string, string, PositionEntry *);

class TailWatcher
{
    public:
        TailWatcher();
        ~TailWatcher();

        bool init(uv_loop_t *loop, 
                string path_pattern, 
                string path, 
                PositionEntry *position_entry,
                unsigned long stat_silent_max_ms,
                bool read_from_head,
                unsigned long max_line_at_once,
                unsigned long line_max_bytes, 
                bool enabled,
                UpdateFunc updateWatcher,
                ReceiveFunc receiveLines,
                TaskConf conf,
                Output *output);

        static void onNotify(void *arg);
        static void onRotate(void *arg, FILE *file);
        static PositionEntry * swapState(PositionEntry **pep, IOHandler *io_handler);

        void start();
        void stop(bool close_io);

        bool isActive();
        bool getEnabled() { return m_enabled; };
        string getPath();
        static bool isStateSilentMaxMsValid(unsigned long stat_silent_max_ms);

        /* serialize to json */
        template <typename JsonWriter>
        void Serialize(JsonWriter& writer)
        {/*{{{*/
            ScopedLock l(m_io_handler_mutex);

            string realpath = m_path;
            long filepos = -1;
            long filesize = 0;
            if (NULL != m_io_handler) {
                filepos = m_io_handler->getFilePos();
                filesize = m_io_handler->getFileSize();
            }

            // This base class just write out name-value pairs, without wrapping within an object.
            writer.StartObject();

            writer.String("realpath");
            writer.String(realpath.c_str(), (rapidjson::SizeType)realpath.length()); // Supplying length of string is faster.
            writer.String("filepos");
            writer.Int64(filepos);
            writer.String("filesize");
            writer.Int64(filesize);

            writer.EndObject();
        };/*}}}*/

    public:
        bool m_unwatched;
        string m_path_pattern;
        string m_path;
        TaskConf m_conf;

    private:
        struct event_base *m_base;
        uv_loop_t *m_loop;
        TimerWatcher *m_timer_trigger;
        StatWatcher *m_stat_trigger;
        RotateHandler *m_rotate_handler;
        IOHandler *m_io_handler;
        PositionEntry *m_position_entry;
        UpdateFunc m_updateWatcher;
        ReceiveFunc m_receive_func;
        Manager *m_manager;
        Output *m_output;
        bool m_read_from_head;
        unsigned long m_max_line_at_once;
        unsigned long m_line_max_bytes;
        unsigned long m_stat_silent_max_ms;
        bool m_enabled;

    private:
        Mutex m_io_handler_mutex;

        static const unsigned long TIMER_WATCHER_DEFAULT_REPEAT;
        static const unsigned long STAT_WATCHER_DEFAULT_INTERVAL;
};

} // namespace logkafka

#endif // LOGKAFKA_TAIL_WATCHER_H_
