/***********************************************************************************************
zip_write.h - v0.1 (alpha) - github.com/andrei-drexler/toolbox - Public domain/MIT
************************************************************************************************
ABOUT:
	Minimalistic STB-style ZIP64 writer.
	Actual compression is performed by a slightly modified version of Sean Barrett's
	stbi_zlib_compress, from stb_image_write (https://github.com/nothings/stb).
	To quote Sean:
		"[...] output is not optimal; it is 20-50% larger than the file
		written by a decent optimizing implementation;
		[...] This library is designed for source code compactness and simplicity,
		not optimal image file size or run-time performance."

BUILDING:
	Before #including this header,
		#define ZIP_WRITE_IMPLEMENT
	in the file that you want to have the implementation.

	You can #define ZW_ASSERT(x) before the #include to avoid using assert.h.
	You can #define ZW_MALLOC(), ZW_REALLOC(), and ZW_FREE() to replace malloc, realloc, free.
	You can #define ZW_MEMMOVE() to replace memmove.

USAGE:
	// [main.c]
	#define ZIP_WRITE_IMPLEMENT
	#include "zip_write.h"

	int main() {
		zw_zip archive = zw_create("envelope.zip");
		zw_begin_file(archive, "letter.txt");
		zw_write_text(archive, "hello, world!");
		return zw_finish(archive) ? 0 : 1;
	}

LICENSE:
	MIT or public domain, whichever you prefer (see end of file).
***********************************************************************************************/
#ifndef ZIP_WRITE_H_INCLUDED
#define ZIP_WRITE_H_INCLUDED

#include <stddef.h>	// size_t

#ifdef __cplusplus
extern "C" {
#endif // def __cplusplus

typedef enum { zw_false, zw_true, } zw_bool;
typedef struct zw_zip_options zw_zip_options;
typedef struct zw__zip_details* zw_zip;

zw_zip                  zw_create(const char* file_path);
zw_zip                  zw_create_ex(const zw_zip_options* options);
zw_bool                 zw_begin_file(zw_zip archive, const char* file_path);
zw_bool                 zw_write(zw_zip archive, const void* data, size_t data_len);
zw_bool                 zw_write_text(zw_zip archive, const char* text);
zw_bool                 zw_finish(zw_zip archive);

typedef struct zw_output_stream {
	void*               user_data;
	size_t              (*write)(struct zw_output_stream* stream, const void* buf, size_t size);
	void                (*close)(struct zw_output_stream* stream);
	int                 error;
} zw_output_stream;

struct zw_zip_options {
	zw_output_stream   stream;
};

#ifdef __cplusplus
}
#endif // def __cplusplus

#endif // ndef ZIP_WRITE_H_INCLUDED

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#ifdef ZIP_WRITE_IMPLEMENT
#ifndef ZIP_WRITE_IMPLEMENTATION_DEFINED
#define ZIP_WRITE_IMPLEMENTATION_DEFINED

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif // def __cplusplus

////////////////////////////////////////////////////////////////

#if defined(ZW_MALLOC) && defined(ZW_FREE) && (defined(ZW_REALLOC) || defined(ZW_REALLOC_SIZED))
// ok
#elif !defined(ZW_MALLOC) && !defined(ZW_FREE) && !defined(ZW_REALLOC) && !defined(ZW_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of ZW_MALLOC, ZW_FREE, and ZW_REALLOC (or ZW_REALLOC_SIZED)."
#endif

#ifndef ZW_MALLOC
#include <malloc.h>
#define ZW_MALLOC(sz)          malloc(sz)
#define ZW_REALLOC(p,newsz)    realloc(p,newsz)
#define ZW_FREE(p)             free(p)
#endif

#ifndef ZW_REALLOC_SIZED
#define ZW_REALLOC_SIZED(p,oldsz,newsz) ZW_REALLOC(p,newsz)
#endif

#ifndef ZW_MEMMOVE
#define ZW_MEMMOVE(a,b,sz)     memmove(a,b,sz)
#endif

////////////////////////////////////////////////////////////////

#define ZW_CONCAT2(a, b) a ## b
#define ZW_CONCAT(a, b) ZW_CONCAT2(a, b)

#ifndef ZW_ASSERT
#include <assert.h>
#define ZW_ASSERT(x) assert(x)
#endif

#ifndef ZW_STATIC_ASSERT
#define ZW_STATIC_ASSERT(condition) \
	struct ZW_CONCAT(static_assertion_at_line_, __LINE__) { int ZW_CONCAT(static_assertion_failed_at_line_, __LINE__) : !!(condition); }
#endif

