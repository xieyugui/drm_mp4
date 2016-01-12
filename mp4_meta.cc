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

#include "mp4_meta.h"

static mp4_atom_handler mp4_atoms[] = {
		{ "ftyp", &Mp4Meta::mp4_read_ftyp_atom }, { "moov",
				&Mp4Meta::mp4_read_moov_atom }, { "mdat",
				&Mp4Meta::mp4_read_mdat_atom }, { NULL, NULL } };

static mp4_atom_handler mp4_moov_atoms[] = { { "mvhd",
		&Mp4Meta::mp4_read_mvhd_atom },
		{ "trak", &Mp4Meta::mp4_read_trak_atom }, { "cmov",
				&Mp4Meta::mp4_read_cmov_atom }, { NULL, NULL } };

static mp4_atom_handler mp4_trak_atoms[] = { { "tkhd",
		&Mp4Meta::mp4_read_tkhd_atom },
		{ "mdia", &Mp4Meta::mp4_read_mdia_atom }, { NULL, NULL } };

static mp4_atom_handler mp4_mdia_atoms[] = { { "mdhd",
		&Mp4Meta::mp4_read_mdhd_atom },
		{ "hdlr", &Mp4Meta::mp4_read_hdlr_atom }, { "minf",
				&Mp4Meta::mp4_read_minf_atom }, { NULL, NULL } };

static mp4_atom_handler mp4_minf_atoms[] = { { "vmhd",
		&Mp4Meta::mp4_read_vmhd_atom },
		{ "smhd", &Mp4Meta::mp4_read_smhd_atom }, { "dinf",
				&Mp4Meta::mp4_read_dinf_atom }, { "stbl",
				&Mp4Meta::mp4_read_stbl_atom }, { NULL, NULL } };

static mp4_atom_handler mp4_stbl_atoms[] = { { "stsd",
		&Mp4Meta::mp4_read_stsd_atom },
		{ "stts", &Mp4Meta::mp4_read_stts_atom }, { "stss",
				&Mp4Meta::mp4_read_stss_atom }, { "ctts",
				&Mp4Meta::mp4_read_ctts_atom }, { "stsc",
				&Mp4Meta::mp4_read_stsc_atom }, { "stsz",
				&Mp4Meta::mp4_read_stsz_atom }, { "stco",
				&Mp4Meta::mp4_read_stco_atom }, { "co64",
				&Mp4Meta::mp4_read_co64_atom }, { NULL, NULL } };

//以上函数将mp4结构都解析出来

static void mp4_reader_set_32value(TSIOBufferReader readerp, int64_t offset,
		uint32_t n);
static void mp4_reader_set_64value(TSIOBufferReader readerp, int64_t offset,
		uint64_t n);
static uint32_t mp4_reader_get_32value(TSIOBufferReader readerp,
		int64_t offset);
static uint64_t mp4_reader_get_64value(TSIOBufferReader readerp,
		int64_t offset);
static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf,
		int64_t length);

//#########################drm header start#############################//

void Mp4Meta::get_des_videoid(u_char *des_videoid, uint32_t *d_v_length) {
	memcpy(des_videoid, videoid, videoid_size);
	des_encrypt(tdes_key, des_videoid, videoid_size);
	*d_v_length = videoid_size+MP4_DES_ADD_LENGTH;
}

//void Mp4Meta::get_des_null(u_char *des_null, uint32_t *d_n_length) {
//	des_encrypt(tdes_key, des_null, 0);
//	*d_n_length = MP4_DES_ADD_LENGTH;
//}

int Mp4Meta::process_drm_header() //先解析pcf 的头 signature, version, videoid tag, userid tag, reserved tag
{

	int64_t avail;
	size_t drm_header_size = get_drm_header_size();
	char buf[3];

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < (int64_t) drm_header_size)
		return 0;


	IOBufferReaderCopy(meta_reader, buf, 3);
	if (buf[0] != 'P' || buf[1] != 'C'|| buf[2] != 'M') {
		return -1;
	}

	version = mp4_reader_get_32value(meta_reader,3);//4
	TSDebug(PLUGIN_NAME, "drm version = %d", version);
	if (version != VIDEO_VERSION_4)
		return -1;

	videoid_size = mp4_reader_get_32value(meta_reader,7);//32
	TSDebug(PLUGIN_NAME, " drm videoid_size = %d", videoid_size);
	if (videoid_size <= 0)
		return -1;

	TSIOBufferCopy(drm_buffer, meta_reader, drm_header_size, 0);
	TSIOBufferReaderConsume(meta_reader, drm_header_size);
	tag_pos += drm_header_size;

	TSDebug(PLUGIN_NAME, "process_header tag_pos=%ld", tag_pos); //已经消费了多少字节

	this->current_handler = &Mp4Meta::process_drm_header_videoid;
	return process_drm_header_videoid();

}

int Mp4Meta::process_drm_header_videoid() {
	int64_t avail;
	size_t userid_size_length = sizeof(uint32_t);
	int64_t read_size = videoid_size + userid_size_length;

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < read_size)
		return 0;

	videoid = (u_char *) TSmalloc(sizeof(u_char) * (videoid_size));
	IOBufferReaderCopy(meta_reader, videoid, videoid_size);
	TSIOBufferCopy(drm_buffer, meta_reader, videoid_size, 0);
	TSIOBufferReaderConsume(meta_reader, videoid_size);

	IOBufferReaderCopy(meta_reader, &userid_size, userid_size_length);
	userid_size = mp4_reader_get_32value(meta_reader,0);//16
	TSIOBufferCopy(drm_buffer, meta_reader, userid_size_length, 0);
	TSIOBufferReaderConsume(meta_reader, userid_size_length);

	tag_pos += read_size; //总共消费了多少字节

	if (userid_size <= 0)
		return -1;

	TSDebug(PLUGIN_NAME, "process_header_videoid  userid_size=%d, tag_pos=%ld",
			userid_size, tag_pos);

	this->current_handler = &Mp4Meta::process_drm_header_userid;
	return process_drm_header_userid();
}

int Mp4Meta::process_drm_header_userid() {
	int64_t avail;
	size_t range_size_length = sizeof(uint32_t);
	int64_t read_size = userid_size + range_size_length;

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < read_size)
		return 0;

	//B941E9F028226923
//	userid = (u_char *) TSmalloc(sizeof(u_char) * (userid_size));
//	IOBufferReaderCopy(meta_reader, userid, userid_size);
//	TSDebug(PLUGIN_NAME, "process_header userid=%.*s",userid_size ,userid);
	TSIOBufferCopy(drm_buffer, meta_reader, userid_size, 0);
	TSIOBufferReaderConsume(meta_reader, userid_size);

	IOBufferReaderCopy(meta_reader, &range_size, range_size_length);
	range_size = mp4_reader_get_32value(meta_reader,0);//24
	TSIOBufferCopy(drm_buffer, meta_reader, range_size_length, 0);
	TSIOBufferReaderConsume(meta_reader, range_size_length);

	tag_pos += read_size;

	TSDebug(PLUGIN_NAME, "process_header_userid range_size=%d, tag_pos=%ld",
			range_size, tag_pos);

	this->current_handler = &Mp4Meta::process_drm_header_range;
	return process_drm_header_range();
}

int Mp4Meta::process_drm_header_range() {
	int64_t avail;
	size_t range_body_length = sizeof(uint64_t) * 3;
	size_t read_size = range_body_length + sizeof(uint32_t) * 2;
	u_char buf[read_size];

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < (int64_t) read_size)
		return 0;

	IOBufferReaderCopy(meta_reader, buf, read_size);
	range_start = mp4_get_64value(buf);
	range_end = mp4_get_64value(buf + sizeof(uint64_t));
	original_file_size = mp4_get_64value(buf + sizeof(uint64_t)* 2);
//	TSDebug(PLUGIN_NAME,
//			"process_drm_header_range range_start=%ld, range_end=%ld, original_file_size=%ld",
//			range_start, range_end, original_file_size);

	if ((range_start >= range_end) || ((int64_t)original_file_size >= this->cl))
		return -1;
	if(this->end > 0 && this->end > (int64_t)range_end)
		return -1;
	if(this->end == (int64_t)range_end)
		this->end = 0;

	section_size = mp4_get_32value(buf + range_body_length);
	section_count = mp4_get_32value(buf + range_body_length + sizeof(uint32_t));
	old_section_count = section_count;
	//TODO 可以加点判断

	TSIOBufferReaderConsume(meta_reader, read_size);

	tag_pos += read_size;

	TSDebug(PLUGIN_NAME,
			"process_drm_header_range  section_size=%d, section_count=%d, tag_pos=%ld",
			section_size, section_count, tag_pos);

	if (section_count <= 0
			|| (section_size
					!= ((sizeof(uint64_t) * section_count) + sizeof(uint32_t))))
		return -1;

	this->current_handler = &Mp4Meta::process_drm_header_sections;
	return process_drm_header_sections();
}

int Mp4Meta::process_drm_header_sections() {
	int64_t avail;
	size_t range_body_length = sizeof(uint64_t) * section_count;
	size_t reserved_size_length = sizeof(uint32_t);
	int64_t read_size = range_body_length + reserved_size_length;

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < (int64_t) read_size)
		return 0;

	section_length_arr = (u_char *) TSmalloc(range_body_length);//8184
	IOBufferReaderCopy(meta_reader, section_length_arr, range_body_length);
	TSIOBufferReaderConsume(meta_reader, range_body_length);
	IOBufferReaderCopy(meta_reader, &reserved_size, reserved_size_length);
	reserved_size = mp4_reader_get_32value(meta_reader,0);//0
	TSIOBufferReaderConsume(meta_reader, reserved_size_length);

	tag_pos += read_size;

	drm_head_length = tag_pos; //drm head 的长度
	TSDebug(PLUGIN_NAME, "process_drm_header_sections tag_pos=%ld meta_avail=%ld", tag_pos,TSIOBufferReaderAvail(meta_reader));

	if (reserved_size <= 0) {
		this->current_handler = &Mp4Meta::process_decrypt_mp4_body;
		return process_decrypt_mp4_body();
	} else {
		this->current_handler = &Mp4Meta::process_drm_header_reserved;
		return process_drm_header_reserved();
	}
}

int Mp4Meta::process_drm_header_reserved() {
	int64_t avail;

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < reserved_size)
		return 0;

	reserved = (u_char *) TSmalloc(sizeof(u_char) * (reserved_size));
	IOBufferReaderCopy(meta_reader, reserved, reserved_size);
	TSIOBufferReaderConsume(meta_reader, reserved_size);

	tag_pos += reserved_size;

	drm_head_length = tag_pos; //drm head 的长度

	TSDebug(PLUGIN_NAME, "process_header_reserved reserved=%.*s", reserved_size,
			reserved);

	this->current_handler = &Mp4Meta::process_decrypt_mp4_body;
	return process_decrypt_mp4_body();
}

