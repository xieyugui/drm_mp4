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
 *
 * drm_mp4.cc
 *
 *  Created on: 2016年1月8日
 *      Author: xie
 *
 */

#include "mp4_context.h"

//des key
static u_char *des_key = NULL;
//need add mp4 head
static bool is_need_add_mp4_header = false;

static int mp4_handler(TSCont contp, TSEvent event, void *edata);
static void mp4_cache_lookup_complete(Mp4Context *mc, TSHttpTxn txnp);
static void mp4_read_response(Mp4Context *mc, TSHttpTxn txnp);
static void mp4_add_transform(Mp4Context *mc, TSHttpTxn txnp);
static int mp4_transform_entry(TSCont contp, TSEvent event, void *edata);
static int mp4_transform_handler(TSCont contp, Mp4Context *mc);

TSReturnCode TSRemapInit(TSRemapInterface *api_info, char *errbuf,int errbuf_size) {
	if (!api_info) {
		snprintf(errbuf, errbuf_size,"[TSRemapInit] - Invalid TSRemapInterface argument");
		return TS_ERROR;
	}

	if (api_info->size < sizeof(TSRemapInterface)) {
		snprintf(errbuf, errbuf_size,"[TSRemapInit] - Incorrect size of TSRemapInterface structure");
		return TS_ERROR;
	}

	return TS_SUCCESS;
}

// first parameter is des key , the second parameter is optional (need add mp4 head)
TSReturnCode TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size) {
	if (argc < 2) {
		TSError("[%s] Plugin not initialized, must have des key", PLUGIN_NAME);
		return TS_ERROR;
	}

	des_key = (u_char *) (TSstrdup(argv[2]));
	TSDebug(PLUGIN_NAME, "TSRemapNewInstance drm video des key is %s", des_key);

	if(des_key == NULL) {
		TSError("[%s] Plugin not initialized, must have des key", PLUGIN_NAME);
		return TS_ERROR;
	}

	if(argc > 3 && argv[3]){
		is_need_add_mp4_header = true;
	}

	*ih = NULL;
	return TS_SUCCESS;
}

void TSRemapDeleteInstance(void *) {

}


TSRemapStatus TSRemapDoRemap(void * /* ih ATS_UNUSED */, TSHttpTxn rh, TSRemapRequestInfo *rri) {
	const char *method, *path, *range, *range_separator;
	int method_len, path_len, range_len;
	int64_t start, end;
	TSMLoc ae_field, range_field, no_des_field;
	TSCont contp;
	Mp4Context *mc;

	method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
	if (method != TS_HTTP_METHOD_GET) {
		return TSREMAP_NO_REMAP;
	}

	// check suffix
	path = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);
	if (path == NULL || path_len <= 4) {
		return TSREMAP_NO_REMAP;
	} else if (strncasecmp(path + path_len - 4, ".pcm", 4) != 0) {
		return TSREMAP_NO_REMAP;
	}

	start = 0;
	end = 0;

	if (TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", -1) == TS_ERROR) {
		return TSREMAP_NO_REMAP;
	}

	// if request header have No-Des, As a normal http range request
	no_des_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp,"No-Des", 6);
	if (no_des_field) {
		return TSREMAP_NO_REMAP;
	}
	// remove Range   #request Range: bytes=500-999, response Content-Range: bytes 21010-47021/47022
	range_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp,TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);
	if (range_field) {
		range = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp,range_field, -1, &range_len);
		size_t b_len = sizeof("bytes=") - 1;
		if (range && (strncasecmp(range, "bytes=", b_len) == 0)) {
			//get range value
			start = (int64_t) strtol(range + b_len, NULL, 10);
			range_separator = strchr(range, '-');
			if (range_separator) {
				end = (int64_t) strtol(range_separator + 1, NULL, 10);
			}
		}
		TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, range_field);
		TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, range_field);
	}

	if (start == 0 && end == 0) {
		return TSREMAP_NO_REMAP;
	} else if (start < 0 || end < 0) {
		TSHttpTxnSetHttpRetStatus(rh, TS_HTTP_STATUS_BAD_REQUEST);
		TSHttpTxnErrorBodySet(rh, TSstrdup("Invalid request."),sizeof("Invalid request.") - 1, NULL);
	}

	if(start > 0 && end > 0 && end<=start) {
		return TSREMAP_NO_REMAP;
	}

	// remove Accept-Encoding
	ae_field = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp,TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
	if (ae_field) {
		TSMimeHdrFieldDestroy(rri->requestBufp, rri->requestHdrp, ae_field);
		TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, ae_field);
	}

	mc = new Mp4Context(start,end);
	TSDebug(PLUGIN_NAME, "TSRemapDoRemap start=%ld  end=%ld", start, end);
	contp = TSContCreate(mp4_handler, NULL);
	TSContDataSet(contp, mc);

	TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
	TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
	TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);
	return TSREMAP_NO_REMAP;
}

static int mp4_handler(TSCont contp, TSEvent event, void *edata) {
	TSHttpTxn txnp;
	Mp4Context *mc;

	txnp = (TSHttpTxn) edata;
	mc = (Mp4Context *) TSContDataGet(contp);

	switch (event) {
	case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
		mp4_cache_lookup_complete(mc, txnp);
		break;

	case TS_EVENT_HTTP_READ_RESPONSE_HDR:
		mp4_read_response(mc, txnp);
		break;

	case TS_EVENT_HTTP_TXN_CLOSE:
		TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_TXN_CLOSE");
		delete mc;
		TSContDestroy(contp);
		break;

	default:
		break;
	}

	TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
	return 0;
}