////////////////////////////////////////////////////////////////

typedef unsigned char      zw_u8;
typedef unsigned short     zw_u16;
typedef unsigned int       zw_u32;
typedef unsigned long long zw_u64;

ZW_STATIC_ASSERT(sizeof(zw_u8 ) == 1);
ZW_STATIC_ASSERT(sizeof(zw_u16) == 2);
ZW_STATIC_ASSERT(sizeof(zw_u32) == 4);
ZW_STATIC_ASSERT(sizeof(zw_u64) == 8);

////////////////////////////////////////////////////////////////

#ifndef ZW_INLINE
	#ifdef _MSC_VER
		#define ZW_INLINE __forceinline
	#elif defined(__GNUC__)
		#define ZW_INLINE inline __attribute__((always_inline))
	#else
		#define ZW_INLINE inline
	#endif
#endif

#ifndef ZW_NO_SSE
	#if defined(_MSC_VER) 
		#if !defined(_M_IX86_FP) || _M_IX86_FP != 2
			#define ZW_NO_SSE
		#endif
	#elif defined(__GNUC__) 
		#if !defined(__SSE__) || !defined(__SSE2__)
			#define ZW_NO_SSE
		#endif
	#else
		#define ZW_NO_SSE
	#endif
#endif // ndef ZW_NO_SSE

#if defined(__GNUC__)
	#define ZW__GCC_TARGET(name)	__attribute__((target(name)))
#else
	#define ZW__GCC_TARGET(name)
#endif

#ifndef ZW_NO_SSE
	#include <emmintrin.h>
#endif // ndef ZW_NO_SSE

#ifndef ZW_NO_BSF
	#ifdef _MSC_VER
		#include <intrin.h>
		__forceinline zw_u8 zw__bsf(zw_u32 mask) {
			unsigned long ret;
			_BitScanForward(&ret, mask);
			return (zw_u8)ret;
		}
	#elif defined(__GNUC__)
		#define zw__bsf(mask)  ((zw_u8)__builtin_ctz(mask))
	#else
		#define ZW_NO_BSF
	#endif
#endif // ndef ZW_NO_BSF

#ifdef ZW_NO_BSF
	ZW_INLINE zw_u8 zw__bsf(zw_u32 mask) {
		static const zw_u8 MultiplyDeBruijnBitPosition2[32] = {
		  0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 
		  31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
		};
		return MultiplyDeBruijnBitPosition2[(zw_u32)((v & -v) * 0x077CB531U) >> 27];
	}
#endif // def ZW_NO_BSF

////////////////////////////////////////////////////////////////

static zw_u32 zw__crc32(const zw_u8* buffer, size_t len, zw_u32 initial) {
	static const zw_u32 crc_table[256] = {
		0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
		0x0eDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
		0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
		0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
		0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
		0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
		0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
		0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
		0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
		0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
		0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
		0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
		0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
		0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
		0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
		0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
		0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
		0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
		0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
		0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
		0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
		0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
		0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
		0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
		0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
		0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
		0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
		0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
		0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
		0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
		0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
		0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
	};

	zw_u32 crc = ~initial;
	size_t i;
	for (i = 0; i < len; ++i)
		crc = (crc >> 8) ^ crc_table[buffer[i] ^ (crc & 0xff)];

	return ~crc;
}

////////////////////////////////////////////////////////////////
// Stretchy buffer
// zw__sbpush() == vector<>::push_back()
// zw__sbcount() == vector<>::size()
////////////////////////////////////////////////////////////////

#define zw__sbraw(a)            ((size_t*) (a) - 2)
#define zw__sbm(a)              zw__sbraw(a)[0]
#define zw__sbn(a)              zw__sbraw(a)[1]

#define zw__sbneedgrow(a,n)     ((a)==0 || zw__sbn(a)+n >= zw__sbm(a))
#define zw__sbmaybegrow(a,n)    (zw__sbneedgrow(a,(n)) ? zw__sbgrow(a,n) : 0)
#define zw__sbgrow(a,n)         zw__sbgrowf((void **) &(a), (n), sizeof(*(a)))

#define zw__sbpush(a, v)        (zw__sbmaybegrow(a,1), (a)[zw__sbn(a)++] = (v))
#define zw__sbcount(a)          ((a) ? zw__sbn(a) : 0)
#define zw__sbfree(a)           zw__sbfreef((void**) &(a))

static void zw__sbfreef(void** arr) {
	if (*arr) {
		ZW_FREE(zw__sbraw(*arr));
		*arr = NULL;
	}
}