//根据头信息进行解密
int Mp4Meta::process_decrypt_mp4_body() {
	//解析section_length_arr
	uint64_t dec_length;
	int64_t avail;
	uint64_t section_arr[section_count];
	u_char *des_buf;
	uint32_t i;
	dec_length = 0;
	for (i = 0; i < section_count; i++) {
		section_arr[i] = mp4_get_64value(section_length_arr + i * sizeof(uint64_t));
//		TSDebug(PLUGIN_NAME, "process_decrypt_mp4_body section_arr=%.ld",
//				section_arr[i]);
		dec_length += section_arr[i];
	}

	if ((this->cl < (int64_t)dec_length)
			|| (dec_length > MP4_DES_LENGTH * MP4_DES_MAX_COUNT))
		return -1;

	avail = TSIOBufferReaderAvail(meta_reader);
	if (avail < (int64_t)dec_length)
		return 0;


	TSIOBufferCopy(des_buffer, meta_reader, avail, 0); //全部拷贝到des_buffer中
	TSIOBufferReaderConsume(meta_reader, avail);

	des_buf = (u_char *) TSmalloc(sizeof(u_char) * MP4_DES_LENGTH);
	for (i = 0; i < section_count; i++) {
		memset(des_buf, 0, MP4_DES_LENGTH);
		IOBufferReaderCopy(des_reader, des_buf, section_arr[i]);
		des_decrypt(tdes_key, des_buf, section_arr[i]);
		TSIOBufferWrite(meta_buffer, des_buf,section_arr[i] - MP4_DES_ADD_LENGTH);
		TSIOBufferReaderConsume(des_reader, section_arr[i]);
	}

	TSfree((char * )des_buf);
	des_buf = NULL;

	avail = TSIOBufferReaderAvail(des_reader);

	if (avail > 0) {
		TSIOBufferCopy(meta_buffer, des_reader, avail, 0);
		TSIOBufferReaderConsume(des_reader, avail);
	}
	this->meta_avail = TSIOBufferReaderAvail(meta_reader);

	tag_pos += MP4_DES_ADD_LENGTH * section_count; //已经消费了多少  解密之后 又放回去了，也就是 136 － 128

	TSDebug(PLUGIN_NAME, "process_decrypt_mp4_body tag_pos=%ld", tag_pos);

	return 1;
}

int Mp4Meta::read_drm_header(TSIOBufferReader readerp, drm_header * header) {

	IOBufferReaderCopy(readerp, &header->signature, sizeof(header->signature));
	TSIOBufferReaderConsume(readerp, sizeof(header->signature));

	IOBufferReaderCopy(readerp, &header->version, sizeof(header->version));
	TSIOBufferReaderConsume(readerp, sizeof(header->version));

	IOBufferReaderCopy(readerp, &header->videoid_size,
			sizeof(header->videoid_size));
	TSIOBufferReaderConsume(readerp, sizeof(header->videoid_size));

	if (header->signature[0] != 'P' || header->signature[1] != 'C'
			|| header->signature[2] != 'M') {
		return -1;
	}

	return 0;
}

size_t Mp4Meta::get_drm_header_size() {
	drm_header header;
	return (sizeof(header.signature) + sizeof(header.version)
			+ sizeof(header.videoid_size));
}

static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf,
		int64_t length) {
	int64_t avail, need, n;
	const char *start;
	TSIOBufferBlock blk;

	n = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);
		need = length < avail ? length : avail;

		if (need > 0) {
			memcpy((char *) buf + n, start, need);
			length -= need;
			n += need;
		}

		if (length == 0)
			break;

		blk = TSIOBufferBlockNext(blk);
	}

	return n;
}

int Mp4Meta::process_encrypt_mp4_body() {

	int64_t des_avail, new_mp4_length;
	uint64_t i, need_length;
	u_char *buf;
	new_mp4_length = this->range_end - this->range_start + 1;
	des_avail = TSIOBufferReaderAvail(des_reader);
	need_length = 0;
	if(new_mp4_length < MP4_NEED_DES_LENGTH * MP4_DES_MAX_COUNT) {
		need_length = new_mp4_length;
	} else {
		need_length = MP4_NEED_DES_LENGTH * MP4_DES_MAX_COUNT;
	}
//	if( (new_mp4_length % MP4_NEED_DES_LENGTH) < MP4_DES_ADD_LENGTH) {
//		need_length += new_mp4_length % MP4_NEED_DES_LENGTH;
//	}
//	need_length += section_arr[i] - MP4_DES_ADD_LENGTH;
	TSDebug(PLUGIN_NAME, "process_encrypt_mp4_body des_avail=%ld need_length=%ld", des_avail,need_length);
	if ((des_avail) < (int64_t)need_length)
		return 0;
	buf = (u_char *) TSmalloc(sizeof(u_char) * MP4_DES_LENGTH);
	for (i = 0; i < section_count -1; i++) {
		memset(buf, 0, MP4_DES_LENGTH);
//		section_size = section_arr[i] - MP4_DES_ADD_LENGTH;
//		TSDebug(PLUGIN_NAME, "process_encrypt_mp4_body i=%d section_arr[i]=%ld", i, section_arr[i]);
		IOBufferReaderCopy(des_reader, buf, MP4_NEED_DES_LENGTH);
		TSIOBufferReaderConsume(des_reader, MP4_NEED_DES_LENGTH);
		//进行des 加密
		des_encrypt(tdes_key, buf, MP4_NEED_DES_LENGTH);
		TSIOBufferWrite(out_handle.buffer, buf, MP4_DES_LENGTH);
		TSDebug(PLUGIN_NAME, "process_des_mp4_body MP4_NEED_DES_LENGTH=%d avail=%ld section_count=%d", MP4_NEED_DES_LENGTH,TSIOBufferReaderAvail(out_handle.reader), section_count);
	}
	memset(buf, 0, MP4_DES_LENGTH);
	if( small_des_add_length && (small_des_add_length < MP4_DES_ADD_LENGTH)) {
		IOBufferReaderCopy(des_reader, buf, small_des_add_length);
		TSIOBufferReaderConsume(des_reader, small_des_add_length);
		//进行des 加密
		des_encrypt(tdes_key, buf, small_des_add_length);
		TSIOBufferWrite(out_handle.buffer, buf, MP4_DES_ADD_LENGTH);
	} else {
		IOBufferReaderCopy(des_reader, buf, MP4_NEED_DES_LENGTH);
		TSIOBufferReaderConsume(des_reader, MP4_NEED_DES_LENGTH);
		//进行des 加密
		des_encrypt(tdes_key, buf, MP4_NEED_DES_LENGTH);
		TSIOBufferWrite(out_handle.buffer, buf, MP4_DES_LENGTH);
	}

	TSfree((char * )buf);
	buf = NULL;
	des_avail = TSIOBufferReaderAvail(des_reader);
	if (des_avail > 0) {
		TSIOBufferCopy(out_handle.buffer, des_reader, des_avail, 0);
		TSIOBufferReaderConsume(des_reader, des_avail);
	}

	this->is_des_body = true;
	TSDebug(PLUGIN_NAME, "process_des_mp4_body  success");
	return 1;
}

//#########################drm header end#############################//
int Mp4Meta::parse_meta(bool body_complete) {
	int ret, rc;
	int64_t drm_change_length;
	uint32_t del_drm_header_length;

	meta_avail = TSIOBufferReaderAvail(meta_reader);

	if (wait_next && wait_next <= meta_avail) { //不合法的box 需要丢弃
		mp4_meta_consume(wait_next);
		wait_next = 0;
	}

	if (meta_avail < MP4_MIN_BUFFER_SIZE && !body_complete)
		return 0;

	drm_change_length = 0;
	//解析drm header
	if (!complete_parse_drm_header) {
		TSDebug(PLUGIN_NAME, "start parse drm header");
		ret = (this->*current_handler)();//里面注意更新meta_avail
		TSDebug(PLUGIN_NAME, "end parse drm header ret=%d", ret);
		if (ret > 0) {
			complete_parse_drm_header = true;
			TSDebug(PLUGIN_NAME, "---------------------------parse_drm start=%ld  end=%ld",start, end);
			if(!this->is_need_md) {
				change_drm_header(0, 0);
				this->start_pos = this->start + this->tag_pos;
				if(this->end) {
					this->end_pos = this->end +this->tag_pos+1;
				}
				//如果drm 变小了，需要更改总文件大小
				drm_change_length = this->drm_head_length - TSIOBufferReaderAvail(drm_reader);
				TSDebug(PLUGIN_NAME, "parse_meta drm_change_length = %ld", drm_change_length);
				this->content_length = this->cl - this->start - (drm_change_length) - this->small_des_add_length;
				if(this->end) {
					//由于range 请求影响了drm header 大小以及加密的内容的大小
					del_drm_header_length = drm_change_length + (this->old_section_count - this->section_count) * MP4_DES_ADD_LENGTH;
					//将start之前的字节去掉，end 之后的字节去掉  mp4
					this->content_length = this->cl - this->start - ((this->original_file_size-1) - this->end) - del_drm_header_length - this->small_des_add_length;
//					if(range_is_to_small) {
//						this->content_length = this->cl - this->original_file_size - del_drm_header_length + MP4_DES_ADD_LENGTH;
//					}
				}
				return 1;
			}
		} else if (ret == 0) {
			if (body_complete) {
				return -1;
			} else {
				return 0;
			}
		} else {
			return -1;
		}
	}

	if(!this->is_need_md) {
		return -1;
	}

	//带mp4头解析的，就不处理有end 的情况了
	this->end = 0;
	this->end_pos = 0;

	ret = this->parse_root_atoms(); //从根开始解析mp4
	TSDebug(PLUGIN_NAME, "end parse_root_atoms ret = %d", ret);

	if (ret < 0) {
		return -1;

	} else if (ret == 0) {
		if (body_complete) {
			return -1;

		} else {
			return 0;
		}
	}
	// generate new meta data
	rc = this->post_process_meta();
	TSDebug(PLUGIN_NAME, "end post_process_meta rc = %d", rc);
	if (rc != 0) {
		return -1;
	}

	return 1;
}

void Mp4Meta::mp4_meta_consume(int64_t size) {
	TSIOBufferReaderConsume(meta_reader, size);
	meta_avail -= size;
	passed += size;
}