static void mp4_cache_lookup_complete(Mp4Context *mc, TSHttpTxn txnp) {
	TSMBuffer bufp;
	TSMLoc hdrp;
	TSMLoc cl_field;
	TSHttpStatus code;
	int obj_status;
	int64_t n;

	if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
		TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
		return;
	}

	if (obj_status != TS_CACHE_LOOKUP_HIT_STALE && obj_status != TS_CACHE_LOOKUP_HIT_FRESH)
		return;

	if (TSHttpTxnCachedRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
		TSError("[%s] Couldn't get cache resp", __FUNCTION__);
		return;
	}

	code = TSHttpHdrStatusGet(bufp, hdrp);
	if (code != TS_HTTP_STATUS_OK) {
		goto release;
	}

	n = 0;

	cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH,TS_MIME_LEN_CONTENT_LENGTH);
	if (cl_field) {
		n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
		TSHandleMLocRelease(bufp, hdrp, cl_field);
	}

	if (n <= 0)
		goto release;

	mc->cl = n;
	TSDebug(PLUGIN_NAME, "mp4_cache_lookup_complete cl %ld", n);
	mp4_add_transform(mc, txnp);

release:
	TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void mp4_read_response(Mp4Context *mc, TSHttpTxn txnp) {
	TSMBuffer bufp;
	TSMLoc hdrp;
	TSMLoc cl_field;
	TSHttpStatus status;
	int64_t n;

	if (TSHttpTxnServerRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
		TSError("[%s] could not get request os data", __FUNCTION__);
		return;
	}

	status = TSHttpHdrStatusGet(bufp, hdrp);
	if (status != TS_HTTP_STATUS_OK)
		goto release;

	n = 0;
	cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH,
			TS_MIME_LEN_CONTENT_LENGTH);
	if (cl_field) {
		n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
		TSHandleMLocRelease(bufp, hdrp, cl_field);
	}

	if (n <= 0)
		goto release;
	TSDebug(PLUGIN_NAME, "mp4_read_response cl %ld", n);
	mc->cl = n;
	mp4_add_transform(mc, txnp);

release:
	TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void mp4_add_transform(Mp4Context *mc, TSHttpTxn txnp) {
	TSVConn connp;

	if (mc->transform_added)
		return;

	mc->mtc = new Mp4TransformContext(mc->start, mc->end, mc->cl, des_key, is_need_add_mp4_header);

	TSHttpTxnUntransformedRespCache(txnp, 1);//tell TS just cache before transform data, not cache after transform data
	TSHttpTxnTransformedRespCache(txnp, 0);//tell TS just cache before transform data, not cache after transform data

	connp = TSTransformCreate(mp4_transform_entry, txnp);
	TSContDataSet(connp, mc);
	TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

	mc->transform_added = true;
}

static int mp4_transform_entry(TSCont contp, TSEvent event, void * /* edata ATS_UNUSED */) {
	TSVIO input_vio;
	Mp4Context *mc = (Mp4Context *) TSContDataGet(contp);

	if (TSVConnClosedGet(contp)) {
		TSContDestroy(contp);
		return 0;
	}

	switch(event) {
	case TS_EVENT_ERROR:
		input_vio = TSVConnWriteVIOGet(contp);
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
		break;

	case TS_EVENT_VCONN_WRITE_COMPLETE:
		TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
		break;

	case TS_EVENT_VCONN_WRITE_READY:
	default:
		mp4_transform_handler(contp,mc);
		break;
	}

	return 0;
}

static int mp4_transform_handler(TSCont contp, Mp4Context *mc) {
	TSVConn output_conn;
	TSVIO input_vio;
	TSIOBufferReader input_reader;
	int64_t avail, toread, upstream_done;
	int ret;
	bool write_down;
	Mp4TransformContext *mtc;

	mtc = mc->mtc;

	output_conn = TSTransformOutputVConnGet(contp);
	input_vio = TSVConnWriteVIOGet(contp);
	input_reader = TSVIOReaderGet(input_vio);

	if (!TSVIOBufferGet(input_vio)) {
		if (mtc->output.buffer) {
			TSVIONBytesSet(mtc->output.vio, mtc->total);
			TSVIOReenable(mtc->output.vio);
		}
		return 1;
	}

	avail = TSIOBufferReaderAvail(input_reader);
	upstream_done = TSVIONDoneGet(input_vio);

	TSIOBufferCopy(mtc->res_buffer, input_reader, avail, 0);
	TSIOBufferReaderConsume(input_reader, avail);
	TSVIONDoneSet(input_vio, upstream_done + avail);

	toread = TSVIONTodoGet(input_vio);
	write_down = false;
	if(!mtc->parse_over) {
		ret = mtc->mp4_parse_meta(toread <= 0);
		TSDebug(PLUGIN_NAME, "parse_over %d, ret= %d", mtc->parse_over, ret);
		if (ret == 0)
			goto trans;

		mtc->parse_over = true;
		mtc->output.buffer = TSIOBufferCreate();
		mtc->output.reader = TSIOBufferReaderAlloc(mtc->output.buffer);

		if (ret < 0) {
			mtc->output.vio = TSVConnWrite(output_conn, contp,mtc->output.reader, mc->cl);
			mtc->raw_transform = true;

		} else {
			mtc->output.vio = TSVConnWrite(output_conn, contp,mtc->output.reader, mtc->content_length);
		}
	}

	mtc->copy_drm_or_origin_data(&write_down,&toread);

trans:
	if (write_down)
		TSVIOReenable(mtc->output.vio);

	if (toread > 0) {
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY,input_vio);
	} else {
		TSVIONBytesSet(mtc->output.vio, mtc->total);
		TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE,input_vio);
	}

	return 1;
}
