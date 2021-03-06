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
#include "logkafka/rotate_handler.h"

#include "base/common.h"

namespace logkafka {

bool RotateHandler::init(string path,
                         void *rotate_func_arg,
                         RotateFunc on_rotate)
{
    m_path = path;
    m_rotate_func_arg = rotate_func_arg;
    m_rotate_func = on_rotate;
    m_inode = INO_NONE;
    m_fsize = -1;

    return true;
}

void RotateHandler::onNotify(void *arg)
{
    RotateHandler *rh = (RotateHandler *)arg;
    ScopedLock l(rh->m_rotate_handler_mutex);

    if (NULL == rh) {
        LERROR << "rotate handler is NULL";
        return;
    }

    if (NULL == rh->m_rotate_func) {
        LERROR << "rotate handler callback function is NULL";
        return;
    }

    FILE *file = NULL;

    struct stat buf;
    off_t fsize;
    ino_t inode;
    if (0 == stat(rh->m_path.c_str(), &buf)) {
        fsize = buf.st_size;
        inode = buf.st_ino;
    } else {
        fsize = 0;
        inode = INO_NONE;
    }

    if (rh->m_inode != inode || fsize < rh->m_fsize) {
        LDEBUG << "Try to open file " << rh->m_path;
        file = fopen(rh->m_path.c_str(), "r");
        if (file == NULL) {
            LERROR << "Fail to open file " << rh->m_path;
            return;
        }

        (*rh->m_rotate_func)(rh->m_rotate_func_arg, file);
        file = NULL;
    }

    rh->m_inode = inode;
    rh->m_fsize = fsize;

    if (NULL != file) {
        fclose(file); file = NULL;
    }
}


} // namespace logkafka