//更新drm header 头
int Mp4Meta::change_drm_header(off_t start_offset, off_t adjustment) {
	size_t range_body_size, section_size_b, reserved_size_b;
	uint32_t i;
	uint32_t new_section_count;
	uint64_t small_content_length;
	int64_t new_mp4_length;
	reserved_size_b = sizeof(uint32_t);
	range_body_size = sizeof(uint64_t) * 3;
	u_char buf[range_body_size];
	section_size_b = sizeof(uint32_t) * 2;
	u_char section_size_buf[section_size_b];
	new_section_count = 0;
	small_content_length = 0;

	if(out_handle.buffer == NULL)
		out_handle.buffer = TSIOBufferCreate(); //初始化输出buffer
	if(out_handle.reader == NULL)
		out_handle.reader = TSIOBufferReaderAlloc(out_handle.buffer);

	TSDebug(PLUGIN_NAME, "change_drm_header avail = %ld",TSIOBufferReaderAvail(drm_reader));
	memset(this->section_length_arr, 0, section_count * sizeof(uint64_t));

	if(!this->is_need_md) {
		this->range_start = this->start;
		new_mp4_length = this->range_end - this->start +1;
		if(this->end) {
			this->range_end = this->end;
			new_mp4_length = this->range_end - this->range_start + 1;
//			if(range_is_to_small) {
//				new_mp4_length = 0;
//			}
		}
	} else {
		this->range_start = start_offset;
		new_mp4_length = this->original_file_size + adjustment;
	}

	mp4_set_64value(buf, this->range_start);
	mp4_set_64value(buf + sizeof(uint64_t), this->range_end);
	mp4_set_64value(buf + sizeof(uint64_t) * 2, this->original_file_size);
	TSIOBufferWrite(drm_buffer, buf, range_body_size);
	TSDebug(PLUGIN_NAME, "change_drm_header range_start = %ld, range_end = %ld, original_file_size=%ld, start=%ld o_file_size=%ld",
			this->range_start, this->range_end, this->original_file_size,this->start,this->original_file_size);
	TSDebug(PLUGIN_NAME, "change_drm_header avail = %ld",TSIOBufferReaderAvail(drm_reader));
	if (new_mp4_length >= MP4_DES_MAX_COUNT * MP4_NEED_DES_LENGTH) {
		new_section_count = MP4_DES_MAX_COUNT;
	} else {
		new_section_count = new_mp4_length / MP4_NEED_DES_LENGTH + 1;
	}

	this->section_size = sizeof(uint32_t)
			+ sizeof(uint64_t) * new_section_count;
	u_char section_buf[sizeof(uint64_t) * new_section_count];
	for (i = 0; i < new_section_count - 1; i++) {
		mp4_set_64value(section_buf + i * sizeof(uint64_t), MP4_DES_LENGTH);
		TSDebug(PLUGIN_NAME, "rrrrrrrrrrrrrrrrrrrrrrrrrrr i = %d  MP4_DES_LENGTH=%d ", i,MP4_DES_LENGTH);
	}

	if (new_section_count == MP4_DES_MAX_COUNT) {
		mp4_set_64value(section_buf + i * sizeof(uint64_t), MP4_DES_LENGTH);
	} else {
		small_content_length = new_mp4_length % MP4_NEED_DES_LENGTH;

		if(small_content_length < MP4_DES_ADD_LENGTH) {
			this->small_des_add_length = small_content_length;
			small_content_length = 0;
		}
		mp4_set_64value(section_buf + i * sizeof(uint64_t),(small_content_length +MP4_DES_ADD_LENGTH));

		TSDebug(PLUGIN_NAME, "rrrrrrrrrrrrrrrrrrrrrrrrrrr i = %d  small_content_length=%ld ", i,small_content_length);
	}

	memcpy(this->section_length_arr,section_buf, new_section_count*sizeof(uint64_t));

	mp4_set_32value(section_size_buf, this->section_size);
	mp4_set_32value(section_size_buf +sizeof(uint32_t), new_section_count);
	TSIOBufferWrite(drm_buffer, section_size_buf, section_size_b);
	TSIOBufferWrite(drm_buffer, section_buf,sizeof(uint64_t) * new_section_count);
	TSDebug(PLUGIN_NAME, "change_drm_header avail = %ld",TSIOBufferReaderAvail(drm_reader));
	this->drm_length += (new_section_count - this->section_count) * sizeof(uint64_t);
	this->section_count = new_section_count;

	//把 reserved tag 加进去
	u_char reserved_buf[reserved_size_b];
	mp4_set_32value(reserved_buf, this->reserved_size);
	TSIOBufferWrite(drm_buffer, reserved_buf, reserved_size_b);
	TSDebug(PLUGIN_NAME, "change_drm_header avail = %ld",TSIOBufferReaderAvail(drm_reader));
	if (this->reserved_size > 0) {
		TSIOBufferWrite(drm_buffer, reserved, this->reserved_size);
	}

	return 0;
}

//根据时间进行跳转
int Mp4Meta::post_process_meta() {
	off_t start_offset, adjustment;
	uint32_t i, j;
	int64_t avail;
	Mp4Trak *trak;

	if (this->trak_num == 0) { //如果没有trak_num 说明错误的
		return -1;
	}
	TSDebug(PLUGIN_NAME, "post_process_meta trak_num = %d", this->trak_num);

	if (mdat_atom.buffer == NULL) { //如果mdat已经解析了，说明错误
		return -1;
	}

	if(this->start <= this->passed) {//请求的start > ftyp + moov
		return -1;
	}

	//总长度－ 丢弃的长度 － drm header 以及des解密减少的字节和des(videoid)的长度
	this->drm_length = this->tag_pos + this->videoid_size + MP4_DES_ADD_LENGTH;

	if (ftyp_atom.buffer) { //直接copy
		TSIOBufferCopy(des_buffer, ftyp_atom.reader,
				TSIOBufferReaderAvail(ftyp_atom.reader), 0);
	}

	if (moov_atom.buffer) { //直接copy moov header
		TSIOBufferCopy(des_buffer, moov_atom.reader,
				TSIOBufferReaderAvail(moov_atom.reader), 0);
	}

	if (mvhd_atom.buffer) { //直接copy mvhd
		avail = TSIOBufferReaderAvail(mvhd_atom.reader);
		TSIOBufferCopy(des_buffer, mvhd_atom.reader, avail, 0);
		this->moov_size += avail;
	}

	start_offset = this->original_file_size; //文件总长度

	for (i = 0; i < trak_num; i++) { //下面开始遍历track
		trak = trak_vec[i];

		if (mp4_get_start_sample(trak) != 0) {
			return -1;
		}

		if (mp4_update_stts_atom(trak) != 0) { //更新time to sample box
			return -1;
		}

		if (mp4_update_stss_atom(trak) != 0) { //更新sync sample box
			return -1;
		}

		mp4_update_ctts_atom(trak); //更新composition time to sample box

		if (mp4_update_stsc_atom(trak) != 0) { //更新sample to chunk box
			return -1;
		}

		if (mp4_update_stsz_atom(trak) != 0) { //更新sample size box
			return -1;
		}

		if (trak->atoms[MP4_CO64_DATA].buffer) { //chunk offset box
			if (mp4_update_co64_atom(trak) != 0)
				return -1;
		} else if (mp4_update_stco_atom(trak) != 0) {
			return -1;
		}

		mp4_update_stbl_atom(trak);
		mp4_update_minf_atom(trak);
		trak->size += trak->mdhd_size;
		trak->size += trak->hdlr_size;
		mp4_update_mdia_atom(trak);
		trak->size += trak->tkhd_size;
		mp4_update_trak_atom(trak);

		this->moov_size += trak->size;
		if (start_offset > trak->start_offset)
			start_offset = trak->start_offset;

		for (j = 0; j <= MP4_LAST_ATOM; j++) {
			if (trak->atoms[j].buffer) {
				TSIOBufferCopy(des_buffer, trak->atoms[j].reader,
						TSIOBufferReaderAvail(trak->atoms[j].reader), 0); //全部拷贝一份过去
			}
		}

		mp4_update_tkhd_duration(trak);
		mp4_update_mdhd_duration(trak);
	}

	this->moov_size += 8;
	mp4_reader_set_32value(moov_atom.reader, 0, this->moov_size);
	this->content_length += this->moov_size;

	adjustment = this->ftyp_size + this->moov_size
			+ mp4_update_mdat_atom(start_offset) - start_offset; //mdat 起始大小

	change_drm_header(start_offset, adjustment);

	this->content_length += this->drm_length;

	TSIOBufferCopy(des_buffer, mdat_atom.reader,
			TSIOBufferReaderAvail(mdat_atom.reader), 0);

	for (i = 0; i < trak_num; i++) { //调整一下stco
		trak = trak_vec[i];

		if (trak->atoms[MP4_CO64_DATA].buffer) {
			mp4_adjust_co64_atom(trak, adjustment);

		} else {
			mp4_adjust_stco_atom(trak, adjustment);
		}
	}

	mp4_update_mvhd_duration(); //更新duration

	return 0;
}

/*
 * -1: error
 *  0: unfinished
 *  1: success.
 */
int Mp4Meta::parse_root_atoms() {
	int i, ret, rc;
	int64_t atom_size, atom_header_size;
	char buf[64];
	char *atom_header, *atom_name;

	memset(buf, 0, sizeof(buf));

	for (;;) {
		if (meta_avail < (int64_t) sizeof(uint32_t)) //要判断 32位还是64位的，最起码需要size 大小 4位
			return 0;

		IOBufferReaderCopy(meta_reader, buf, sizeof(mp4_atom_header64)); //先当作64位 读取box header
		atom_size = mp4_get_32value(buf); //获取大小

		if (atom_size == 0) { //如果size 大小为0 说明是最后一个box 直接结束
			return 1;
		}

		atom_header = buf;

		if (atom_size < (int64_t) sizeof(mp4_atom_header)) { //判断是否满足一个32位头大小，如果小于的话，一定是64位的
			if (atom_size == 1) { //如果size 为1 说明是64位的
				if (meta_avail < (int64_t) sizeof(mp4_atom_header64)) { //如果总数据小于64 box header 大小，就再次等待
					return 0;
				}
			} else { //如果不满足，说明解释失败，直接返回error
				return -1;
			}

			atom_size = mp4_get_64value(atom_header + 8);
			atom_header_size = sizeof(mp4_atom_header64);

		} else { // regular atom

			if (meta_avail < (int64_t) sizeof(mp4_atom_header)) // not enough for atom header  再次等待数据过来
				return 0;

			atom_header_size = sizeof(mp4_atom_header);
		}

		atom_name = atom_header + 4;

		if (atom_size + this->passed > this->cl) { //超过总长度
			return -1;
		}

		for (i = 0; mp4_atoms[i].name; i++) { // box header + box body
			if (memcmp(atom_name, mp4_atoms[i].name, 4) == 0) {
				ret = (this->*mp4_atoms[i].handler)(atom_header_size,
						atom_size - atom_header_size); // -1: error, 0: unfinished, 1: success

				if (ret <= 0) {
					return ret;

				} else if (meta_complete) { // success
					return 1;
				}

				goto next;
			}
		}

		// nonsignificant atom box
		rc = mp4_atom_next(atom_size, true); // 0: unfinished, 1: success
		if (rc == 0) {
			return rc;
		}

		next: continue;
	}

	return 1;
}

int Mp4Meta::mp4_atom_next(int64_t atom_size, bool wait) {
	if (meta_avail >= atom_size) {
		mp4_meta_consume(atom_size);
		return 1;
	}

	if (wait) {
		wait_next = atom_size;
		return 0;
	}

	return -1;
}

/*
 *  -1: error
 *   1: success
 */
int Mp4Meta::mp4_read_atom(mp4_atom_handler *atom, int64_t size) {
	int i, ret, rc;
	int64_t atom_size, atom_header_size;
	char buf[32];
	char *atom_header, *atom_name;

	if (meta_avail < size) // data insufficient, not reasonable for internal atom box. 数据应该是全的
		return -1;

	while (size > 0) {
		if (meta_avail < (int64_t) sizeof(uint32_t)) // data insufficient, not reasonable for internal atom box.
			return -1;

		IOBufferReaderCopy(meta_reader, buf, sizeof(mp4_atom_header64));
		atom_size = mp4_get_32value(buf);

		if (atom_size == 0) {
			return 1;
		}

		atom_header = buf;

		if (atom_size < (int64_t) sizeof(mp4_atom_header)) { //判断是32位还是64位的
			if (atom_size == 1) {
				if (meta_avail < (int64_t) sizeof(mp4_atom_header64)) {
					return -1;
				}

			} else {
				return -1;
			}

			atom_size = mp4_get_64value(atom_header + 8);
			atom_header_size = sizeof(mp4_atom_header64);

		} else { // regular atom

			if (meta_avail < (int64_t) sizeof(mp4_atom_header))
				return -1;

			atom_header_size = sizeof(mp4_atom_header);
		}

		atom_name = atom_header + 4;

		if (atom_size + this->passed > this->cl) { //判断一下总长度
			return -1;
		}

		for (i = 0; atom[i].name; i++) {
			if (memcmp(atom_name, atom[i].name, 4) == 0) {
				if (meta_avail < atom_size)
					return -1;

				ret = (this->*atom[i].handler)(atom_header_size,
						atom_size - atom_header_size); // -1: error, 0: success.

				if (ret < 0) {
					return ret;
				}

				goto next;
			}
		}

		// insignificant atom box
		rc = mp4_atom_next(atom_size, false); //可以忽视的box
		if (rc < 0) {
			return rc;
		}

		next: size -= atom_size;
		continue;
	}

	return 1;
}