static void *zw__sbgrowf(void **arr, size_t increment, size_t itemsize) {
	size_t m = *arr ? 2*zw__sbm(*arr)+increment : increment+1;
	void *p = ZW_REALLOC_SIZED(*arr ? zw__sbraw(*arr) : 0, *arr ? (zw__sbm(*arr)*itemsize + sizeof(size_t)*2) : 0, itemsize * m + sizeof(size_t)*2);
	ZW_ASSERT(p);
	if (p) {
		if (!*arr) ((size_t *) p)[1] = 0;
		*arr = (void *) ((size_t *) p + 2);
		zw__sbm(*arr) = m;
	}
	return *arr;
}

////////////////////////////////////////////////////////////////

typedef struct zw__zip_details {
	zw_u32              magic;

	unsigned            bitbuf;
	unsigned            bitcount;
	zw_u8               quality;

	zw_u8*              out;
	zw_u16              out_cursor;
	zw_u16              out_total;
	
	zw_u8*              window;
	zw_u16              in_cursor;
	zw_u16              in_total;

	zw_u16**            hash_table;

	zw_output_stream    stream;
	zw_u64              offset;

	struct {
	    zw_u64          start_offset;
	    zw_u64          compressed_size;
	    zw_u64          uncompressed_size;
	    zw_u32          crc;
	    zw_u16          name_length;
	    char            name_buf[64];
	    char*           name;
	}                   current_file;

	zw_u16              time;
	zw_u16              date;

	zw_u8*              central_dir;
	zw_u64              num_files;
} zw__zip_details;

#define ZW_FOURCC(a, b, c, d)       (((a)) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define ZW_ROUND_UP(x, pow2)        (((x)+((pow2)-1)) & ~((pow2)-1))

enum {
	// only used for easier identification in memory dumps
	zw__archive_magic = ZW_FOURCC('u', 'Z', 'I', 'P'), 
};

////////////////////////////////////////////////////////////////

static zw_bool zw__write_to_stream(zw_zip archive, const void* data, size_t size) {
	if (!size)
		return zw_true;
	if (archive->stream.error)
		return zw_false;

	size_t written = archive->stream.write(&archive->stream, data, size);
	archive->offset += written;
	if (written == size)
		return zw_true;

	// if write failed and no explicit error code was set, use a default one
	if (!archive->stream.error)
		archive->stream.error = 1;
	return zw_false;
}

static void zw__flush_compressed_bytes(zw_zip archive) {
	if (archive->out_cursor > 0) {
		archive->current_file.compressed_size += archive->out_cursor;
		zw__write_to_stream(archive, archive->out, archive->out_cursor);
		archive->out_cursor = 0;
	}
}

static ZW_INLINE void zw__flush_bits(zw_zip archive) {
   while (archive->bitcount >= 8) {
		archive->out[archive->out_cursor++] = archive->bitbuf & 0xff;
		if (archive->out_cursor == archive->out_total) {
			zw__flush_compressed_bytes(archive);
		}
		archive->bitbuf >>= 8;
		archive->bitcount -= 8;
   }
}

static ZW_INLINE unsigned int zw__zlib_bitrev(unsigned int code, int codebits) {
	unsigned int res = 0;
	while (codebits--) {
		res = (res << 1) | (code & 1);
		code >>= 1;
	}
	return res;
}

static ZW_INLINE zw_u16 zw__zlib_countm(const zw_u8* a, const zw_u8* b, size_t limit) {
	zw_u16 i = 0;
	if (limit > 258)
		limit = 258;

	#ifndef ZW_NO_SSE
	{
		while (i + 32u <= limit) {
			__m128i va0 = _mm_loadu_si128((const __m128i*)(a + i + 0));
			__m128i va1 = _mm_loadu_si128((const __m128i*)(a + i + 16));
			__m128i vb0 = _mm_loadu_si128((const __m128i*)(b + i + 0));
			__m128i vb1 = _mm_loadu_si128((const __m128i*)(b + i + 16));
			zw_u32 mask = (zw_u32)_mm_movemask_epi8(_mm_cmpeq_epi8(va0, vb0)) | ((zw_u32)_mm_movemask_epi8(_mm_cmpeq_epi8(va1, vb1)) << 16);
			mask = ~mask;
			if (mask != 0)
				return (zw_u16)(i + zw__bsf(mask));
			i += 32u;
		}

		if (i + 16u <= limit) {
			__m128i va = _mm_loadu_si128((const __m128i*)(a + i));
			__m128i vb = _mm_loadu_si128((const __m128i*)(b + i));
			zw_u32 mask = (zw_u32)_mm_movemask_epi8(_mm_cmpeq_epi8(va, vb)) ^ 0xffffu;
			if (mask != 0)
				return (zw_u16)(i + zw__bsf(mask));
			i += 16u;
		}
	}
	#endif // ZW_NO_SSE

	for (; i < limit; ++i)
		if (a[i] != b[i]) break;
	return i;
}

