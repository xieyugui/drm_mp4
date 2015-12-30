/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef _MP4_COMMON_H
#define _MP4_COMMON_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>
#include "mp4_meta.h"

//des key
static u_char *des_key = NULL;

class IOHandle
{
public:
  IOHandle() : vio(NULL), buffer(NULL), reader(NULL){};

  ~IOHandle()
  {
    if (reader) {
      TSIOBufferReaderFree(reader);
      reader = NULL;
    }

    if (buffer) {
      TSIOBufferDestroy(buffer);
      buffer = NULL;
    }
  }

public:
  TSVIO vio;
  TSIOBuffer buffer;
  TSIOBufferReader reader;
};

class Mp4TransformContext
{
public:
  Mp4TransformContext(int64_t offset, int64_t cl, u_char *des_key, bool is_n_md)
    : total(0), tail(0), pos(0), content_length(0), meta_length(0), parse_over(false), raw_transform(false)
  {
    res_buffer = TSIOBufferCreate();
    res_reader = TSIOBufferReaderAlloc(res_buffer);
    dup_reader = TSIOBufferReaderAlloc(res_buffer);

    mm.start = offset; //起始长度
    mm.cl = cl; //文件总长度
    mm.tdes_key = des_key;
    mm.is_need_md = is_n_md;
  }

  ~Mp4TransformContext()
  {
    if (res_reader) {
      TSIOBufferReaderFree(res_reader);
    }

    if (dup_reader) {
      TSIOBufferReaderFree(dup_reader);
    }

    if (res_buffer) {
      TSIOBufferDestroy(res_buffer);
    }
  }

public:
  IOHandle output;
  Mp4Meta mm; //meta data
  int64_t total;
  int64_t tail;
  int64_t pos;
  int64_t content_length;
  int64_t meta_length;

  TSIOBuffer res_buffer;
  TSIOBufferReader res_reader;
  TSIOBufferReader dup_reader;

  bool parse_over; //是否解析过
  bool raw_transform;
};

class Mp4Context
{
public:
  Mp4Context(int64_t s) : start(s), cl(0), mtc(NULL), transform_added(false){};

  ~Mp4Context()
  {
    if (mtc) {
      delete mtc;
      mtc = NULL;
    }
  }

public:
  int64_t start; //起始点
  int64_t cl; //文件总长度
  Mp4TransformContext *mtc;

  bool transform_added;
};

#endif