int Mp4Meta::mp4_read_ftyp_atom(int64_t atom_header_size,
		int64_t atom_data_size) //拷贝ftyp数据，不需要做任何处理
		{
	int64_t atom_size;

	if (atom_data_size > MP4_MIN_BUFFER_SIZE)
		return -1;

	atom_size = atom_header_size + atom_data_size;

	if (meta_avail < atom_size) { // data unsufficient, reasonable from the first level
		return 0;
	}

	ftyp_atom.buffer = TSIOBufferCreate();
	ftyp_atom.reader = TSIOBufferReaderAlloc(ftyp_atom.buffer);

	TSIOBufferCopy(ftyp_atom.buffer, meta_reader, atom_size, 0);
	mp4_meta_consume(atom_size);

	content_length = atom_size;
	ftyp_size = atom_size;

	return 1;
}

int Mp4Meta::mp4_read_moov_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取moov
		{
	int64_t atom_size;
	int ret;

	if (mdat_atom.buffer != NULL) // not reasonable for streaming media 如果先读的mdata 的话，就当失败来处理
		return -1;

	atom_size = atom_header_size + atom_data_size;

	if (atom_data_size >= MP4_MAX_BUFFER_SIZE) //如果大于限定的buffer 当出错处理
		return -1;

	if (meta_avail < atom_size) { // data unsufficient, wait //数据不全，继续等待
		return 0;
	}

	moov_atom.buffer = TSIOBufferCreate();
	moov_atom.reader = TSIOBufferReaderAlloc(moov_atom.buffer);

	TSIOBufferCopy(moov_atom.buffer, meta_reader, atom_header_size, 0); //先拷贝 BOX HEADER
	mp4_meta_consume(atom_header_size);

	ret = mp4_read_atom(mp4_moov_atoms, atom_data_size); //开始解析mvhd + track.........

	return ret;
}

int Mp4Meta::mp4_read_mvhd_atom(int64_t atom_header_size,
		int64_t atom_data_size) {
	int64_t atom_size;
	uint32_t timescale;
	mp4_mvhd_atom *mvhd;
	mp4_mvhd64_atom mvhd64;

	if (sizeof(mp4_mvhd_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &mvhd64, sizeof(mp4_mvhd64_atom));
	mvhd = (mp4_mvhd_atom *) &mvhd64;

	if (mvhd->version[0] == 0) {
		timescale = mp4_get_32value(mvhd->timescale);

	} else { // 64-bit duration
		timescale = mp4_get_32value(mvhd64.timescale);
	}

	this->timescale = timescale; //获取一下整部电影的time scale

	atom_size = atom_header_size + atom_data_size;

	mvhd_atom.buffer = TSIOBufferCreate();
	mvhd_atom.reader = TSIOBufferReaderAlloc(mvhd_atom.buffer);

	TSIOBufferCopy(mvhd_atom.buffer, meta_reader, atom_size, 0);
	mp4_meta_consume(atom_size);

	return 1;
}

int Mp4Meta::mp4_read_trak_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取track
		{
	int rc;
	Mp4Trak *trak;

	if (trak_num >= MP4_MAX_TRAK_NUM - 1)
		return -1;

	trak = new Mp4Trak();
	trak_vec[trak_num++] = trak;

	trak->atoms[MP4_TRAK_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_TRAK_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_TRAK_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_TRAK_ATOM].buffer, meta_reader,
			atom_header_size, 0); // box header
	mp4_meta_consume(atom_header_size);

	rc = mp4_read_atom(mp4_trak_atoms, atom_data_size); //读取tkhd + media

	return rc;
}

int Mp4Meta::mp4_read_cmov_atom(int64_t /*atom_header_size ATS_UNUSED */,
		int64_t /* atom_data_size ATS_UNUSED */) {
	return -1;
}

int Mp4Meta::mp4_read_tkhd_atom(int64_t atom_header_size,
		int64_t atom_data_size) {
	int64_t atom_size;
	Mp4Trak *trak;

	atom_size = atom_header_size + atom_data_size;

	trak = trak_vec[trak_num - 1];
	trak->tkhd_size = atom_size;

	trak->atoms[MP4_TKHD_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_TKHD_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_TKHD_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_TKHD_ATOM].buffer, meta_reader, atom_size,
			0); //读取tkhd
	mp4_meta_consume(atom_size);

	mp4_reader_set_32value(trak->atoms[MP4_TKHD_ATOM].reader,
			offsetof(mp4_tkhd_atom, size), atom_size); //设置一下tkhd 的总大小

	return 1;
}

int Mp4Meta::mp4_read_mdia_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取 mdia
		{
	Mp4Trak *trak;

	trak = trak_vec[trak_num - 1];

	trak->atoms[MP4_MDIA_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_MDIA_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_MDIA_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_MDIA_ATOM].buffer, meta_reader,
			atom_header_size, 0); //读取 box header
	mp4_meta_consume(atom_header_size);

	return mp4_read_atom(mp4_mdia_atoms, atom_data_size);
}

int Mp4Meta::mp4_read_mdhd_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取mdhd
		{
	int64_t atom_size, duration;
	uint32_t ts;
	Mp4Trak *trak;
	mp4_mdhd_atom *mdhd;
	mp4_mdhd64_atom mdhd64;

	IOBufferReaderCopy(meta_reader, &mdhd64, sizeof(mp4_mdhd64_atom));
	mdhd = (mp4_mdhd_atom *) &mdhd64;

	if (mdhd->version[0] == 0) {
		ts = mp4_get_32value(mdhd->timescale);
		duration = mp4_get_32value(mdhd->duration);

	} else {
		ts = mp4_get_32value(mdhd64.timescale);
		duration = mp4_get_64value(mdhd64.duration);
	}

	atom_size = atom_header_size + atom_data_size;

	trak = trak_vec[trak_num - 1];
	trak->mdhd_size = atom_size;
	trak->timescale = ts;
	trak->duration = duration;

	trak->atoms[MP4_MDHD_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_MDHD_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_MDHD_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_MDHD_ATOM].buffer, meta_reader, atom_size,
			0);
	mp4_meta_consume(atom_size);

	mp4_reader_set_32value(trak->atoms[MP4_MDHD_ATOM].reader,
			offsetof(mp4_mdhd_atom, size), atom_size); //重新设置大小

	return 1;
}

int Mp4Meta::mp4_read_hdlr_atom(int64_t atom_header_size,
		int64_t atom_data_size) //hdlr
		{
	int64_t atom_size;
	Mp4Trak *trak;

	atom_size = atom_header_size + atom_data_size;

	trak = trak_vec[trak_num - 1];
	trak->hdlr_size = atom_size;

	trak->atoms[MP4_HDLR_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_HDLR_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_HDLR_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_HDLR_ATOM].buffer, meta_reader, atom_size,
			0);
	mp4_meta_consume(atom_size);

	return 1;
}

int Mp4Meta::mp4_read_minf_atom(int64_t atom_header_size,
		int64_t atom_data_size) {
	Mp4Trak *trak;

	trak = trak_vec[trak_num - 1];

	trak->atoms[MP4_MINF_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_MINF_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_MINF_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_MINF_ATOM].buffer, meta_reader,
			atom_header_size, 0); //读取 header box
	mp4_meta_consume(atom_header_size);

	return mp4_read_atom(mp4_minf_atoms, atom_data_size);
}

int Mp4Meta::mp4_read_vmhd_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取vmhd
		{
	int64_t atom_size;
	Mp4Trak *trak;

	atom_size = atom_data_size + atom_header_size;

	trak = trak_vec[trak_num - 1];
	trak->vmhd_size += atom_size;

	trak->atoms[MP4_VMHD_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_VMHD_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_VMHD_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_VMHD_ATOM].buffer, meta_reader, atom_size,
			0);
	mp4_meta_consume(atom_size);

	return 1;
}

int Mp4Meta::mp4_read_smhd_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取smhd
		{
	int64_t atom_size;
	Mp4Trak *trak;

	atom_size = atom_data_size + atom_header_size;

	trak = trak_vec[trak_num - 1];
	trak->smhd_size += atom_size;

	trak->atoms[MP4_SMHD_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_SMHD_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_SMHD_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_SMHD_ATOM].buffer, meta_reader, atom_size,
			0);
	mp4_meta_consume(atom_size);

	return 1;
}

int Mp4Meta::mp4_read_dinf_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取dinf
		{
	int64_t atom_size;
	Mp4Trak *trak;

	atom_size = atom_data_size + atom_header_size;

	trak = trak_vec[trak_num - 1];
	trak->dinf_size += atom_size;

	trak->atoms[MP4_DINF_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_DINF_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_DINF_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_DINF_ATOM].buffer, meta_reader, atom_size,
			0);
	mp4_meta_consume(atom_size);

	return 1;
}

int Mp4Meta::mp4_read_stbl_atom(int64_t atom_header_size,
		int64_t atom_data_size) //读取stbl
		{
	Mp4Trak *trak;

	trak = trak_vec[trak_num - 1];

	trak->atoms[MP4_STBL_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STBL_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STBL_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_STBL_ATOM].buffer, meta_reader,
			atom_header_size, 0); // box header
	mp4_meta_consume(atom_header_size);

	return mp4_read_atom(mp4_stbl_atoms, atom_data_size);
}

int Mp4Meta::mp4_read_stsd_atom(int64_t atom_header_size,
		int64_t atom_data_size) //sample description box
		{
	int64_t atom_size;
	Mp4Trak *trak;

	atom_size = atom_data_size + atom_header_size;

	trak = trak_vec[trak_num - 1];
	trak->size += atom_size;

	trak->atoms[MP4_STSD_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STSD_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STSD_ATOM].buffer);

	TSIOBufferCopy(trak->atoms[MP4_STSD_ATOM].buffer, meta_reader, atom_size,
			0);

	mp4_meta_consume(atom_size);

	return 1;
}

/**
 * time to sample box
 * size, type, version flags, number of entries
 * entry 1: sample count 1, sample duration 42
 * .......
 * 实际时间 0.2s  对应的duration = mdhd.timescale * 0.2s
 */
int Mp4Meta::mp4_read_stts_atom(int64_t atom_header_size,
		int64_t atom_data_size) // time to sample box
		{
	int32_t entries;
	int64_t esize;
	mp4_stts_atom stts;
	Mp4Trak *trak;

	if (sizeof(mp4_stts_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &stts, sizeof(mp4_stts_atom));

	entries = mp4_get_32value(stts.entries);
	esize = entries * sizeof(mp4_stts_entry);

	if (sizeof(mp4_stts_atom) - 8 + esize > (size_t) atom_data_size)
		return -1;

	trak = trak_vec[trak_num - 1];
	trak->time_to_sample_entries = entries;

	trak->atoms[MP4_STTS_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STTS_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STTS_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STTS_ATOM].buffer, meta_reader,
			sizeof(mp4_stts_atom), 0);

	trak->atoms[MP4_STTS_DATA].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STTS_DATA].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STTS_DATA].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STTS_DATA].buffer, meta_reader, esize,
			sizeof(mp4_stts_atom));

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}

