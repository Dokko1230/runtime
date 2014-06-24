#include "tm.h"
#include "greatest-buf.h"

inline void print_buffer (uint8_t* buf, size_t len) {
	int i = 0;
	for (i = 0; i < len; i++) {
		printf("%02X ", buf[i]);
	}
	printf("\n");
}


/**
 * tm test
 */


TEST floats()
{
	uint8_t buf[16] = { 0 };

	uint8_t buf_le[16] = { 0, 0x00, 0x00, 0x80, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	tm_buffer_float_write (buf, 1, 1.0, LE);
	ASSERT_BUF_EQ(buf_le, buf);

	uint8_t buf_ge[16] = { 0, 0x3f, 0x80, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	tm_buffer_float_write (buf, 1, 1.0, BE);
	ASSERT_BUF_EQ(buf_ge, buf);

	PASS();
}


TEST doubles()
{
	uint8_t buf[16] = { 0 };

	uint8_t buf_le[16] = { 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, 0, 0, 0, 0, 0, 0, 0 };
	tm_buffer_double_write (buf, 1, 1.0, LE);
	ASSERT_BUF_EQ(buf_le, buf);

	uint8_t buf_ge[16] = { 0, 0x3f, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0 };
	tm_buffer_double_write (buf, 1, 1.0, BE);
	ASSERT_BUF_EQ(buf_ge, buf);

	PASS();
}

SUITE(tm_buf)
{
	RUN_TEST(floats);
	RUN_TEST(doubles);
}


TEST random_test()
{
	size_t read = 0;
	uint8_t buf[16] = { 0 };
	tm_entropy_seed();
	tm_random_bytes(buf, sizeof(buf), &read);
	// print_buffer(buf, sizeof(buf));

	PASS();
}

SUITE(tm_random)
{
	RUN_TEST(random_test);
}


#include <miniz.h>
#define MZ_MIN(a,b) (((a)<(b))?(a):(b))

// Minimum out_len length of 16

typedef tdefl_compressor tm_deflate_t;

static void tm_buffer_write_uint32le (uint8_t *p, uint32_t n)
{
	p[0] = n >>  0;
	p[1] = n >>  8;
	p[2] = n >> 16;
	p[3] = n >> 24;
}

int tm_deflate_start_gzip (tm_deflate_t* deflator, size_t level, uint8_t* out, size_t out_len, size_t* out_total)
{
	size_t out_written = out_len;
	*out_total = 0;

	// The number of dictionary probes to use at each compression level (0-10). 0=implies fastest/minimal possible probing.
	static const mz_uint s_tdefl_num_probes[11] = { 0, 1, 6, 32,  16, 32, 128, 256,  512, 768, 1500 };

	tdefl_status status;

	// create tdefl() compatible flags (we have to compose the low-level flags ourselves, or use tdefl_create_comp_flags_from_zip_params() but that means MINIZ_NO_ZLIB_APIS can't be defined).
	mz_uint comp_flags = s_tdefl_num_probes[MZ_MIN(10, level)] | ((level <= 3) ? TDEFL_GREEDY_PARSING_FLAG : 0);
	if (!level)
		comp_flags |= TDEFL_FORCE_ALL_RAW_BLOCKS;

	// Initialize the low-level compressor.
	status = tdefl_init(deflator, NULL, NULL, comp_flags);
	if (status != TDEFL_STATUS_OKAY) {
		return EPERM;
	}

	if (out_len - *out_total < 10) {
		return ENOSPC;
	}

	uint8_t hdr[10] = {
		0x1F, 0x8B,	/* magic */
		8,		/* z method */
		0,		/* flags */
		0,0,0,0,	/* mtime */
		0,		/* xfl */
		0xFF,		/* OS */
	};

	memcpy(out, hdr, 10);
	out_written = 10;
	*out_total += out_written;

	return 0;
}

mz_uint file_crc32 = MZ_CRC32_INIT;

int tm_deflate_write (tm_deflate_t* deflator, const uint8_t* in, size_t in_len, size_t* in_total, uint8_t* out, size_t out_len, size_t* out_total)
{
	size_t in_read = in_len, out_written = out_len;
	*in_total = 0;
	*out_total = 0;

	int status = tdefl_compress(deflator, in, &in_read, out, &out_written, TDEFL_NO_FLUSH);
	*in_total += in_read;
	*out_total += out_written;

	file_crc32=(mz_uint32)mz_crc32(file_crc32, in, in_read);

	return status;
}

int tm_deflate_end_gzip (tm_deflate_t* deflator, uint8_t* out, size_t out_len, size_t* out_total)
{
	size_t out_written = out_len;
	*out_total = 0;

	int status = tdefl_compress(deflator, NULL, 0, &out[*out_total], &out_written, TDEFL_FINISH);
	*out_total += out_written;
	if (status == TDEFL_STATUS_OKAY) {
		return ENOSPC;
	} else if (status != TDEFL_STATUS_DONE) {
		return EPERM;
	}

	if (out_len - *out_total < 8) {
		return ENOSPC;
	}

	tm_buffer_write_uint32le(&out[*out_total + 0], file_crc32);
	tm_buffer_write_uint32le(&out[*out_total + 4], 0x0c + 1);
	*out_total += 8;

	return 0;
}


unsigned char hello_gz[] = {
  0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x01, 0x0d,
  0x00, 0xf2, 0xff, 0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x20, 0x57, 0x4f, 0x52,
  0x4c, 0x44, 0x0a, 0x00, 0x1a, 0xdf, 0xa2, 0xe6, 0x0d, 0x00, 0x00, 0x00
};
unsigned int hello_gz_len = 36;

unsigned char* helloworld = (unsigned char *) "HELLO WORLD\n";
size_t helloworld_len = sizeof("HELLO WORLD\n");

TEST tm_deflate_test()
{
	uint8_t out[1024*16] = { 0 };
	size_t out_len = sizeof(out), out_written = 0, out_total = 0, in_read = 0;

	tm_deflate_t deflator;

	ASSERT_EQ(tm_deflate_start_gzip(&deflator, 1, &out[out_total], out_len, &out_written), 0);
	out_total += out_written;
	ASSERT_EQ(out_total, 10);
	ASSERT_EQ(out[0], 0x1F); // magic
	ASSERT_EQ(out[1], 0x8b); // magic

	ASSERT_EQ(tm_deflate_write(&deflator, helloworld, helloworld_len, &in_read, &out[out_total], out_len - out_total, &out_written), 0);
	out_total += out_written;
	ASSERT_EQ(in_read, helloworld_len);

	ASSERT_EQ(tm_deflate_end_gzip(&deflator, &out[out_total], out_len - out_total, &out_written), 0);
	out_total += out_written;

	// Check compiled version.
	ASSERT_BUF_N_EQ(hello_gz, out, hello_gz_len);

	// tm_deflate_start(); // raw deflate stream
	// tm_deflate_start_gzip(); // gzip header/trailer
	// tm_deflate_start_zlib(); // zlib header/trailer
	// tm_deflate_compress(obj, uint8_t* in, size_t in_len, uint8_t* out, size_t out_len);
	// tm_deflate_end();
	// tm_deflate_end_gzip();
	// tm_deflate_end_zlib();
	// tm_deflate_start_gzip();
	// tm_deflate_start_compress();
	// tm_deflate_start_end_gzip();

	PASS();
}

SUITE(tm_deflate)
{
	RUN_TEST(tm_deflate_test);
}



/**
 * runtime test
 */

// #include "colony.h"


// TEST colony(lua_State *L)
// {
// 	int stacksize = 0;
// 	size_t buf_len = 0;
// 	const uint8_t* buf = NULL;

// 	// test string -> buffer
// 	const char* str = "this is a cool string";
// 	lua_pushstring(L, str);
// 	buf_len = 0;
// 	stacksize = lua_gettop(L);
// 	buf = colony_tobuffer(L, -1, &buf_len);
// 	ASSERT_EQ(buf_len, strlen(str));
// 	ASSERT_EQ(strncmp((const char*) buf, str, buf_len), 0);
// 	ASSERT_EQm("colony_tobuffer doesn't grow or shrink stack", stacksize, lua_gettop(L));

// 	// test buffer -> buffer
// 	const uint8_t* newbuf = colony_createbuffer(L, 256);
// 	buf_len = 0;
// 	stacksize = lua_gettop(L);
// 	buf = colony_tobuffer(L, -1, &buf_len);
// 	ASSERT_EQ(buf_len, 256);
// 	ASSERT_EQ(strncmp((const char*) buf, (const char*) newbuf, buf_len), 0);
// 	ASSERT_EQm("colony_tobuffer doesn't grow or shrink stack", stacksize, lua_gettop(L));

// 	PASS();
// }


// SUITE(runtime)
// {
// 	lua_State *L = NULL;
// 	colony_runtime_open(&L);
// 	RUN_TESTp(colony, L);
// 	colony_runtime_close(&L);
// }


/**
 * entry
 */

GREATEST_MAIN_DEFS();

int main(int argc, char **argv)
{
	GREATEST_MAIN_BEGIN();      /* command-line arguments, initialization. */
	RUN_SUITE(tm_buf);
	RUN_SUITE(tm_random);
	RUN_SUITE(tm_deflate);
	// RUN_SUITE(runtime);
	GREATEST_MAIN_END();        /* display results */
}