static ZW_INLINE zw_u32 zw__zhash(const zw_u8 *data) {
	zw_u32 hash = data[0] + (data[1] << 8) + (data[2] << 16);
	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;
	return hash;
}

#define zw__zlib_flush() zw__flush_bits(archive)
#define zw__zlib_add(code,codebits) \
	(archive->bitbuf |= (code) << archive->bitcount, archive->bitcount += (codebits), zw__zlib_flush())
#define zw__zlib_huffa(b,c)  zw__zlib_add(zw__zlib_bitrev(b,c),c)
// default huffman tables
#define zw__zlib_huff1(n)  zw__zlib_huffa(0x30 + (n), 8)
#define zw__zlib_huff2(n)  zw__zlib_huffa(0x190 + (n)-144, 9)
#define zw__zlib_huff3(n)  zw__zlib_huffa(0 + (n)-256,7)
#define zw__zlib_huff4(n)  zw__zlib_huffa(0xc0 + (n)-280,8)
#define zw__zlib_huff(n)  ((n) <= 143 ? zw__zlib_huff1(n) : (n) <= 255 ? zw__zlib_huff2(n) : (n) <= 279 ? zw__zlib_huff3(n) : zw__zlib_huff4(n))
#define zw__zlib_huffb(n) ((n) <= 143 ? zw__zlib_huff1(n) : zw__zlib_huff2(n))

enum {
	zw__hash_size = 16384,
};

static void zw__flush_input(zw_zip archive) {
	static const zw_u16 lengthc[]     = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258, 259 };
	static const zw_u8  lengtheb[]    = { 0,0,0,0,0,0,0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0 };
	static const zw_u16 distc[]       = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577, 32768 };
	static const zw_u8  disteb[]      = { 0,0,0,0,1,1,2, 2, 3, 3, 4, 4, 5, 5,  6,  6,  7,  7,  8,  8,   9,   9,  10,  10,  11,  11,  12,   12,   13,   13 };

	const zw_u8* data = archive->window + 32768;
	zw_u16 i,j, data_len = archive->in_cursor;

	if (data_len == 0)
		return;
	ZW_ASSERT(data_len <= 32768);

	i=0;
	while (i < data_len-3) {
		// hash next 3 bytes of data to be compressed
		zw_u32 h = zw__zhash(data + i) & (zw__hash_size - 1);
		zw_u16 best = 3;
		unsigned char *bestloc = 0;
		zw_u16 *hlist = archive->hash_table[h];
		size_t n = zw__sbcount(hlist);
		for (j = 0; j < n; ++j) {
			if (hlist[j] > i) { // if entry lies within window
				zw_u16 d = zw__zlib_countm(archive->window + hlist[j], data + i, data_len - i);
				if (d >= best) {
					best = d;
					bestloc = hlist[j] + archive->window;
				}
			}
		}
		// when hash table entry is too long, delete half the entries
		if (hlist && zw__sbn(hlist) == 2u * archive->quality) {
			ZW_MEMMOVE(hlist, hlist + archive->quality, sizeof(hlist[0]) * archive->quality);
			zw__sbn(hlist) = archive->quality;
		}
		zw__sbpush(archive->hash_table[h], (zw_u16)(i + 32768));

		if (bestloc) {
			// "lazy matching" - check match at *next* byte, and if it's better, do cur byte as literal
			h = zw__zhash(data + i + 1) & (zw__hash_size - 1);
			hlist = archive->hash_table[h];
			n = zw__sbcount(hlist);
			for (j = 0; j < n; ++j) {
				if (hlist[j] > i + 1) {
					zw_u16 e = zw__zlib_countm(archive->window + hlist[j], data + i + 1, data_len - i - 1);
					if (e > best) { // if next match is better, bail on current match
						bestloc = NULL;
						break;
					}
				}
			}
		}

		if (bestloc) {
			zw_u16 d = (zw_u16)(data + i - bestloc); // distance back
			ZW_ASSERT(d <= 32767 && best <= 258);
			for (j=0; best>lengthc[j+1]-1; ++j);
			zw__zlib_huff(j + 257);
			if (lengtheb[j]) zw__zlib_add(best - lengthc[j], lengtheb[j]);
			for (j=0; d>distc[j+1]-1; ++j);
			zw__zlib_add(zw__zlib_bitrev(j, 5), 5);
			if (disteb[j]) zw__zlib_add(d - distc[j], disteb[j]);
			i += best;
		} else {
			zw__zlib_huffb(data[i]);
			++i;
		}
	}

	// write out final bytes
	for (; i < data_len; ++i)
		zw__zlib_huffb(data[i]);

	// slide window and remove hash table entries that point too far back
	for (i = 0; i < zw__hash_size; ++i) {
		zw_u16 *hlist = archive->hash_table[i];
		size_t valid, total;
		if (!hlist)
			continue;

		valid = 0;
		total = zw__sbn(hlist);
		for (j = 0; j < total; ++j) {
			zw_u16 ofs = hlist[j];
			if (ofs >= archive->in_cursor) {
				hlist[valid++] = ofs - archive->in_cursor;
			}
		}

		zw__sbn(hlist) = valid;
	}
	ZW_MEMMOVE(archive->window, archive->window + archive->in_cursor, 32768);

	archive->current_file.uncompressed_size += data_len;
	archive->current_file.crc = zw__crc32(data, data_len, archive->current_file.crc);

	archive->in_cursor = 0;
}