/**
 * Sync Sample Box
 * size, type, version flags, number of entries
 *
 * 该box 决定了整个mp4 文件是否可以拖动，如果box 只有一个entry,则拖拉时，进度到最后
 */
int Mp4Meta::mp4_read_stss_atom(int64_t atom_header_size,
		int64_t atom_data_size) // Sync Sample Box
		{
	int32_t entries;
	int64_t esize;
	mp4_stss_atom stss;
	Mp4Trak *trak;

	if (sizeof(mp4_stss_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &stss, sizeof(mp4_stss_atom));
	entries = mp4_get_32value(stss.entries);
	esize = entries * sizeof(int32_t);

	if (sizeof(mp4_stss_atom) - 8 + esize > (size_t) atom_data_size)
		return -1;

	trak = trak_vec[trak_num - 1];
	trak->sync_samples_entries = entries;

	trak->atoms[MP4_STSS_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STSS_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STSS_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STSS_ATOM].buffer, meta_reader,
			sizeof(mp4_stss_atom), 0);

	trak->atoms[MP4_STSS_DATA].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STSS_DATA].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STSS_DATA].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STSS_DATA].buffer, meta_reader, esize,
			sizeof(mp4_stss_atom));

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}

int Mp4Meta::mp4_read_ctts_atom(int64_t atom_header_size,
		int64_t atom_data_size) //composition time to sample box
		{
	int32_t entries;
	int64_t esize;
	mp4_ctts_atom ctts;
	Mp4Trak *trak;

	if (sizeof(mp4_ctts_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &ctts, sizeof(mp4_ctts_atom));
	entries = mp4_get_32value(ctts.entries);
	esize = entries * sizeof(mp4_ctts_entry);

	if (sizeof(mp4_ctts_atom) - 8 + esize > (size_t) atom_data_size)
		return -1;

	trak = trak_vec[trak_num - 1];
	trak->composition_offset_entries = entries;

	trak->atoms[MP4_CTTS_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_CTTS_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_CTTS_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_CTTS_ATOM].buffer, meta_reader,
			sizeof(mp4_ctts_atom), 0);

	trak->atoms[MP4_CTTS_DATA].buffer = TSIOBufferCreate();
	trak->atoms[MP4_CTTS_DATA].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_CTTS_DATA].buffer);
	TSIOBufferCopy(trak->atoms[MP4_CTTS_DATA].buffer, meta_reader, esize,
			sizeof(mp4_ctts_atom));

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}

/**
 * sample to chunk box
 * size, type, version, flags, number of entries
 * entry 1:first chunk 1, samples pre chunk 13, sample description 1 ('self-ref')
 * ...............
 * 第500个sample 500 ＝ 28 ＊ 13 ＋ 12 ＋ 13*9 ＋ 7
 */
int Mp4Meta::mp4_read_stsc_atom(int64_t atom_header_size,
		int64_t atom_data_size) {
	int32_t entries;
	int64_t esize;
	mp4_stsc_atom stsc;
	Mp4Trak *trak;

	if (sizeof(mp4_stsc_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &stsc, sizeof(mp4_stsc_atom));
	entries = mp4_get_32value(stsc.entries);
	esize = entries * sizeof(mp4_stsc_entry);

	if (sizeof(mp4_stsc_atom) - 8 + esize > (size_t) atom_data_size)
		return -1;

	trak = trak_vec[trak_num - 1];
	trak->sample_to_chunk_entries = entries;
	TSDebug(PLUGIN_NAME, "mp4_read_stsc_atom sample_to_chunk_entries=%d",
			trak->sample_to_chunk_entries);
	trak->atoms[MP4_STSC_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STSC_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STSC_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STSC_ATOM].buffer, meta_reader,
			sizeof(mp4_stsc_atom), 0);

	trak->atoms[MP4_STSC_DATA].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STSC_DATA].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STSC_DATA].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STSC_DATA].buffer, meta_reader, esize,
			sizeof(mp4_stsc_atom));

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}
/**
 * sample size box
 * size, type, version, flags, sample size, number of entries
 * sample 1: sample size $000000ae
 * ........
 */
int Mp4Meta::mp4_read_stsz_atom(int64_t atom_header_size,
		int64_t atom_data_size) //sample size box
		{
	int32_t entries, size;
	int64_t esize, atom_size;
	mp4_stsz_atom stsz;
	Mp4Trak *trak;

	if (sizeof(mp4_stsz_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &stsz, sizeof(mp4_stsz_atom));
	entries = mp4_get_32value(stsz.entries);
	esize = entries * sizeof(int32_t);

	trak = trak_vec[trak_num - 1];
	size = mp4_get_32value(stsz.uniform_size);

	trak->sample_sizes_entries = entries;

	trak->atoms[MP4_STSZ_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STSZ_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STSZ_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STSZ_ATOM].buffer, meta_reader,
			sizeof(mp4_stsz_atom), 0);

	if (size == 0) { //全部sample 数目，如果所有的sample有相同的长度，这个字段就是这个值，否则就是0
		if (sizeof(mp4_stsz_atom) - 8 + esize > (size_t) atom_data_size)
			return -1;

		trak->atoms[MP4_STSZ_DATA].buffer = TSIOBufferCreate();
		trak->atoms[MP4_STSZ_DATA].reader = TSIOBufferReaderAlloc(
				trak->atoms[MP4_STSZ_DATA].buffer);
		TSIOBufferCopy(trak->atoms[MP4_STSZ_DATA].buffer, meta_reader, esize,
				sizeof(mp4_stsz_atom));

	} else {
		atom_size = atom_header_size + atom_data_size;
		trak->size += atom_size;
		mp4_reader_set_32value(trak->atoms[MP4_STSZ_ATOM].reader, 0, atom_size);
	}

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}
/**
 * 32 位 chunk offset box 定义了每个chunk 在媒体流中的位置。
 * size, type, version, flags, number of entries
 * chunk 1: $00039D28(in this file)
 * .........
 *
 */

int Mp4Meta::mp4_read_stco_atom(int64_t atom_header_size,
		int64_t atom_data_size) {
	int32_t entries;
	int64_t esize;
	mp4_stco_atom stco;
	Mp4Trak *trak;

	if (sizeof(mp4_stco_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &stco, sizeof(mp4_stco_atom));
	entries = mp4_get_32value(stco.entries);
	esize = entries * sizeof(int32_t);

	if (sizeof(mp4_stco_atom) - 8 + esize > (size_t) atom_data_size)
		return -1;

	trak = trak_vec[trak_num - 1];
	trak->chunks = entries;
	TSDebug(PLUGIN_NAME, "mp4_read_stco_atom chunks=%d", trak->chunks);
	trak->atoms[MP4_STCO_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STCO_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STCO_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STCO_ATOM].buffer, meta_reader,
			sizeof(mp4_stco_atom), 0);

	trak->atoms[MP4_STCO_DATA].buffer = TSIOBufferCreate();
	trak->atoms[MP4_STCO_DATA].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_STCO_DATA].buffer);
	TSIOBufferCopy(trak->atoms[MP4_STCO_DATA].buffer, meta_reader, esize,
			sizeof(mp4_stco_atom));

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}

/**
 *  64 位
 */
int Mp4Meta::mp4_read_co64_atom(int64_t atom_header_size,
		int64_t atom_data_size) {
	int32_t entries;
	int64_t esize;
	mp4_co64_atom co64;
	Mp4Trak *trak;

	if (sizeof(mp4_co64_atom) - 8 > (size_t) atom_data_size)
		return -1;

	IOBufferReaderCopy(meta_reader, &co64, sizeof(mp4_co64_atom));
	entries = mp4_get_32value(co64.entries);
	esize = entries * sizeof(int64_t);

	if (sizeof(mp4_co64_atom) - 8 + esize > (size_t) atom_data_size)
		return -1;

	trak = trak_vec[trak_num - 1];
	trak->chunks = entries; //chunk offset的数目

	trak->atoms[MP4_CO64_ATOM].buffer = TSIOBufferCreate();
	trak->atoms[MP4_CO64_ATOM].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_CO64_ATOM].buffer);
	TSIOBufferCopy(trak->atoms[MP4_CO64_ATOM].buffer, meta_reader,
			sizeof(mp4_co64_atom), 0);

	trak->atoms[MP4_CO64_DATA].buffer = TSIOBufferCreate();
	trak->atoms[MP4_CO64_DATA].reader = TSIOBufferReaderAlloc(
			trak->atoms[MP4_CO64_DATA].buffer);
	TSIOBufferCopy(trak->atoms[MP4_CO64_DATA].buffer, meta_reader, esize,
			sizeof(mp4_co64_atom));

	mp4_meta_consume(atom_data_size + atom_header_size);

	return 1;
}

/**
 * 当读到mdat 的时候说明解析成功了
 */
int Mp4Meta::mp4_read_mdat_atom(int64_t /* atom_header_size ATS_UNUSED */,
		int64_t /* atom_data_size ATS_UNUSED */) {
	mdat_atom.buffer = TSIOBufferCreate();
	mdat_atom.reader = TSIOBufferReaderAlloc(mdat_atom.buffer);

	meta_complete = true;
	return 1;
}

int Mp4Meta::mp4_get_start_sample(Mp4Trak *trak) {
	uint32_t start_sample, entries, sample_size, i;
	uint32_t key_sample, old_sample;
	TSIOBufferReader readerp;
	uint32_t size;
	off_t pass_chunk_sample_size;

	if (this->is_rs_find) { //说明已经找到视频的start_sample 了，开始到mp4_update_stts_atom根据时间查找音频的start_sample
		return 0;
	}

	if (trak->atoms[MP4_CO64_DATA].buffer) { //chunk offset box
		if(mp4_get_start_chunk_offset_co64(trak) != 0)
			return -1;
	} else {
		if(mp4_get_start_chunk_offset_stco(trak) != 0)
			return -1;
	}

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSC_DATA].reader);
	//entry 1:first chunk 1, samples pre chunk 13, sample description 1 ('self-ref')
	//chunk 1 sample 1 id 1
	//chunk 490 sample 3 id 1
	//第500个sample 500 ＝ 28 ＊ 13 ＋ 12 ＋ 13*9 ＋ 7

	uint32_t chunk,samples, next_chunk, sum_chunks, now_chunks;
	chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
	samples = mp4_reader_get_32value(readerp,offsetof(mp4_stsc_entry, samples));
//	id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));
	TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

	sum_chunks = 0;
	for (i = 1; i < trak->sample_to_chunk_entries; i++) {  //该trak一共有多少个chunk
		next_chunk = mp4_reader_get_32value(readerp,offsetof(mp4_stsc_entry, chunk));
		now_chunks = (next_chunk - chunk);
		sum_chunks += now_chunks;
		if (trak->start_chunk < sum_chunks) {
			trak->start_chunk_samples += ((trak->start_chunk + now_chunks) - sum_chunks) * samples;
			trak->last_start_chunk_samples = samples;

			goto stsc_found;
		}

		trak->start_chunk_samples += now_chunks* samples;

		chunk = next_chunk;
		samples = mp4_reader_get_32value(readerp,offsetof(mp4_stsc_entry, samples));
//		id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));
		TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
	}

	next_chunk = trak->chunks;
	now_chunks = (next_chunk - chunk);
	sum_chunks += now_chunks;
	if (trak->start_chunk < sum_chunks) {
		trak->start_chunk_samples += ((trak->start_chunk + now_chunks) - sum_chunks) * samples;
		trak->last_start_chunk_samples = samples;
	} else {
		return -1;
	}
//	//当sample_to_chunk_entries 为 1 的时候
//	trak->start_chunk_samples = trak->start_chunk * samples; //312
//	trak->last_start_chunk_samples = samples;

stsc_found:
	TSIOBufferReaderFree(readerp);


	TSDebug(PLUGIN_NAME, "mp4_get_start_sample start_chunk=%u,start_chunk_samples=%u,last_start_chunk_samples=%u",
			trak->start_chunk,trak->start_chunk_samples,trak->last_start_chunk_samples);



	entries = trak->sample_sizes_entries;

	start_sample = 0;
	pass_chunk_sample_size = trak->start_chunk_size;
	size = mp4_reader_get_32value(trak->atoms[MP4_STSZ_ATOM].reader,offsetof(mp4_stsz_atom, uniform_size));
	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSZ_DATA].reader);
	if (size == 0) { //全部sample 数目，如果所有的sample有相同的长度，这个字段就是这个值，否则就是0
		for (i = 0; i < entries; i++) {
			sample_size = mp4_reader_get_32value(readerp, 0);
			start_sample++;
			if(trak->start_chunk_samples <  start_sample) {
				pass_chunk_sample_size += sample_size;
				if(pass_chunk_sample_size >= this->start) {
					trak->chunk_samples = start_sample;
					goto found;
				}

			}
			if( (trak->start_chunk_samples + trak->last_start_chunk_samples) <= start_sample) {
				trak->chunk_samples = trak->last_start_chunk_samples;
				goto found;
			}

			TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
		}

	} else {
		for (i = 0; i < entries; i++) {
			sample_size = size;
			start_sample++;
			if(trak->start_chunk_samples <  start_sample) {
				pass_chunk_sample_size += sample_size;
				if(pass_chunk_sample_size >= this->start) {
					trak->chunk_samples = start_sample;
					goto found;
				}
			}
			if( (trak->start_chunk_samples + trak->last_start_chunk_samples) <= start_sample) {
				trak->chunk_samples = trak->last_start_chunk_samples;
				goto found;
			}

//			TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
		}
	}

found:
	TSIOBufferReaderFree(readerp);
	old_sample = start_sample; //已经检查过的sample
	//到 Sync Sample Box 找最适合的关键帧
	key_sample = this->mp4_find_key_sample(start_sample, trak); // find the last key frame before start_sample
	if (old_sample != key_sample) {
		start_sample = key_sample - 1;
	}
	TSDebug(PLUGIN_NAME,
			"mp4_update_stsz_atom old_sample=%d, start_sample=%d, key_sample = %d",
			old_sample, start_sample, key_sample);

	trak->start_sample = start_sample; //找到start_sample

	if(trak->start_sample <=0 ) {
		return -1;
	}

	return 0;
}

int Mp4Meta::mp4_get_start_chunk_offset_co64(Mp4Trak *trak) {
	int64_t pos, avail, offset, start_chunk;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_CO64_DATA].buffer == NULL)
		return -1;
	start_chunk = 0;
	offset = 0;
	readerp = TSIOBufferReaderClone(trak->atoms[MP4_CO64_DATA].reader);
	avail = TSIOBufferReaderAvail(readerp);

	for (pos = 0; pos < avail; pos += sizeof(uint64_t)) {
		offset = mp4_reader_get_64value(readerp, 0);
		if(this->start < offset) {
			break;
		}
		start_chunk++;
		trak->start_chunk_size = offset;
		mp4_reader_set_64value(readerp, 0, offset);
		TSIOBufferReaderConsume(readerp, sizeof(uint64_t));
	}

	TSIOBufferReaderFree(readerp);

	trak->start_chunk = start_chunk;
//	TSDebug(PLUGIN_NAME,
//			"mp4_get_start_chunk_offset_co64 start_chunk=%d, start_chunk_size=%ld",
//			trak->start_chunk, trak->start_chunk_size);

	return 0;
}

int Mp4Meta::mp4_get_start_chunk_offset_stco(Mp4Trak *trak) {
	int64_t pos, avail, offset, start_chunk;
	TSIOBufferReader readerp;
	if (trak->atoms[MP4_STCO_DATA].buffer == NULL)
		return -1;
	start_chunk = 0;
	offset = 0;
	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STCO_DATA].reader);
	avail = TSIOBufferReaderAvail(readerp);

	for (pos = 0; pos < avail; pos += sizeof(uint32_t)) {
		offset = mp4_reader_get_32value(readerp, 0);
		if( this->start < offset ) {
			break;
		}
		start_chunk++;
		trak->start_chunk_size = offset;
		mp4_reader_set_32value(readerp, 0, offset);
		TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
	}

	TSIOBufferReaderFree(readerp);
	trak->start_chunk = start_chunk;
//	TSDebug(PLUGIN_NAME,
//			"mp4_get_start_chunk_offset_stco start_chunk=%d, start_chunk_size=%ld",
//			trak->start_chunk, trak->start_chunk_size);
	return 0;
}

int Mp4Meta::mp4_update_stsz_atom(Mp4Trak *trak) {
	uint32_t i;
	int64_t atom_size, avail;
	uint32_t pass;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_STSZ_DATA].buffer == NULL)
		return 0;

	if (trak->start_sample > trak->sample_sizes_entries)
		return -1;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSZ_DATA].reader);
	avail = TSIOBufferReaderAvail(readerp);

	pass = trak->start_sample * sizeof(uint32_t);

	TSIOBufferReaderConsume(readerp,
			pass - sizeof(uint32_t) * (trak->chunk_samples));

	for (i = 0; i < trak->chunk_samples; i++) {
		trak->chunk_samples_size += mp4_reader_get_32value(readerp, 0);
		TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
	}

	atom_size = sizeof(mp4_stsz_atom) + avail - pass;
	trak->size += atom_size;

	mp4_reader_set_32value(trak->atoms[MP4_STSZ_ATOM].reader,
			offsetof(mp4_stsz_atom, size), atom_size);
	mp4_reader_set_32value(trak->atoms[MP4_STSZ_ATOM].reader,
			offsetof(mp4_stsz_atom, entries),
			trak->sample_sizes_entries - trak->start_sample);

	TSIOBufferReaderConsume(trak->atoms[MP4_STSZ_DATA].reader, pass);
	TSIOBufferReaderFree(readerp);

	return 0;
}

int Mp4Meta::mp4_update_stts_atom(Mp4Trak *trak) {
	uint32_t i, entries, count, duration, pass;
	uint32_t start_sample, left, start_count;
	uint32_t key_sample, old_sample;
	uint64_t start_time, sum;
	int64_t atom_size;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_STTS_DATA].buffer == NULL)
		return -1;

	sum = start_count = start_time = 0;
	duration = count = pass = 0;
	entries = trak->time_to_sample_entries; //number of entries
	if (this->rs > 0 && trak->start_sample == 0) {
		start_time = (uint64_t) (this->rs * trak->timescale / 1000);
//		TSDebug(PLUGIN_NAME, "mp4_update_stts_atom (this->rs > 0)= %lf",this->rs);
//		TSDebug(PLUGIN_NAME, "mp4_update_stts_atom start_time = %ld entries=%d",start_time, entries);
		//先查找start_sample
		start_sample = 0; //开始start_sample
		readerp = TSIOBufferReaderClone(trak->atoms[MP4_STTS_DATA].reader);
		for (i = 0; i < entries; i++) { //根据时间查找
			duration = (uint32_t) mp4_reader_get_32value(readerp,offsetof(mp4_stts_entry, duration));
			count = (uint32_t) mp4_reader_get_32value(readerp,offsetof(mp4_stts_entry, count));
//			TSDebug(PLUGIN_NAME,"mp4_update_stts_atom duration = %u, count = %u", duration,count);
			if (start_time < (uint64_t) count * duration) {
				pass = (uint32_t) (start_time / duration);
				start_sample += pass;

				goto audio_found;
			}

			start_sample += count;
			start_time -= (uint64_t) count * duration;
			TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
		}

		audio_found:

		TSIOBufferReaderFree(readerp);

		old_sample = start_sample; //已经检查过的sample
		//到 Sync Sample Box 找最适合的关键帧  返回sample序号
		key_sample = this->mp4_find_key_sample(start_sample, trak); // find the last key frame before start_sample

		if (old_sample != key_sample) {
			start_sample = key_sample - 1;
		}

		trak->start_sample = start_sample; //找到start_sample
	}

	start_sample = trak->start_sample;
	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STTS_DATA].reader);
	sum = start_count = start_time = 0;
	duration = count = pass = 0;
	//更新time to sample box
	for (i = 0; i < entries; i++) {
		duration = (uint32_t) mp4_reader_get_32value(readerp,offsetof(mp4_stts_entry, duration));
		count = (uint32_t) mp4_reader_get_32value(readerp,offsetof(mp4_stts_entry, count));
		if (start_sample < count) {
			count -= start_sample;
			mp4_reader_set_32value(readerp, offsetof(mp4_stts_entry, count),count);

			//计算总共丢弃的duration
			sum += (uint64_t) start_sample * duration;
			break;
		}

		start_sample -= count;
		sum += (uint64_t) count * duration;

		TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
	}

	if (this->rs == 0) {
		//实际时间 0.2s  对应的duration = mdhd.timescale * 0.2s
		// 丢弃了多少时间 ＝ 多个sample ＊ 每个sample等于多少秒 * 1000
		this->rs = ((double) sum / trak->duration) * ((double) trak->duration / trak->timescale) * 1000;
//		TSDebug(PLUGIN_NAME, "mp4_update_stsz_atom rs=%lf, sum=%ld", this->rs,
//				sum);
		this->is_rs_find = true;
	}

	left = entries - i;	  //之前遍历，丢弃了，剩下多少数据
//	TSDebug(PLUGIN_NAME, "mp4_update_stts_atom left=%u, entries=%u", left,entries);
	atom_size = sizeof(mp4_stts_atom) + left * sizeof(mp4_stts_entry);
	trak->size += atom_size;	  //默认位0 开始累加
//	TSDebug(PLUGIN_NAME, "mp4_update_stts_atom trak->size=%lu", trak->size);

	mp4_reader_set_32value(trak->atoms[MP4_STTS_ATOM].reader,
			offsetof(mp4_stts_atom, size), atom_size);
	mp4_reader_set_32value(trak->atoms[MP4_STTS_ATOM].reader,
			offsetof(mp4_stts_atom, entries), left);
	TSIOBufferReaderConsume(trak->atoms[MP4_STTS_DATA].reader,
			i * sizeof(mp4_stts_entry));

	TSIOBufferReaderFree(readerp);
	return 0;
}

int Mp4Meta::mp4_update_stss_atom(Mp4Trak *trak) {
	int64_t atom_size;
	uint32_t i, j, entries, sample, start_sample, left;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_STSS_DATA].buffer == NULL)
		return 0;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSS_DATA].reader);

	start_sample = trak->start_sample + 1;
	entries = trak->sync_samples_entries;

	for (i = 0; i < entries; i++) {
		sample = (uint32_t) mp4_reader_get_32value(readerp, 0);

		if (sample >= start_sample) {
			goto found;
		}

		TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
	}

	TSIOBufferReaderFree(readerp);
	return -1;

	found:

	left = entries - i;

	start_sample = trak->start_sample;
	for (j = 0; j < left; j++) {
		sample = (uint32_t) mp4_reader_get_32value(readerp, 0);
		sample -= start_sample;
		mp4_reader_set_32value(readerp, 0, sample);
		TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
	}

	atom_size = sizeof(mp4_stss_atom) + left * sizeof(uint32_t);
	trak->size += atom_size;