////////////////////////////////////////////////////////////////
// ZIP format details //////////////////////////////////////////
////////////////////////////////////////////////////////////////

enum {
	zw__zip_sig_local_file_header       = 0x04034b50,
	zw__zip_sig_central_dir_file_header = 0x02014b50,
	zw__zip_sig_eocd                    = 0x06054b50,
	zw__zip_sig_eocd64                  = 0x06064b50,
	zw__zip_sig_eocdloc64               = 0x07064b50,

	zw__zip_info64_id                   = 0x0001,
	zw__zip_file_system_fat             = 0,

	zw__zip_compression_method_store    = 0,
	zw__zip_compression_method_deflate  = 8,

	zw__zip_flag_has_data_desc          = 1 << 3,
};

#pragma pack(push, 1)

typedef struct {
	zw_u32              signature;
	zw_u16              version;
	zw_u16              flags;
	zw_u16              compression_method;
	zw_u16              file_time;
	zw_u16              file_date;
	zw_u32              crc;
	zw_u32              compressed_size;
	zw_u32              uncompressed_size;
	zw_u16              file_name_length;
	zw_u16              extra_field_length;
} zw__zip_local_file_header;
ZW_STATIC_ASSERT(sizeof(zw__zip_local_file_header) == 30);

typedef struct {
	zw_u32              crc;
	zw_u32              compressed_size;
	zw_u32              uncompressed_size;
} zw__zip_data_descriptor;
ZW_STATIC_ASSERT(sizeof(zw__zip_data_descriptor) == 12);

typedef struct {
	zw_u32              signature;
	zw_u8               spec_version;
	zw_u8               file_system;
	zw_u16              required_version;
	zw_u16              flags;
	zw_u16              compression_method;
	zw_u16              file_time;
	zw_u16              file_date;
	zw_u32              crc;
	zw_u32              compressed_size;
	zw_u32              uncompressed_size;
	zw_u16              file_name_length;
	zw_u16              extra_field_length;
	zw_u16              file_comment_length;
	zw_u16              start_disk;
	zw_u16              internal_attributes;
	zw_u32              external_attributes;
	zw_u32              local_header_relative_offset;
} zw__zip_central_dir_file_header;
ZW_STATIC_ASSERT(sizeof(zw__zip_central_dir_file_header) == 46);

typedef struct
{
	zw_u16              id;
	zw_u16              length;
} zw__zip_extra_header;
ZW_STATIC_ASSERT(sizeof(zw__zip_extra_header) == 4);

typedef struct {
	zw__zip_extra_header    header;
	zw_u64                  uncompressed_size;
	zw_u64                  compressed_size;
	zw_u64                  local_header_relative_offset;
} zw__zip_info64;
ZW_STATIC_ASSERT(sizeof(zw__zip_info64) == 28);

typedef struct {
	zw_u32              signature;
	zw_u64              end_of_central_dir_64_size;
	zw_u16              writer_version;
	zw_u16              required_version;
	zw_u32              current_disk;
	zw_u32              central_dir_disk;
	zw_u64              num_central_dir_recs_disk;
	zw_u64              num_central_dir_recs_total;
	zw_u64              central_dir_size;
	zw_u64              central_dir_offset;
} zw__zip_end_of_central_dir_64;
ZW_STATIC_ASSERT(sizeof(zw__zip_end_of_central_dir_64) == 56);