//	TSDebug(PLUGIN_NAME, "mp4_update_stss_atom trak->size=%lu", trak->size);
	mp4_reader_set_32value(trak->atoms[MP4_STSS_ATOM].reader,
			offsetof(mp4_stss_atom, size), atom_size);

	mp4_reader_set_32value(trak->atoms[MP4_STSS_ATOM].reader,
			offsetof(mp4_stss_atom, entries), left);

	TSIOBufferReaderConsume(trak->atoms[MP4_STSS_DATA].reader,
			i * sizeof(uint32_t));
	TSIOBufferReaderFree(readerp);

	return 0;
}

int Mp4Meta::mp4_update_ctts_atom(Mp4Trak *trak) {
	int64_t atom_size;
	uint32_t i, entries, start_sample, left;
	uint32_t count;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_CTTS_DATA].buffer == NULL)
		return 0;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_CTTS_DATA].reader);

	start_sample = trak->start_sample + 1;
	entries = trak->composition_offset_entries;

	for (i = 0; i < entries; i++) {
		count = (uint32_t) mp4_reader_get_32value(readerp,
				offsetof(mp4_ctts_entry, count));

		if (start_sample <= count) {
			count -= (start_sample - 1);
			mp4_reader_set_32value(readerp, offsetof(mp4_ctts_entry, count),
					count);
			goto found;
		}

		start_sample -= count;
		TSIOBufferReaderConsume(readerp, sizeof(mp4_ctts_entry));
	}

	if (trak->atoms[MP4_CTTS_ATOM].reader) {
		TSIOBufferReaderFree(trak->atoms[MP4_CTTS_ATOM].reader);
		TSIOBufferDestroy(trak->atoms[MP4_CTTS_ATOM].buffer);

		trak->atoms[MP4_CTTS_ATOM].buffer = NULL;
		trak->atoms[MP4_CTTS_ATOM].reader = NULL;
	}

	TSIOBufferReaderFree(trak->atoms[MP4_CTTS_DATA].reader);
	TSIOBufferDestroy(trak->atoms[MP4_CTTS_DATA].buffer);

	trak->atoms[MP4_CTTS_DATA].reader = NULL;
	trak->atoms[MP4_CTTS_DATA].buffer = NULL;

	TSIOBufferReaderFree(readerp);
	return 0;

	found:

	left = entries - i;
	atom_size = sizeof(mp4_ctts_atom) + left * sizeof(mp4_ctts_entry);
	trak->size += atom_size;
//	TSDebug(PLUGIN_NAME, "mp4_update_ctts_atom trak->size=%lu", trak->size);
	mp4_reader_set_32value(trak->atoms[MP4_CTTS_ATOM].reader,
			offsetof(mp4_ctts_atom, size), atom_size);
	mp4_reader_set_32value(trak->atoms[MP4_CTTS_ATOM].reader,
			offsetof(mp4_ctts_atom, entries), left);

	TSIOBufferReaderConsume(trak->atoms[MP4_CTTS_DATA].reader,
			i * sizeof(mp4_ctts_entry));
	TSIOBufferReaderFree(readerp);

	return 0;
}

int Mp4Meta::mp4_update_stsc_atom(Mp4Trak *trak) {
	int64_t atom_size;
	uint32_t i, entries, samples, start_sample;
	uint32_t chunk, next_chunk, n, id, j;
	mp4_stsc_entry *first;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_STSC_DATA].buffer == NULL)
		return -1;

	if (trak->sample_to_chunk_entries == 0)
		return -1;

	start_sample = (uint32_t) trak->start_sample;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSC_DATA].reader);

	//entry 1:first chunk 1, samples pre chunk 13, sample description 1 ('self-ref')
	//chunk 1 sample 1 id 1
	//chunk 490 sample 3 id 1
	//第500个sample 500 ＝ 28 ＊ 13 ＋ 12 ＋ 13*9 ＋ 7

	chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
	samples = mp4_reader_get_32value(readerp,
			offsetof(mp4_stsc_entry, samples));
	id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));

	TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

	for (i = 1; i < trak->sample_to_chunk_entries; i++) {  //该trak一共有多少个chunk
		next_chunk = mp4_reader_get_32value(readerp,
				offsetof(mp4_stsc_entry, chunk));
		n = (next_chunk - chunk) * samples;

		if (start_sample <= n) {
			goto found;
		}

		start_sample -= n;

		chunk = next_chunk;
		samples = mp4_reader_get_32value(readerp,
				offsetof(mp4_stsc_entry, samples));
		id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));
		TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
	}

	next_chunk = trak->chunks; //chunk offset的数目 最后一个chunk

	n = (next_chunk - chunk) * samples; //start_sample 最大就是本身 超过就算出错处理, = 0 就算对了
	if (start_sample > n) {
		TSIOBufferReaderFree(readerp);
		return -1;
	}

	found:

	TSIOBufferReaderFree(readerp);

	entries = trak->sample_to_chunk_entries - i + 1;
	if (samples == 0)
		return -1;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSC_DATA].reader);
	TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry) * (i - 1));

	trak->start_chunk = chunk - 1;
	trak->start_chunk += start_sample / samples; //312
	trak->chunk_samples = start_sample % samples; //还有几个sample 未加入chunk中的 0
//	TSDebug(PLUGIN_NAME,
//			"mp4_update_stsc_atom bbbb start_chunk=%u, chunk_samples=%u, i=%u",
//			trak->start_chunk, trak->chunk_samples, i);
	atom_size = sizeof(mp4_stsc_atom) + entries * sizeof(mp4_stsc_entry);

	mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), 1);

	if (trak->chunk_samples && next_chunk - trak->start_chunk == 2) {
		mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, samples),
				samples - trak->chunk_samples);

	} else if (trak->chunk_samples) {
		first = &trak->stsc_chunk_entry;
		mp4_set_32value(first->chunk, 1);
		mp4_set_32value(first->samples, samples - trak->chunk_samples);
		mp4_set_32value(first->id, id);

		trak->atoms[MP4_STSC_CHUNK].buffer = TSIOBufferSizedCreate(
				TS_IOBUFFER_SIZE_INDEX_128);
		trak->atoms[MP4_STSC_CHUNK].reader = TSIOBufferReaderAlloc(
				trak->atoms[MP4_STSC_CHUNK].buffer);
		TSIOBufferWrite(trak->atoms[MP4_STSC_CHUNK].buffer, first,
				sizeof(mp4_stsc_entry));

		mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), 2);

		entries++;
		atom_size += sizeof(mp4_stsc_entry);
	}

	TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
//	TSDebug(PLUGIN_NAME, "mp4_update_stsc_atom sample_to_chunk_entries=%u",
//			trak->sample_to_chunk_entries);
	for (j = i; j < trak->sample_to_chunk_entries; j++) {
		chunk = mp4_reader_get_32value(readerp,
				offsetof(mp4_stsc_entry, chunk));
		chunk -= trak->start_chunk;
		mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), chunk);
		TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
	}

	trak->size += atom_size;
//	TSDebug(PLUGIN_NAME, "mp4_update_stsc_atom trak->size=%lu", trak->size);
	mp4_reader_set_32value(trak->atoms[MP4_STSC_ATOM].reader,
			offsetof(mp4_stsc_atom, size), atom_size);
	mp4_reader_set_32value(trak->atoms[MP4_STSC_ATOM].reader,
			offsetof(mp4_stsc_atom, entries), entries);

	TSIOBufferReaderConsume(trak->atoms[MP4_STSC_DATA].reader,
			(i - 1) * sizeof(mp4_stsc_entry));
	TSIOBufferReaderFree(readerp);

	return 0;
}

int Mp4Meta::mp4_update_co64_atom(Mp4Trak *trak) {
	int64_t atom_size, avail, pass;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_CO64_DATA].buffer == NULL)
		return -1;

	if (trak->start_chunk > trak->chunks)
		return -1;

	readerp = trak->atoms[MP4_CO64_DATA].reader;
	avail = TSIOBufferReaderAvail(readerp);

	pass = trak->start_chunk * sizeof(uint64_t);
	atom_size = sizeof(mp4_co64_atom) + avail - pass;
	trak->size += atom_size;
//	TSDebug(PLUGIN_NAME, "mp4_update_co64_atom trak->size=%lu", trak->size);
	TSIOBufferReaderConsume(readerp, pass);
	trak->start_offset = mp4_reader_get_64value(readerp, 0);
	trak->start_offset += trak->chunk_samples_size;
	mp4_reader_set_64value(readerp, 0, trak->start_offset);

	mp4_reader_set_32value(trak->atoms[MP4_CO64_ATOM].reader,
			offsetof(mp4_co64_atom, size), atom_size);
	mp4_reader_set_32value(trak->atoms[MP4_CO64_ATOM].reader,
			offsetof(mp4_co64_atom, entries), trak->chunks - trak->start_chunk);

	return 0;
}

int Mp4Meta::mp4_update_stco_atom(Mp4Trak *trak) {
	int64_t atom_size, avail;
	uint32_t pass;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_STCO_DATA].buffer == NULL)
		return -1;

	if (trak->start_chunk > trak->chunks)
		return -1;

	readerp = trak->atoms[MP4_STCO_DATA].reader;
	avail = TSIOBufferReaderAvail(readerp);

	pass = trak->start_chunk * sizeof(uint32_t);
	atom_size = sizeof(mp4_stco_atom) + avail - pass;
	trak->size += atom_size;
	TSIOBufferReaderConsume(readerp, pass);

	trak->start_offset = mp4_reader_get_32value(readerp, 0);
	trak->start_offset += trak->chunk_samples_size;
	mp4_reader_set_32value(readerp, 0, trak->start_offset);

	mp4_reader_set_32value(trak->atoms[MP4_STCO_ATOM].reader,
			offsetof(mp4_stco_atom, size), atom_size);
	mp4_reader_set_32value(trak->atoms[MP4_STCO_ATOM].reader,
			offsetof(mp4_stco_atom, entries), trak->chunks - trak->start_chunk);

//	TSDebug(PLUGIN_NAME,
//			"mp4_update_stco_atom start_offset=%ld chunks=%d, start_chunk=%d",
//			trak->start_offset, trak->chunks, trak->start_chunk);
	return 0;
}

int Mp4Meta::mp4_update_stbl_atom(Mp4Trak *trak) {
	trak->size += sizeof(mp4_atom_header);
	mp4_reader_set_32value(trak->atoms[MP4_STBL_ATOM].reader, 0, trak->size);

	return 0;
}

int Mp4Meta::mp4_update_minf_atom(Mp4Trak *trak) {
	trak->size += sizeof(mp4_atom_header) + trak->vmhd_size + trak->smhd_size
			+ trak->dinf_size;
	mp4_reader_set_32value(trak->atoms[MP4_MINF_ATOM].reader, 0, trak->size);

	return 0;
}

int Mp4Meta::mp4_update_mdia_atom(Mp4Trak *trak) {
	trak->size += sizeof(mp4_atom_header);
	mp4_reader_set_32value(trak->atoms[MP4_MDIA_ATOM].reader, 0, trak->size);

	return 0;
}

int Mp4Meta::mp4_update_trak_atom(Mp4Trak *trak) {
	trak->size += sizeof(mp4_atom_header);
	mp4_reader_set_32value(trak->atoms[MP4_TRAK_ATOM].reader, 0, trak->size);

	return 0;
}

int Mp4Meta::mp4_adjust_co64_atom(Mp4Trak *trak, off_t adjustment) {
	int64_t pos, avail, offset;
	TSIOBufferReader readerp;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_CO64_DATA].reader);
	avail = TSIOBufferReaderAvail(readerp);

	for (pos = 0; pos < avail; pos += sizeof(uint64_t)) {
		offset = mp4_reader_get_64value(readerp, 0);
		offset += adjustment;
		mp4_reader_set_64value(readerp, 0, offset);
		TSIOBufferReaderConsume(readerp, sizeof(uint64_t));
	}

	TSIOBufferReaderFree(readerp);

	return 0;
}

int Mp4Meta::mp4_adjust_stco_atom(Mp4Trak *trak, int32_t adjustment) {
	int64_t pos, avail, offset;
	TSIOBufferReader readerp;

	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STCO_DATA].reader);
	avail = TSIOBufferReaderAvail(readerp);

	for (pos = 0; pos < avail; pos += sizeof(uint32_t)) {
		offset = mp4_reader_get_32value(readerp, 0);
		offset += adjustment;
		mp4_reader_set_32value(readerp, 0, offset);
		TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
	}

	TSIOBufferReaderFree(readerp);

	return 0;
}

int64_t Mp4Meta::mp4_update_mdat_atom(int64_t start_offset) {
	int64_t atom_data_size;
	int64_t atom_size;
	int64_t atom_header_size;
	u_char *atom_header;

	//总长度－ 丢弃的长度 － drm header 以及des解密减少的字节和des(videoid)的长度
//  TSDebug(PLUGIN_NAME, "mp4_update_mdat_atom drm_length=%ld",this->drm_length);
	atom_data_size = this->original_file_size - start_offset;
	this->start_pos = start_offset + this->tag_pos;
	atom_header = mdat_atom_header;

	if (atom_data_size > 0xffffffff) {
		atom_size = 1;
		atom_header_size = sizeof(mp4_atom_header64);
		mp4_set_64value(atom_header + sizeof(mp4_atom_header),
				sizeof(mp4_atom_header64) + atom_data_size);

	} else {
		atom_size = sizeof(mp4_atom_header) + atom_data_size;
		atom_header_size = sizeof(mp4_atom_header);
	}

	this->content_length += atom_header_size + atom_data_size;

//	TSDebug(PLUGIN_NAME,"mp4_update_mdat_atom atom_header_size=%ld content_length=%ld",
//				atom_header_size, this->content_length);

	mp4_set_32value(atom_header, atom_size);
	mp4_set_atom_name(atom_header, 'm', 'd', 'a', 't');

	mdat_atom.buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_128); //这里创建了一个头
	mdat_atom.reader = TSIOBufferReaderAlloc(mdat_atom.buffer);

	TSIOBufferWrite(mdat_atom.buffer, atom_header, atom_header_size);

	return atom_header_size;
}

/**
 * Sync Sample Box
 * size, type, version flags, number of entries
 *
 * 该box 决定了整个mp4 文件是否可以拖动，如果box 只有一个entry,则拖拉时，进度到最后
 */
uint32_t Mp4Meta::mp4_find_key_sample(uint32_t start_sample, Mp4Trak *trak) {
	uint32_t i;
	uint32_t sample, prev_sample, entries;
	TSIOBufferReader readerp;

	if (trak->atoms[MP4_STSS_DATA].buffer == NULL)
		return start_sample;

	prev_sample = 1;
	entries = trak->sync_samples_entries;
//	TSDebug(PLUGIN_NAME, "mp4_find_key_sample sync_samples_entries=%u",
//			entries);
	readerp = TSIOBufferReaderClone(trak->atoms[MP4_STSS_DATA].reader);

	for (i = 0; i < entries; i++) {
		sample = (uint32_t) mp4_reader_get_32value(readerp, 0);
//		TSDebug(PLUGIN_NAME, "mp4_find_key_sample sample=%u", sample);
		if (sample > start_sample) {
			goto found;
		}

		prev_sample = sample;
		TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
	}

	found:

	TSIOBufferReaderFree(readerp);
	return prev_sample;
}

void Mp4Meta::mp4_update_mvhd_duration() {
	int64_t need;
	uint64_t duration, cut;
	mp4_mvhd_atom *mvhd;
	mp4_mvhd64_atom mvhd64;

	need = TSIOBufferReaderAvail(mvhd_atom.reader);

	if (need > (int64_t) sizeof(mp4_mvhd64_atom))
		need = sizeof(mp4_mvhd64_atom);

	IOBufferReaderCopy(mvhd_atom.reader, &mvhd64, need);
	mvhd = (mp4_mvhd_atom *) &mvhd64;

	if (this->rs > 0) {
		cut = (uint64_t) (this->rs * this->timescale / 1000);

	} else {
		cut = this->start * this->timescale / 1000;
	}

	TSDebug(PLUGIN_NAME, "yyyyy cut=%ld", cut);

	if (mvhd->version[0] == 0) {
		duration = mp4_get_32value(mvhd->duration);
		duration -= cut;
		mp4_reader_set_32value(mvhd_atom.reader,
				offsetof(mp4_mvhd_atom, duration), duration);

	} else { // 64-bit duration
		duration = mp4_get_64value(mvhd64.duration);
		duration -= cut;
		mp4_reader_set_64value(mvhd_atom.reader,
				offsetof(mp4_mvhd64_atom, duration), duration);
	}
}

void Mp4Meta::mp4_update_tkhd_duration(Mp4Trak *trak) {
	int64_t need, cut;
	mp4_tkhd_atom *tkhd_atom;
	mp4_tkhd64_atom tkhd64_atom;
	int64_t duration;

	need = TSIOBufferReaderAvail(trak->atoms[MP4_TKHD_ATOM].reader);

	if (need > (int64_t) sizeof(mp4_tkhd64_atom))
		need = sizeof(mp4_tkhd64_atom);

	IOBufferReaderCopy(trak->atoms[MP4_TKHD_ATOM].reader, &tkhd64_atom, need);
	tkhd_atom = (mp4_tkhd_atom *) &tkhd64_atom;

	if (this->rs > 0) {
		cut = (uint64_t) (this->rs * this->timescale / 1000);

	} else {
		cut = this->start * this->timescale / 1000;
	}

	if (tkhd_atom->version[0] == 0) {
		duration = mp4_get_32value(tkhd_atom->duration);
		duration -= cut;
		mp4_reader_set_32value(trak->atoms[MP4_TKHD_ATOM].reader,
				offsetof(mp4_tkhd_atom, duration), duration);

	} else {
		duration = mp4_get_64value(tkhd64_atom.duration);
		duration -= cut;
		mp4_reader_set_64value(trak->atoms[MP4_TKHD_ATOM].reader,
				offsetof(mp4_tkhd64_atom, duration), duration);
	}
}

void Mp4Meta::mp4_update_mdhd_duration(Mp4Trak *trak) {
	int64_t duration, need, cut;
	mp4_mdhd_atom *mdhd;
	mp4_mdhd64_atom mdhd64;

	memset(&mdhd64, 0, sizeof(mp4_mdhd64_atom));

	need = TSIOBufferReaderAvail(trak->atoms[MP4_MDHD_ATOM].reader);

	if (need > (int64_t) sizeof(mp4_mdhd64_atom))
		need = sizeof(mp4_mdhd64_atom);

	IOBufferReaderCopy(trak->atoms[MP4_MDHD_ATOM].reader, &mdhd64, need);
	mdhd = (mp4_mdhd_atom *) &mdhd64;

	if (this->rs > 0) {
		cut = (uint64_t) (this->rs * trak->timescale / 1000);
	} else {
		cut = this->start * trak->timescale / 1000;
	}

	if (mdhd->version[0] == 0) {
		duration = mp4_get_32value(mdhd->duration);
		duration -= cut;
		mp4_reader_set_32value(trak->atoms[MP4_MDHD_ATOM].reader,
				offsetof(mp4_mdhd_atom, duration), duration);
	} else {
		duration = mp4_get_64value(mdhd64.duration);
		duration -= cut;
		mp4_reader_set_64value(trak->atoms[MP4_MDHD_ATOM].reader,
				offsetof(mp4_mdhd64_atom, duration), duration);
	}
}

static void mp4_reader_set_32value(TSIOBufferReader readerp, int64_t offset,
		uint32_t n) {
	int pos;
	int64_t avail, left;
	TSIOBufferBlock blk;
	const char *start;
	u_char *ptr;

	pos = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);

		if (avail <= offset) {
			offset -= avail;

		} else {
			left = avail - offset;
			ptr = (u_char *) (const_cast<char *>(start) + offset);

			while (pos < 4 && left > 0) {
				*ptr++ = (u_char) ((n) >> ((3 - pos) * 8));
				pos++;
				left--;
			}

			if (pos >= 4)
				return;

			offset = 0;
		}

		blk = TSIOBufferBlockNext(blk);
	}
}

static void mp4_reader_set_64value(TSIOBufferReader readerp, int64_t offset,
		uint64_t n) {
	int pos;
	int64_t avail, left;
	TSIOBufferBlock blk;
	const char *start;
	u_char *ptr;

	pos = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);

		if (avail <= offset) {
			offset -= avail;

		} else {
			left = avail - offset;
			ptr = (u_char *) (const_cast<char *>(start) + offset);

			while (pos < 8 && left > 0) {
				*ptr++ = (u_char) ((n) >> ((7 - pos) * 8));
				pos++;
				left--;
			}

			if (pos >= 4)
				return;

			offset = 0;
		}

		blk = TSIOBufferBlockNext(blk);
	}
}

static uint32_t mp4_reader_get_32value(TSIOBufferReader readerp,
		int64_t offset) {
	int pos;
	int64_t avail, left;
	TSIOBufferBlock blk;
	const char *start;
	const u_char *ptr;
	u_char res[4];

	pos = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);

		if (avail <= offset) {
			offset -= avail;

		} else {
			left = avail - offset;
			ptr = (u_char *) (start + offset);

			while (pos < 4 && left > 0) {
				res[3 - pos] = *ptr++;
				pos++;
				left--;
			}

			if (pos >= 4) {
				return *(uint32_t *) res;
			}

			offset = 0;
		}

		blk = TSIOBufferBlockNext(blk);
	}

	return -1;
}

static uint64_t mp4_reader_get_64value(TSIOBufferReader readerp,
		int64_t offset) {
	int pos;
	int64_t avail, left;
	TSIOBufferBlock blk;
	const char *start;
	u_char *ptr;
	u_char res[8];

	pos = 0;
	blk = TSIOBufferReaderStart(readerp);

	while (blk) {
		start = TSIOBufferBlockReadStart(blk, readerp, &avail);

		if (avail <= offset) {
			offset -= avail;

		} else {
			left = avail - offset;
			ptr = (u_char *) (start + offset);

			while (pos < 8 && left > 0) {
				res[7 - pos] = *ptr++;
				pos++;
				left--;
			}

			if (pos >= 8) {
				return *(uint64_t *) res;
			}

			offset = 0;
		}

		blk = TSIOBufferBlockNext(blk);
	}

	return -1;
}