typedef struct {
	zw_u32              signature;
	zw_u32              end_of_central_dir_64_disk;
	zw_u64              end_of_central_dir_64_offset;
	zw_u32              num_total_disks;
} zw__zip_end_of_central_dir_locator_64;
ZW_STATIC_ASSERT(sizeof(zw__zip_end_of_central_dir_locator_64) == 20);

typedef struct {
	zw_u32              signature;
	zw_u16              current_disk;
	zw_u16              central_dir_disk;
	zw_u16              num_central_dir_recs_disk;
	zw_u16              num_central_dir_recs_total;
	zw_u32              central_dir_size;
	zw_u32              central_dir_offset;
	zw_u16              comment_length;
} zw__zip_end_of_central_dir;
ZW_STATIC_ASSERT(sizeof(zw__zip_end_of_central_dir) == 22);

#pragma pack(pop)

// Time and date (MS-DOS format) ///////////////////////////////

static zw_u16 zw__zip_encode_time(unsigned hour, unsigned minute, unsigned second) {
	if (hour > 23)
		hour = 23;
	if (minute > 59)
		hour = 59;
	if (second > 59) // no leap seconds
		second = 59;
	return (zw_u16)((second >> 1) | (minute << 5) | (hour << 11));
}

static zw_u16 zw__zip_encode_date(unsigned year, unsigned month, unsigned day) {
	year = (year >= 1980) ? year - 1980 : 0;
	if (year > 127)
		year = 127;
	if (month > 12)
		month = 12;
	if (day > 31)
		day = 31;
	return (zw_u16)(day | (month << 5) | (year << 9));
}

// STDIO stream ////////////////////////////////////////////////

#include <stdio.h>

static size_t zw__write_stdio(zw_output_stream* stream, const void* data, size_t size) {
	FILE* file = (FILE*)stream->user_data;
	size_t written = fwrite(data, 1, size, file);
	if (written != size)
		stream->error = ferror(file);
	return written;
}

static void zw__close_stdio(struct zw_output_stream* stream) {
	FILE* file = (FILE*)stream->user_data;
	if (file)
		fclose(file);
}

// Archive functionality ///////////////////////////////////////

static zw_bool zw__append_to_central_dir(zw_zip archive, const void* data, size_t size) {
	if (!size)
		return zw_true;
	zw__sbmaybegrow(archive->central_dir, size);
	ZW_MEMMOVE(archive->central_dir + zw__sbn(archive->central_dir), data, size);
	zw__sbn(archive->central_dir) += size;
	return zw_true;
}

zw_zip zw_create(const char* file_path) {
	zw_zip_options options;
	FILE* file = fopen(file_path, "wb");
	zw_zip archive;

	if (!file)
		return NULL;

	memset(&options, 0, sizeof(options));
	options.stream.user_data    = file;
	options.stream.write        = &zw__write_stdio;
	options.stream.close        = &zw__close_stdio;

	archive = zw_create_ex(&options);
	if (!archive) {
		fclose(file);
		return NULL;
	}

	return archive;
}

zw_zip zw_create_ex(const zw_zip_options* options) {
	const size_t archive_bytes  = ZW_ROUND_UP(sizeof(zw__zip_details), 16);
	const size_t window_bytes   = 65536;
	const size_t output_bytes   = 32768;
	const size_t hash_bytes     = zw__hash_size * sizeof(zw_u16**);

	const size_t total_bytes    = archive_bytes + window_bytes + hash_bytes + output_bytes;

	zw_u8* mem_block;
	zw_zip archive;

	time_t rawtime;
	struct tm* local;

	if (!options || !options->stream.write) {
		return NULL;
	}

	mem_block = (zw_u8*) ZW_MALLOC(total_bytes);
	if (!mem_block)
		return NULL;
	memset(mem_block, 0, total_bytes);

	archive = (zw_zip)mem_block;
	archive->magic = zw__archive_magic;
	archive->quality = 8;
	mem_block += archive_bytes;

	archive->window = mem_block;
	archive->in_total = 32768;
	mem_block += window_bytes;

	archive->hash_table = (zw_u16**)mem_block;
	mem_block += hash_bytes;

	archive->out = mem_block;
	archive->out_total = 32768;

	archive->stream = options->stream;
	archive->current_file.name = archive->current_file.name_buf;

	time(&rawtime);
	local = localtime(&rawtime);
	archive->date = zw__zip_encode_date(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
	archive->time = zw__zip_encode_time(local->tm_hour, local->tm_min, local->tm_sec);

	return archive;
}

static zw_bool zw__zip_end_file(zw_zip archive) {
	zw__zip_data_descriptor data_desc;
	zw__zip_central_dir_file_header central_header;
	zw__zip_info64 info64;

	if (!archive || !archive->current_file.name_length)
		return zw_false;

	zw__flush_input(archive);
	zw__zlib_huff(256); // end of block

	// pad with 0 bits to byte boundary
	while (archive->bitcount)
		zw__zlib_add(0,1);
	zw__flush_compressed_bytes(archive);

	data_desc.crc                               = archive->current_file.crc;
	data_desc.compressed_size                   = ~(zw_u32)0;
	data_desc.uncompressed_size                 = ~(zw_u32)0;
	if (!zw__write_to_stream(archive, &data_desc, sizeof(data_desc)))
		return zw_false;

	central_header.signature                    = zw__zip_sig_central_dir_file_header;
	central_header.spec_version                 = 45;
	central_header.file_system                  = zw__zip_file_system_fat;
	central_header.required_version             = 45;
	central_header.flags                        = zw__zip_flag_has_data_desc;
	central_header.compression_method           = zw__zip_compression_method_deflate;
	central_header.file_time                    = archive->time;
	central_header.file_date                    = archive->date;
	central_header.crc                          = archive->current_file.crc;
	central_header.compressed_size              = ~(zw_u32)0;
	central_header.uncompressed_size            = ~(zw_u32)0;
	central_header.file_name_length             = archive->current_file.name_length;
	central_header.extra_field_length           = sizeof(zw__zip_info64);
	central_header.file_comment_length          = 0;
	central_header.start_disk                   = 0;
	central_header.internal_attributes          = 0;
	central_header.external_attributes          = 0;
	central_header.local_header_relative_offset = ~(zw_u32)0;
	if (!zw__append_to_central_dir(archive, &central_header, sizeof(central_header)))
		return zw_false;
	if (!zw__append_to_central_dir(archive, archive->current_file.name, central_header.file_name_length))
		return zw_false;
	
	info64.header.id                            = zw__zip_info64_id;
	info64.header.length                        = sizeof(info64) - sizeof(info64.header);
	info64.compressed_size                      = archive->current_file.compressed_size;
	info64.uncompressed_size                    = archive->current_file.uncompressed_size;
	info64.local_header_relative_offset         = archive->current_file.start_offset;
	if (!zw__append_to_central_dir(archive, &info64, sizeof(info64)))
		return zw_false;

	archive->current_file.name_length = 0;

	return zw_true;
}

zw_bool zw_begin_file(zw_zip archive, const char* file_path) {
	zw__zip_local_file_header local_header;
	size_t name_length, name_capacity;
	zw_u64 offset;
	int i;

	if (!archive)
		return zw_false;
	zw__zip_end_file(archive);

	if (!file_path || !*file_path)
		return zw_false;

	name_length = strlen(file_path);
	if (name_length > 0xfffe)
		name_length = 0xfffe;
	offset = archive->offset;

	local_header.signature              = zw__zip_sig_local_file_header;
	local_header.version                = 45;
	local_header.flags                  = zw__zip_flag_has_data_desc;
	local_header.compression_method     = zw__zip_compression_method_deflate;
	local_header.file_time              = archive->time;
	local_header.file_date              = archive->date;
	local_header.crc                    = 0;
	local_header.compressed_size        = 0;
	local_header.uncompressed_size      = 0;
	local_header.file_name_length       = (zw_u16)name_length;
	local_header.extra_field_length     = 0;

	if (!zw__write_to_stream(archive, &local_header, sizeof(local_header)))
		return zw_false;
	if (!zw__write_to_stream(archive, file_path, name_length))
		return zw_false;

	if (archive->current_file.name == archive->current_file.name_buf)
		name_capacity = sizeof(archive->current_file.name_buf);
	else
		name_capacity = zw__sbm(archive->current_file.name);

	if (name_length >= name_capacity) {
		if (archive->current_file.name == archive->current_file.name_buf)
			archive->current_file.name = NULL;
		else
			zw__sbn(archive->current_file.name) = 0;
		name_capacity = name_length + 1;
		if (name_capacity < 260)
			name_capacity = 260;
		zw__sbgrow(archive->current_file.name, name_capacity);
	}
	ZW_MEMMOVE(archive->current_file.name, file_path, name_length);
	archive->current_file.name[name_length] = 0;

	if (archive->num_files) {
		// reset lengths of hash table chains
		for (i = 0; i < zw__hash_size; ++i) {
			zw_u16 *hlist = archive->hash_table[i];
			if (hlist)
				zw__sbn(hlist) = 0;
		}
	}

	archive->in_cursor = 0;
	archive->out_cursor = 0;
	archive->current_file.name_length = (zw_u16)name_length;
	archive->current_file.compressed_size = 0;
	archive->current_file.uncompressed_size = 0;
	archive->current_file.crc = 0;
	archive->current_file.start_offset = offset;
	archive->num_files++;

	// begin raw deflate block
	zw__zlib_add(1,1);  // BFINAL = 1
	zw__zlib_add(1,2);  // BTYPE  = 01 (fixed Huffman)

	return zw_true;
}

zw_bool zw_write(zw_zip archive, const void* data, size_t data_len) {
	if (!archive || !archive->num_files)
		return zw_false;

	while (data_len > 0) {
		zw_u16 avail = archive->in_total - archive->in_cursor;
		zw_u16 batch = data_len < avail ? (zw_u16)data_len : avail;
		ZW_MEMMOVE(archive->window + 32768 + archive->in_cursor, data, batch);
		archive->in_cursor += batch;
		if (archive->in_cursor == archive->in_total)
			zw__flush_input(archive);
		data_len -= batch;
		data = (const zw_u8*)data + batch;
	}

	return zw_true;
}

zw_bool zw_write_text(zw_zip archive, const char* text) {
	return zw_write(archive, text, strlen(text));
}

zw_bool zw_finish(zw_zip archive) {
	zw__zip_end_of_central_dir_64 eocd64;
	zw__zip_end_of_central_dir_locator_64 eocdloc64;
	zw__zip_end_of_central_dir eocd;

	zw_bool result = zw_true;
	zw_u64 offset;
	size_t central_dir_size;
	unsigned i;

	if (!archive)
		return zw_false;
	zw__zip_end_file(archive);

	for (i=0; i<zw__hash_size; ++i)
		zw__sbfree(archive->hash_table[i]);
	if (archive->current_file.name != archive->current_file.name_buf)
		zw__sbfree(archive->current_file.name);

	offset = archive->offset;
	central_dir_size = zw__sbcount(archive->central_dir);
	if (result && !zw__write_to_stream(archive, archive->central_dir, central_dir_size))
		result = zw_false;
	zw__sbfree(archive->central_dir);

	eocd64.signature                        = zw__zip_sig_eocd64;
	eocd64.end_of_central_dir_64_size       = sizeof(eocd64) - 12;	// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT, 4.3.14.1
	eocd64.writer_version                   = 45;
	eocd64.required_version                 = 45;
	eocd64.current_disk                     = 0;
	eocd64.central_dir_disk                 = 0;
	eocd64.num_central_dir_recs_disk        = archive->num_files;
	eocd64.num_central_dir_recs_total       = archive->num_files;
	eocd64.central_dir_size                 = central_dir_size;
	eocd64.central_dir_offset               = offset;
	offset += central_dir_size;
	if (result && !zw__write_to_stream(archive, &eocd64, sizeof(eocd64)))
		result = zw_false;
	
	eocdloc64.signature                     = zw__zip_sig_eocdloc64;
	eocdloc64.end_of_central_dir_64_disk	= 0;
	eocdloc64.end_of_central_dir_64_offset  = offset;
	eocdloc64.num_total_disks               = 1;
	if (!zw__write_to_stream(archive, &eocdloc64, sizeof(eocdloc64)))
		result = zw_false;
	
	memset(&eocd, 0xff, sizeof(eocd));
	eocd.signature                          = zw__zip_sig_eocd;
	eocd.comment_length                     = 0;
	if (result && !zw__write_to_stream(archive, &eocd, sizeof(eocd)))
		return zw_false;

	if (archive->stream.close)
		archive->stream.close(&archive->stream);
	if (archive->stream.error)
		result = zw_false;

	ZW_FREE(archive);

	return result;
}

#ifdef __cplusplus
}
#endif // def __cplusplus

#endif // ndef ZIP_WRITE_IMPLEMENTATION_DEFINED
#endif // def ZIP_WRITE_IMPLEMENT

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Copyright (c) 2018 Andrei Drexler
Permission is hereby granted, free of charge, to any person obtaining a copy of 
this software and associated documentation files (the "Software"), to deal in 
the Software without restriction, including without limitation the rights to 
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
of the Software, and to permit persons to whom the Software is furnished to do 
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all 
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this 
software, either in source code form or as a compiled binary, for any purpose, 
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this 
software dedicate any and all copyright interest in the software to the public 
domain. We make this dedication for the benefit of the public at large and to 
the detriment of our heirs and successors. We intend this dedication to be an 
overt act of relinquishment in perpetuity of all present and future rights to 
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN 
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION 
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
